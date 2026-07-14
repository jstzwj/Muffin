#include "mermaid/flowchart/FlowLabel.h"

#include "math/MathRenderer.h"

#include <QFont>
#include <QFontMetricsF>
#include <QMap>
#include <QPainter>
#include <QRectF>
#include <QRegularExpression>

#include <algorithm>
#include <vector>

namespace muffin::mermaid::flowchart {
namespace {

struct Marker {
  QString token;
  QTextCharFormat format;
};

QString decodeEntity(QStringView source, qsizetype* consumed) {
  *consumed = 0;
  if (!source.startsWith(QLatin1Char('&'))) return {};
  const qsizetype semicolon = source.indexOf(QLatin1Char(';'));
  if (semicolon < 2 || semicolon > 12) return {};
  const QString name = source.mid(1, semicolon - 1).toString();
  static const QMap<QString, QString> named = {
      {QStringLiteral("amp"), QStringLiteral("&")},
      {QStringLiteral("apos"), QStringLiteral("'")},
      {QStringLiteral("gt"), QStringLiteral(">")},
      {QStringLiteral("lt"), QStringLiteral("<")},
      {QStringLiteral("nbsp"), QString(QChar(0x00a0))},
      {QStringLiteral("quot"), QStringLiteral("\"")},
  };
  QString value = named.value(name.toLower());
  if (value.isEmpty() && name.startsWith(QLatin1Char('#'))) {
    bool ok = false;
    const bool hexadecimal = name.size() > 2 && name.at(1).toLower() == QLatin1Char('x');
    const uint codepoint = name.mid(hexadecimal ? 2 : 1).toUInt(&ok, hexadecimal ? 16 : 10);
    if (ok && codepoint <= 0x10ffff && !(codepoint >= 0xd800 && codepoint <= 0xdfff)) {
      const char32_t scalar = codepoint;
      value = QString::fromUcs4(&scalar, 1);
    }
  }
  if (value.isEmpty()) return {};
  *consumed = semicolon + 1;
  return value;
}

QString normalizeBreaks(QString text) {
  static const QRegularExpression br(QStringLiteral(R"(<\s*br\s*/?\s*>)"),
                                      QRegularExpression::CaseInsensitiveOption);
  text.replace(br, QStringLiteral("\n"));
  return text;
}

void appendFormatted(FlowLabelDocument& result, const QString& text,
                     const QTextCharFormat& format) {
  if (text.isEmpty()) return;
  if (format.isEmpty()) {
    result.text += text;
    return;
  }
  QTextLayout::FormatRange range;
  range.start = result.text.size();
  range.length = text.size();
  range.format = format;
  result.text += text;
  result.formats.push_back(range);
}

FlowLabelDocument parseMarkup(QString source, bool markdown, bool mathEnabled) {
  FlowLabelDocument result;
  source = normalizeBreaks(std::move(source));
  QVector<Marker> stack;
  QString plain;
  auto flush = [&]() {
    if (plain.isEmpty()) return;
    QTextCharFormat combined;
    for (const Marker& marker : stack) combined.merge(marker.format);
    appendFormatted(result, plain, combined);
    plain.clear();
  };

  for (qsizetype i = 0; i < source.size();) {
    QString token;
    QTextCharFormat format;
    qsizetype consumed = 0;
    const QStringView rest(source.constData() + i, source.size() - i);
    auto htmlToken = [&](QStringView name, bool closing) {
      const QString candidate = closing ? QStringLiteral("</%1>").arg(name)
                                        : QStringLiteral("<%1>").arg(name);
      if (rest.startsWith(candidate, Qt::CaseInsensitive)) {
        token = candidate.toLower();
        consumed = candidate.size();
        return true;
      }
      return false;
    };

    bool closing = false;
    if (mathEnabled && rest.startsWith(QStringLiteral("$$"))) {
      const qsizetype close = source.indexOf(QStringLiteral("$$"), i + 2);
      if (close >= 0) {
        flush();
        FlowLabelMathSpan math;
        math.start = result.text.size();
        math.source = source.mid(i + 2, close - i - 2);
        result.text += QChar::ObjectReplacementCharacter;
        result.math.push_back(std::move(math));
        i = close + 2;
        continue;
      }
    }
    if (markdown && rest.startsWith(QStringLiteral("**"))) {
      token = QStringLiteral("**"); consumed = 2; format.setFontWeight(QFont::Bold);
    } else if (markdown && rest.startsWith(QLatin1Char('*'))) {
      token = QStringLiteral("*"); consumed = 1; format.setFontItalic(true);
    } else if (markdown && rest.startsWith(QLatin1Char('`'))) {
      token = QStringLiteral("`"); consumed = 1; format.setFontFamilies({QStringLiteral("monospace")});
    } else if (htmlToken(QStringLiteral("strong"), false) || htmlToken(QStringLiteral("b"), false)) {
      format.setFontWeight(QFont::Bold);
    } else if ((closing = htmlToken(QStringLiteral("strong"), true)) ||
               (closing = htmlToken(QStringLiteral("b"), true))) {
    } else if (htmlToken(QStringLiteral("em"), false) || htmlToken(QStringLiteral("i"), false)) {
      format.setFontItalic(true);
    } else if ((closing = htmlToken(QStringLiteral("em"), true)) ||
               (closing = htmlToken(QStringLiteral("i"), true))) {
    } else if (htmlToken(QStringLiteral("code"), false)) {
      format.setFontFamilies({QStringLiteral("monospace")});
    } else if ((closing = htmlToken(QStringLiteral("code"), true))) {
    }

    if (consumed == 0) {
      qsizetype entityLength = 0;
      const QString entity = decodeEntity(rest, &entityLength);
      if (entityLength > 0) {
        plain += entity;
        i += entityLength;
        continue;
      }
      // Unknown tags are retained as text. They are never interpreted by an
      // HTML engine, which preserves content without creating an injection path.
      plain += source.at(i++);
      continue;
    }

    flush();
    if (!stack.isEmpty() && stack.last().token == token) {
      stack.removeLast();
    } else if (closing) {
      const QString open = token;
      for (qsizetype s = stack.size() - 1; s >= 0; --s) {
        if (open.contains(stack.at(s).token.mid(1).section(QLatin1Char('>'), 0, 0), Qt::CaseInsensitive)) {
          stack.remove(s);
          break;
        }
      }
    } else {
      stack.push_back({token, format});
    }
    i += consumed;
  }
  flush();
  return result;
}

}  // namespace

FlowLabelDocument parseFlowLabel(const QString& source, const QString& labelType,
                                 bool mathEnabled) {
  if (labelType == QLatin1String("markdown")) return parseMarkup(source, true, mathEnabled);
  if (source.contains(QLatin1Char('<')) ||
      (mathEnabled && source.contains(QStringLiteral("$$"))))
    return parseMarkup(source, false, mathEnabled);
  return {.text = normalizeBreaks(source)};
}

qreal measureTextRange(const FlowLabelDocument& label, qsizetype start, qsizetype length,
                       const QFont& font) {
  if (length <= 0) return 0.0;
  QTextLayout layout(label.text.mid(start, length), font);
  QTextOption option;
  option.setUseDesignMetrics(true);
  layout.setTextOption(option);
  QVector<QTextLayout::FormatRange> ranges;
  for (const auto& range : label.formats) {
    const qsizetype overlapStart = std::max<qsizetype>(range.start, start);
    const qsizetype overlapEnd = std::min<qsizetype>(range.start + range.length, start + length);
    if (overlapEnd <= overlapStart) continue;
    auto local = range;
    local.start = overlapStart - start;
    local.length = overlapEnd - overlapStart;
    ranges.push_back(local);
  }
  layout.setFormats(ranges);
  layout.beginLayout();
  QTextLine line = layout.createLine();
  if (line.isValid()) line.setLineWidth(1e9);
  layout.endLayout();
  return line.isValid() ? line.naturalTextWidth() : 0.0;
}

QVector<QTextLayout::FormatRange> localFormats(const FlowLabelDocument& label,
                                                qsizetype start, qsizetype length) {
  QVector<QTextLayout::FormatRange> ranges;
  for (const auto& range : label.formats) {
    const qsizetype overlapStart = std::max<qsizetype>(range.start, start);
    const qsizetype overlapEnd = std::min<qsizetype>(range.start + range.length,
                                                     start + length);
    if (overlapEnd <= overlapStart) continue;
    auto local = range;
    local.start = overlapStart - start;
    local.length = overlapEnd - overlapStart;
    ranges.push_back(local);
  }
  return ranges;
}

void drawTextRange(QPainter& painter, const FlowLabelDocument& label,
                   qsizetype start, qsizetype length, const QFont& font,
                   QPointF topLeft, qreal scaleX) {
  if (length <= 0) return;
  QTextLayout layout(label.text.mid(start, length), font);
  QTextOption option;
  option.setUseDesignMetrics(true);
  layout.setTextOption(option);
  layout.setFormats(localFormats(label, start, length));
  layout.beginLayout();
  QTextLine line = layout.createLine();
  if (line.isValid()) line.setLineWidth(1e9);
  layout.endLayout();
  painter.save();
  painter.translate(topLeft);
  painter.scale(scaleX, 1.0);
  layout.draw(&painter, QPointF());
  painter.restore();
}

QSizeF measureFlowLabel(const FlowLabelDocument& label, const QString& fontFamily,
                        qreal fontPixelSize, qreal lineHeight) {
  QFont font(fontFamily);
  font.setPixelSize(static_cast<int>(std::round(fontPixelSize)));
  font.setHintingPreference(QFont::PreferNoHinting);
  const QFontMetricsF metrics(font);
  qreal width = 0.0;
  qreal height = 0.0;
  const QStringList lines = label.text.split(QLatin1Char('\n'));
  qsizetype offset = 0;
  for (const QString& line : lines) {
    QVector<FlowLabelMathSpan> mathSpans;
    for (const FlowLabelMathSpan& math : label.math)
      if (math.start >= offset && math.start < offset + line.size()) mathSpans.push_back(math);
    if (!mathSpans.isEmpty()) {
      qreal lineWidth = 0.0;
      qreal actualLineHeight = lineHeight;
      qsizetype cursor = offset;
      muffin::math::MathRenderer renderer;
      for (const FlowLabelMathSpan& math : mathSpans) {
        lineWidth += measureTextRange(label, cursor, math.start - cursor, font) *
                     kFlowMathMlTextScaleX;
        const muffin::math::MathLayoutResult layout = renderer.render(
            math.source, fontPixelSize * 1.21, Qt::black, true);
        if (layout.valid()) {
          lineWidth += layout.naturalSize.width() * kFlowMathMlScaleX;
          actualLineHeight = std::max(actualLineHeight,
                                      layout.naturalSize.height() * kFlowMathMlScaleY);
        }
        cursor = math.start + math.length;
      }
      lineWidth += measureTextRange(label, cursor, offset + line.size() - cursor, font) *
                   kFlowMathMlTextScaleX;
      width = std::max(width, lineWidth);
      height += actualLineHeight;
      offset += line.size() + 1;
      continue;
    }
    QTextLayout layout(line, font);
    QTextOption option;
    option.setUseDesignMetrics(true);
    layout.setTextOption(option);
    QVector<QTextLayout::FormatRange> ranges;
    for (const auto& range : label.formats) {
      const int start = std::max<int>(range.start, offset);
      const int end = std::min<int>(range.start + range.length, offset + line.size());
      if (end > start) {
        auto local = range;
        local.start = start - offset;
        local.length = end - start;
        ranges.push_back(local);
      }
    }
    layout.setFormats(ranges);
    layout.beginLayout();
    QTextLine textLine = layout.createLine();
    if (textLine.isValid()) textLine.setLineWidth(1e9);
    layout.endLayout();
    width = std::max(width, textLine.isValid() ? textLine.naturalTextWidth()
                                               : metrics.horizontalAdvance(line));
    height += lineHeight;
    offset += line.size() + 1;
  }
  return {width, std::max(lineHeight, height)};
}

void paintFlowLabel(QPainter& painter, const FlowLabelDocument& label,
                    const QRectF& rect, const QString& fontFamily,
                    qreal fontPixelSize, qreal lineHeight,
                    const QColor& color, bool centerVertically) {
  QFont font(fontFamily);
  font.setPixelSize(static_cast<int>(std::round(fontPixelSize)));
  font.setHintingPreference(QFont::PreferNoHinting);
  const QSizeF measured = measureFlowLabel(label, fontFamily, fontPixelSize, lineHeight);
  qreal lineTop = centerVertically
                      ? rect.top() + std::max<qreal>(0.0, (rect.height() - measured.height()) / 2.0)
                      : rect.top();
  painter.setPen(color);
  const QStringList lines = label.text.split(QLatin1Char('\n'));
  qsizetype offset = 0;
  muffin::math::MathRenderer renderer;
  for (const QString& line : lines) {
    QVector<FlowLabelMathSpan> mathSpans;
    for (const FlowLabelMathSpan& math : label.math)
      if (math.start >= offset && math.start < offset + line.size()) mathSpans.push_back(math);

    qreal actualLineHeight = lineHeight;
    std::vector<muffin::math::MathLayoutResult> mathLayouts;
    mathLayouts.reserve(mathSpans.size());
    for (const FlowLabelMathSpan& math : mathSpans) {
      mathLayouts.push_back(renderer.render(math.source, fontPixelSize * 1.21, color, true));
      if (mathLayouts.back().valid())
        actualLineHeight = std::max(actualLineHeight,
                                    mathLayouts.back().naturalSize.height() * kFlowMathMlScaleY);
    }

    qreal lineWidth = 0.0;
    if (mathSpans.isEmpty()) {
      lineWidth = measureTextRange(label, offset, line.size(), font);
    } else {
      qsizetype cursor = offset;
      for (qsizetype i = 0; i < mathSpans.size(); ++i) {
        lineWidth += measureTextRange(label, cursor, mathSpans.at(i).start - cursor, font) *
                     kFlowMathMlTextScaleX;
        if (mathLayouts.at(static_cast<size_t>(i)).valid())
          lineWidth += mathLayouts.at(static_cast<size_t>(i)).naturalSize.width() * kFlowMathMlScaleX;
        cursor = mathSpans.at(i).start + mathSpans.at(i).length;
      }
      lineWidth += measureTextRange(label, cursor, offset + line.size() - cursor, font) *
                   kFlowMathMlTextScaleX;
    }
    qreal x = rect.left() + (rect.width() - lineWidth) / 2.0;
    if (mathSpans.isEmpty()) {
      drawTextRange(painter, label, offset, line.size(), font,
                    QPointF(x, lineTop + (actualLineHeight - lineHeight) / 2.0), 1.0);
    } else {
      qsizetype cursor = offset;
      for (qsizetype i = 0; i < mathSpans.size(); ++i) {
        const qsizetype textLength = mathSpans.at(i).start - cursor;
        const qreal textWidth = measureTextRange(label, cursor, textLength, font) *
                                kFlowMathMlTextScaleX;
        drawTextRange(painter, label, cursor, textLength, font,
                      QPointF(x, lineTop + (actualLineHeight - lineHeight) / 2.0),
                      kFlowMathMlTextScaleX);
        x += textWidth;
        const auto& mathLayout = mathLayouts.at(static_cast<size_t>(i));
        if (mathLayout.valid()) {
          const qreal mathHeight = mathLayout.naturalSize.height() * kFlowMathMlScaleY;
          painter.save();
          painter.translate(x, lineTop + (actualLineHeight - mathHeight) / 2.0);
          painter.scale(kFlowMathMlScaleX, kFlowMathMlScaleY);
          mathLayout.paint(painter, QPointF());
          painter.restore();
          x += mathLayout.naturalSize.width() * kFlowMathMlScaleX;
        }
        cursor = mathSpans.at(i).start + mathSpans.at(i).length;
      }
      const qsizetype textLength = offset + line.size() - cursor;
      drawTextRange(painter, label, cursor, textLength, font,
                    QPointF(x, lineTop + (actualLineHeight - lineHeight) / 2.0),
                    kFlowMathMlTextScaleX);
    }
    lineTop += actualLineHeight;
    offset += line.size() + 1;
  }
}

}  // namespace muffin::mermaid::flowchart
