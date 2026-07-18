#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/MermaidFontRegistry.h"

#include "mermaid/math/MathMlCssLayout.h"
#include "mermaid/math/MathMlCssPainter.h"
#include "math/MathRenderer.h"

#include <QFont>
#include <QFontMetricsF>
#include <QGlyphRun>
#include <QMap>
#include <QPainter>
#include <QRectF>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace muffin::mermaid::flowchart {

struct FlowLabelPreparedMath {
  qreal fontPixelSize = 0.0;
  muffin::math::MathCssBox box;
  muffin::math::MathCssPaintOperation operation;
};

namespace {

struct Marker {
  QString token;
  QTextCharFormat format;
};

struct MathMlInlineMetrics {
  qreal visualWidth = 0.0;
  qreal advance = 0.0;
  qreal height = 22.0;
  FlowLabelMathStructure structure = FlowLabelMathStructure::None;
  qreal baseline = 0.0;
  qreal inkTop = 0.0;
  qreal inkBottom = 0.0;
};

FlowLabelMathStructure flowMathStructure(muffin::math::MathSemanticKind kind) {
  switch (kind) {
    case muffin::math::MathSemanticKind::Fraction: return FlowLabelMathStructure::Fraction;
    case muffin::math::MathSemanticKind::Radical: return FlowLabelMathStructure::Radical;
    case muffin::math::MathSemanticKind::SupSub: return FlowLabelMathStructure::SupSub;
    case muffin::math::MathSemanticKind::Array: return FlowLabelMathStructure::Array;
    case muffin::math::MathSemanticKind::None: return FlowLabelMathStructure::Plain;
  }
  return FlowLabelMathStructure::Plain;
}

MathMlInlineMetrics sequenceMathMlMetrics(const muffin::math::MathLayoutResult& layout,
                                           qreal renderFontPixelSize) {
  const muffin::math::MathCssBox box = muffin::math::layoutMathMlCssBox(
      layout, renderFontPixelSize, 16.0);
  return {box.width, box.advance, box.height, flowMathStructure(box.semanticKind),
          box.baseline, box.inkTop, box.inkBottom};
}

MathMlInlineMetrics sequenceMathMlMetrics(
    const FlowLabelPreparedMath& prepared) {
  const auto& box = prepared.box;
  return {box.width, box.advance, box.height,
          flowMathStructure(box.semanticKind), box.baseline, box.inkTop,
          box.inkBottom};
}

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

qreal chromiumFallbackWidth(const FlowLabelDocument& label, qsizetype lineStart,
                            const QString& text, qreal qtWidth, qreal fontPixelSize) {
  const bool japaneseRun = std::any_of(text.cbegin(), text.cend(), [](QChar ch) {
    const ushort code = ch.unicode();
    return (code >= 0x3040 && code <= 0x30ff) || (code >= 0x31f0 && code <= 0x31ff);
  });
  qreal width = qtWidth;
  const qsizetype fullWidthCharacters = std::count_if(text.cbegin(), text.cend(), [](QChar ch) {
    const ushort code = ch.unicode();
    return (code >= 0x2e80 && code <= 0x9fff) ||
           (code >= 0x3040 && code <= 0x30ff) ||
           (code >= 0xac00 && code <= 0xd7af);
  });
  if (japaneseRun) {
    // DirectWrite exposes Yu Gothic's 2040/2048-em advance to Chromium, while
    // Qt normalizes the same fallback glyphs to exactly one em.
    width -= fullWidthCharacters * fontPixelSize * (8.0 / 2048.0);
  }
  qsizetype syntheticBoldCjk = 0;
  bool lineOnlyArabicWhitespace = true;
  qsizetype arabicCharacters = 0;
  for (QChar ch : text) {
    const ushort code = ch.unicode();
    if (code >= 0x0600 && code <= 0x06ff)
      ++arabicCharacters;
    else if (!ch.isSpace())
      lineOnlyArabicWhitespace = false;
  }
  for (const auto& range : label.formats) {
    if (range.format.fontWeight() < QFont::Bold) continue;
    const qsizetype begin = std::max<qsizetype>(lineStart, range.start);
    const qsizetype end = std::min<qsizetype>(lineStart + text.size(),
                                              range.start + range.length);
    for (qsizetype index = begin; index < end; ++index) {
      const ushort code = label.text.at(index).unicode();
      if ((code >= 0x2e80 && code <= 0x9fff) ||
          (code >= 0x3040 && code <= 0x30ff))
        ++syntheticBoldCjk;
    }
  }
  // Qt synthesizes bold for SimSun by widening each glyph; Chromium selects a
  // DirectWrite fallback face whose CJK advance remains one em.
  width -= syntheticBoldCjk * fontPixelSize * (37.0 / 2048.0);
  if (lineOnlyArabicWhitespace)
    width += arabicCharacters * fontPixelSize * (8.0 / 2048.0);
  return width;
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
  if (labelType == QLatin1String("markdown")) {
    // Mermaid's SVG-text Math path bypasses Markdown when a Markdown label also
    // contains an HTML break. The markers stay literal and the break collapses
    // between the adjacent text/Math spans.
    if (mathEnabled && source.contains(QStringLiteral("$$")) &&
        source.contains(QRegularExpression(QStringLiteral("<br\\s*/?>"),
                                           QRegularExpression::CaseInsensitiveOption))) {
      QString collapsed = normalizeBreaks(source);
      collapsed.remove(QLatin1Char('\n'));
      FlowLabelDocument result = parseMarkup(std::move(collapsed), false, true);
      result.literalMarkdownMathFallback = true;
      return result;
    }
    return parseMarkup(source, true, mathEnabled);
  }
  if (source.contains(QLatin1Char('<')) ||
      (mathEnabled && source.contains(QStringLiteral("$$"))))
    return parseMarkup(source, false, mathEnabled);
  return {.text = normalizeBreaks(source)};
}

void prepareFlowLabelMath(FlowLabelDocument& label, qreal fontPixelSize) {
  if (!label.sequenceMathMlModel || label.math.isEmpty()) return;
  const qreal renderFontPixelSize = fontPixelSize * 1.21;
  muffin::math::MathRenderer renderer;
  for (FlowLabelMathSpan& span : label.math) {
    if (span.prepared &&
        qFuzzyCompare(span.prepared->fontPixelSize, fontPixelSize))
      continue;
    auto layout = renderer.render(
        span.source, renderFontPixelSize, Qt::black, true);
    if (!layout.valid()) {
      span.prepared.reset();
      continue;
    }
    auto build = muffin::math::buildMathMlPaintOperations(
        layout, renderFontPixelSize, 16.0);
    if (!build.succeeded())
      throw muffin::math::MathMlPaintError(std::move(*build.failure));
    auto prepared = std::make_shared<FlowLabelPreparedMath>();
    prepared->fontPixelSize = fontPixelSize;
    prepared->box = muffin::math::layoutMathMlCssBox(
        layout, renderFontPixelSize, 16.0);
    prepared->operation = std::move(*build.operation);
    span.prepared = std::move(prepared);
  }
}

qreal measureTextRange(const FlowLabelDocument& label, qsizetype start, qsizetype length,
                       const QFont& font) {
  if (length <= 0) return 0.0;
  QTextLayout layout(label.text.mid(start, length), font);
  QTextOption option;
  option.setUseDesignMetrics(true);
  option.setTextDirection(label.direction);
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
  option.setTextDirection(label.direction);
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
  return layoutFlowLabel(label, fontFamily, fontPixelSize, lineHeight).size;
}

FlowLabelLayoutMetrics layoutFlowLabel(const FlowLabelDocument& label,
                                       const QString& fontFamily,
                                       qreal fontPixelSize, qreal lineHeight) {
  QFont font(fontFamily);
  MermaidFontRegistry::configureFont(font, fontFamily);
  font.setPixelSize(static_cast<int>(std::round(fontPixelSize)));
  font.setHintingPreference(QFont::PreferNoHinting);
  const QFontMetricsF metrics(font);
  const QRectF inkMetrics = metrics.tightBoundingRect(QStringLiteral("Mg"));
  // Canvas TextMetrics reports the pixel-aligned ink box used by Chromium's
  // foreignObject labels. Qt exposes the fractional outline box; align its top
  // outward and bottom inward to reproduce actualBoundingBoxAscent/Descent.
  const qreal cssAscent = std::ceil(std::max<qreal>(0.0, -inkMetrics.top()));
  const qreal cssDescent = std::floor(std::max<qreal>(0.0, inkMetrics.bottom()));
  FlowLabelLayoutMetrics result;
  const QStringList lines = label.text.split(QLatin1Char('\n'));
  qsizetype offset = 0;
  for (const QString& line : lines) {
    FlowLabelLineMetrics measured;
    measured.start = offset;
    measured.length = line.size();
    QVector<FlowLabelMathSpan> mathSpans;
    for (const FlowLabelMathSpan& math : label.math)
      if (math.start >= offset && math.start < offset + line.size()) mathSpans.push_back(math);
    if (!mathSpans.isEmpty()) {
      qreal lineWidth = 0.0;
      qreal actualLineHeight = lineHeight;
      qsizetype cursor = offset;
      muffin::math::MathRenderer renderer;
      auto rangeIsRtl = [&](qsizetype start, qsizetype length) {
        if (length <= 0) return false;
        const QString directionText = label.text.mid(start, length).trimmed();
        if (directionText.isEmpty()) return false;
        QTextLayout directionLayout(directionText, font);
        QTextOption directionOption;
        directionOption.setUseDesignMetrics(true);
        directionOption.setTextDirection(label.direction);
        directionLayout.setTextOption(directionOption);
        directionLayout.beginLayout();
        QTextLine directionLine = directionLayout.createLine();
        if (directionLine.isValid()) directionLine.setLineWidth(1e9);
        directionLayout.endLayout();
        const auto runs = directionLine.isValid()
            ? directionLine.glyphRuns(0, -1, QTextLayout::RetrieveAll) : QList<QGlyphRun>{};
        return !runs.isEmpty() &&
            std::all_of(runs.cbegin(), runs.cend(), [](const QGlyphRun& run) {
              return run.isRightToLeft();
            });
      };
      auto mathTextWidth = [&](qsizetype start, qsizetype length) {
        const bool literalMarkdownMarkers = label.text.contains(QLatin1Char('`')) ||
                                            label.text.contains(QStringLiteral("**"));
        if (label.sequenceMathMlModel && !literalMarkdownMarkers) {
          while (length > 0 && label.text.at(start).isSpace()) {
            ++start;
            --length;
          }
          while (length > 0 && label.text.at(start + length - 1).isSpace())
            --length;
        }
        qreal width = 0.0;
        const qsizetype end = start + length;
        const bool segmentHasFullWidth = std::any_of(
            label.text.cbegin() + start, label.text.cbegin() + end, [](QChar ch) {
              const ushort code = ch.unicode();
              return (code >= 0x2e80 && code <= 0x9fff) ||
                     (code >= 0x3040 && code <= 0x30ff) ||
                     (code >= 0xac00 && code <= 0xd7af);
            });
        for (qsizetype runStart = start; runStart < end;) {
          while (segmentHasFullWidth && runStart < end &&
                 label.text.at(runStart).isSpace()) ++runStart;
          if (runStart >= end) break;
          const ushort code = label.text.at(runStart).unicode();
          const bool fullWidth = (code >= 0x2e80 && code <= 0x9fff) ||
                                 (code >= 0x3040 && code <= 0x30ff) ||
                                 (code >= 0xac00 && code <= 0xd7af);
          qsizetype runEnd = runStart + 1;
          while (runEnd < end &&
                 !(segmentHasFullWidth && label.text.at(runEnd).isSpace())) {
            const ushort next = label.text.at(runEnd).unicode();
            const bool nextFullWidth = (next >= 0x2e80 && next <= 0x9fff) ||
                                       (next >= 0x3040 && next <= 0x30ff) ||
                                       (next >= 0xac00 && next <= 0xd7af);
            if (nextFullWidth != fullWidth) break;
            ++runEnd;
          }
          const qreal raw = measureTextRange(label, runStart, runEnd - runStart, font);
          qreal measuredWidth = fullWidth
              ? chromiumFallbackWidth(label, runStart,
                                      label.text.mid(runStart, runEnd - runStart),
                                      raw, fontPixelSize)
              : raw * (label.literalMarkdownMathFallback ? 1.0
                                                         : kFlowMathMlTextScaleX);
          if (label.sequenceMathMlModel && !literalMarkdownMarkers && !fullWidth) {
            const QString segment = label.text.mid(runStart, runEnd - runStart);
            const bool arabic = std::any_of(segment.cbegin(), segment.cend(), [](QChar ch) {
              return ch.unicode() >= 0x0600 && ch.unicode() <= 0x08ff;
            });
            measuredWidth *= arabic ? (33.672 / 34.421875)
                                    : (183.375 / 183.1875);
          }
          width += measuredWidth;
          runStart = runEnd;
        }
        return width;
      };
      qreal visualRight = 0.0;
      for (const FlowLabelMathSpan& math : mathSpans) {
        const qreal textWidth = mathTextWidth(cursor, math.start - cursor);
        if (math.start > cursor)
          measured.runs.push_back({cursor, math.start - cursor, lineWidth, textWidth,
                                   rangeIsRtl(cursor, math.start - cursor), false, font.family()});
        lineWidth += textWidth;
        if (label.sequenceMathMlModel && math.prepared &&
            qFuzzyCompare(math.prepared->fontPixelSize, fontPixelSize)) {
          const MathMlInlineMetrics mathMetrics =
              sequenceMathMlMetrics(*math.prepared);
          measured.runs.push_back(
              {math.start, math.length, lineWidth, mathMetrics.visualWidth,
               false, true, QStringLiteral("KaTeX_Main"),
               mathMetrics.structure});
          auto& mathRun = measured.runs.back();
          mathRun.mathBoxHeight = mathMetrics.height;
          mathRun.mathBaseline = mathMetrics.baseline;
          mathRun.mathInkTop = mathMetrics.inkTop;
          mathRun.mathInkBottom = mathMetrics.inkBottom;
          visualRight = std::max(
              visualRight, lineWidth + mathMetrics.visualWidth);
          lineWidth += mathMetrics.advance;
          actualLineHeight = std::max(actualLineHeight, mathMetrics.height);
          cursor = math.start + math.length;
          continue;
        }
        const muffin::math::MathLayoutResult layout = renderer.render(
            math.source, fontPixelSize * 1.21, Qt::black, true);
        if (layout.valid()) {
          if (label.sequenceMathMlModel) {
            auto paintBuild = muffin::math::buildMathMlPaintOperations(
                layout, fontPixelSize * 1.21, 16.0);
            if (!paintBuild.succeeded())
              throw muffin::math::MathMlPaintError(
                  std::move(*paintBuild.failure));
          }
          const qreal mathScaleX = label.literalMarkdownMathFallback
                                       ? kFlowMathMlLiteralFallbackScaleX
                                       : kFlowMathMlScaleX;
          const qreal nativeMathWidth = layout.naturalSize.width() * mathScaleX;
          const qreal nativeMathHeight = layout.naturalSize.height() * kFlowMathMlScaleY;
          const MathMlInlineMetrics mathMetrics = label.sequenceMathMlModel
              ? sequenceMathMlMetrics(layout, fontPixelSize * 1.21)
              : MathMlInlineMetrics{nativeMathWidth, nativeMathWidth, nativeMathHeight,
                                    FlowLabelMathStructure::Plain};
          measured.runs.push_back({math.start, math.length, lineWidth, mathMetrics.visualWidth,
                                   false, true, QStringLiteral("KaTeX_Main"),
                                   mathMetrics.structure});
          auto& mathRun = measured.runs.back();
          mathRun.mathBoxHeight = mathMetrics.height;
          mathRun.mathBaseline = mathMetrics.baseline;
          mathRun.mathInkTop = mathMetrics.inkTop;
          mathRun.mathInkBottom = mathMetrics.inkBottom;
          visualRight = std::max(visualRight, lineWidth + mathMetrics.visualWidth);
          lineWidth += mathMetrics.advance;
          actualLineHeight = std::max(actualLineHeight, mathMetrics.height);
        }
        cursor = math.start + math.length;
      }
      const qreal tailWidth = mathTextWidth(cursor, offset + line.size() - cursor);
      if (cursor < offset + line.size())
        measured.runs.push_back({cursor, offset + line.size() - cursor, lineWidth,
                                 tailWidth, rangeIsRtl(cursor, offset + line.size() - cursor),
                                 false, font.family()});
      lineWidth += tailWidth;
      if (cursor >= offset + line.size() && !measured.runs.isEmpty() &&
          measured.runs.back().math &&
          measured.runs.back().mathStructure == FlowLabelMathStructure::Fraction)
        lineWidth = std::max<qreal>(0.0, lineWidth - 1.0);
      visualRight = std::max(visualRight, lineWidth);
      if (label.sequenceMathMlModel && actualLineHeight > lineHeight) {
        const bool mixedFallback = std::any_of(line.cbegin(), line.cend(), [](QChar ch) {
          return (ch.unicode() >= 0x2e80 && ch.unicode() <= 0x9fff) ||
                 (ch.unicode() >= 0x0600 && ch.unicode() <= 0x08ff);
        });
        if (mixedFallback) actualLineHeight = std::max(actualLineHeight, 34.0);
      }
      measured.width = lineWidth;
      // The fallback is emitted as one SVG text range (17px ink box) inside
      // the normal 24px label block.
      measured.height = label.literalMarkdownMathFallback ? 17.0 : actualLineHeight;
      measured.ascent = cssAscent;
      measured.descent = cssDescent;
      measured.baseline = (actualLineHeight - cssAscent - cssDescent) / 2.0 + cssAscent;
      result.size.setWidth(std::max(result.size.width(), label.sequenceMathMlModel
          ? std::round(visualRight) : lineWidth));
      result.size.setHeight(result.size.height() + actualLineHeight);
      result.lines.push_back(std::move(measured));
      offset += line.size() + 1;
      continue;
    }
    QTextLayout layout(line, font);
    QTextOption option;
    option.setUseDesignMetrics(true);
    option.setTextDirection(label.direction);
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
    const qreal qtWidth = textLine.isValid() ? textLine.naturalTextWidth()
                                             : metrics.horizontalAdvance(line);
    measured.width = chromiumFallbackWidth(label, offset, line, qtWidth, fontPixelSize);
    measured.height = lineHeight;
    measured.ascent = cssAscent;
    measured.descent = cssDescent;
    measured.baseline = (lineHeight - cssAscent - cssDescent) / 2.0 + cssAscent;
    if (textLine.isValid()) {
      const auto glyphRuns = textLine.glyphRuns(0, -1, QTextLayout::RetrieveAll);
      bool hasGlyphRun = false;
      bool allRunsRtl = true;
      for (const QGlyphRun& run : glyphRuns) {
        const QList<qsizetype> indexes = run.stringIndexes();
        if (indexes.isEmpty()) continue;
        const auto [minimum, maximum] = std::minmax_element(indexes.cbegin(), indexes.cend());
        const QRectF bounds = run.boundingRect();
        measured.runs.push_back({offset + *minimum, *maximum - *minimum + 1,
                                 bounds.x(), bounds.width(), run.isRightToLeft(), false,
                                 run.rawFont().familyName()});
        hasGlyphRun = true;
        allRunsRtl = allRunsRtl && run.isRightToLeft();
      }
      // For the bundled Noto faces, SVG getBBox() uses the painted glyph bounds
      // while QTextLine includes direction-dependent advances/bearings. Keep
      // the legacy system-font correction path unchanged.
      if (hasGlyphRun && allRunsRtl &&
          font.family().contains(QStringLiteral("Noto Sans"), Qt::CaseInsensitive))
        measured.width = metrics.tightBoundingRect(line).width();
      std::sort(measured.runs.begin(), measured.runs.end(),
                [](const FlowLabelVisualRun& a, const FlowLabelVisualRun& b) {
                  return a.x < b.x;
                });
      if (qtWidth > 0.0 && measured.width != qtWidth) {
        const qreal scale = measured.width / qtWidth;
        for (auto& run : measured.runs) {
          run.x *= scale;
          run.width *= scale;
        }
      }
    }
    result.size.setWidth(std::max(result.size.width(), measured.width));
    result.size.setHeight(result.size.height() + lineHeight);
    result.lines.push_back(std::move(measured));
    offset += line.size() + 1;
  }
  result.size.setHeight(std::max(lineHeight, result.size.height()));
  return result;
}

void paintFlowLabel(QPainter& painter, const FlowLabelDocument& label,
                    const QRectF& rect, const QString& fontFamily,
                    qreal fontPixelSize, qreal lineHeight,
                    const QColor& color, bool centerVertically) {
  QFont font(fontFamily);
  MermaidFontRegistry::configureFont(font, fontFamily);
  font.setPixelSize(static_cast<int>(std::round(fontPixelSize)));
  font.setHintingPreference(QFont::PreferNoHinting);
  FlowLabelDocument paintedLabel = label;
  prepareFlowLabelMath(paintedLabel, fontPixelSize);
  bool autoWrapped = false;
  const qreal availableWidth = std::max<qreal>(0.0, rect.width() - 4.0);
  if (paintedLabel.math.isEmpty() && !paintedLabel.text.contains(QLatin1Char('\n')) &&
      availableWidth > 0.0 &&
      measureTextRange(paintedLabel, 0, paintedLabel.text.size(), font) > availableWidth) {
    qsizetype lineStart = 0;
    qsizetype separator = paintedLabel.text.indexOf(QLatin1Char(' '));
    while (separator >= 0) {
      const qsizetype nextSeparator = paintedLabel.text.indexOf(QLatin1Char(' '), separator + 1);
      const qsizetype candidateEnd = nextSeparator >= 0 ? nextSeparator : paintedLabel.text.size();
      if (measureTextRange(paintedLabel, lineStart, candidateEnd - lineStart, font) >
              availableWidth && separator > lineStart) {
        paintedLabel.text[separator] = QLatin1Char('\n');
        autoWrapped = true;
        lineStart = separator + 1;
      }
      separator = nextSeparator;
    }
  }
  const qreal effectiveLineHeight = autoWrapped ? 19.3 : lineHeight;
  const FlowLabelLayoutMetrics layoutMetrics =
      layoutFlowLabel(paintedLabel, fontFamily, fontPixelSize, effectiveLineHeight);
  const QSizeF measured = layoutMetrics.size;
  const qreal qtAscent = QFontMetricsF(font).ascent();
  qreal lineTop = centerVertically
                      ? rect.top() + std::max<qreal>(0.0, (rect.height() - measured.height()) / 2.0)
                      : rect.top();
  painter.setPen(color);
  const QStringList lines = paintedLabel.text.split(QLatin1Char('\n'));
  qsizetype offset = 0;
  qsizetype lineIndex = 0;
  muffin::math::MathRenderer renderer;
  for (const QString& line : lines) {
    const FlowLabelLineMetrics& measuredLine = layoutMetrics.lines.at(lineIndex++);
    QVector<FlowLabelMathSpan> mathSpans;
    for (const FlowLabelMathSpan& math : paintedLabel.math)
      if (math.start >= offset && math.start < offset + line.size()) mathSpans.push_back(math);

    qreal actualLineHeight = effectiveLineHeight;
    std::vector<muffin::math::MathLayoutResult> mathLayouts;
    std::vector<MathMlInlineMetrics> mathMetrics;
    mathLayouts.reserve(mathSpans.size());
    mathMetrics.reserve(mathSpans.size());
    for (const FlowLabelMathSpan& math : mathSpans) {
      if (paintedLabel.sequenceMathMlModel && math.prepared &&
          qFuzzyCompare(math.prepared->fontPixelSize, fontPixelSize)) {
        mathLayouts.emplace_back();
        mathMetrics.push_back(sequenceMathMlMetrics(*math.prepared));
        actualLineHeight = std::max(
            actualLineHeight, mathMetrics.back().height);
        continue;
      }
      mathLayouts.push_back(renderer.render(math.source, fontPixelSize * 1.21, color, true));
      if (mathLayouts.back().valid()) {
        const qreal mathScaleX = paintedLabel.literalMarkdownMathFallback
                                     ? kFlowMathMlLiteralFallbackScaleX
                                     : kFlowMathMlScaleX;
        const qreal nativeWidth = mathLayouts.back().naturalSize.width() * mathScaleX;
        const qreal nativeHeight = mathLayouts.back().naturalSize.height() * kFlowMathMlScaleY;
        mathMetrics.push_back(paintedLabel.sequenceMathMlModel
            ? sequenceMathMlMetrics(mathLayouts.back(), fontPixelSize * 1.21)
            : MathMlInlineMetrics{nativeWidth, nativeWidth, nativeHeight,
                                  FlowLabelMathStructure::Plain});
        actualLineHeight = std::max(actualLineHeight, mathMetrics.back().height);
      } else {
        mathMetrics.push_back({});
      }
    }

    const qreal lineWidth = measuredLine.width;
    qreal x = rect.left() + (rect.width() - lineWidth) / 2.0;
    if (mathSpans.isEmpty()) {
      const qreal rawWidth = measureTextRange(paintedLabel, offset, line.size(), font);
      drawTextRange(painter, paintedLabel, offset, line.size(), font,
                    QPointF(x, lineTop + measuredLine.baseline - qtAscent),
                    rawWidth > 0.0 ? measuredLine.width / rawWidth : 1.0);
    } else {
      qsizetype cursor = offset;
      qsizetype runIndex = 0;
      for (qsizetype i = 0; i < mathSpans.size(); ++i) {
        const qsizetype textLength = mathSpans.at(i).start - cursor;
        const qreal rawTextWidth = measureTextRange(paintedLabel, cursor, textLength, font);
        const FlowLabelVisualRun* textRun = nullptr;
        if (textLength > 0 && runIndex < measuredLine.runs.size() &&
            !measuredLine.runs.at(runIndex).math)
          textRun = &measuredLine.runs.at(runIndex++);
        const qreal textWidth = textRun ? textRun->width : 0.0;
        drawTextRange(painter, paintedLabel, cursor, textLength, font,
                      QPointF(x, lineTop + measuredLine.baseline - qtAscent),
                      rawTextWidth > 0.0 ? textWidth / rawTextWidth : 1.0);
        x += textWidth;
        const FlowLabelVisualRun* mathRun = nullptr;
        if (runIndex < measuredLine.runs.size() && measuredLine.runs.at(runIndex).math)
          mathRun = &measuredLine.runs.at(runIndex++);
        const auto& mathLayout = mathLayouts.at(static_cast<size_t>(i));
        const auto& mathSpan = mathSpans.at(i);
        if (paintedLabel.sequenceMathMlModel && mathSpan.prepared &&
            qFuzzyCompare(mathSpan.prepared->fontPixelSize,
                          fontPixelSize)) {
          const auto& prepared = *mathSpan.prepared;
          painter.save();
          painter.translate(
              x, lineTop + measuredLine.baseline - prepared.box.baseline);
          muffin::math::paintMathMlOperation(
              painter, prepared.operation, color);
          painter.restore();
          x = runIndex < measuredLine.runs.size()
              ? rect.left() + (rect.width() - lineWidth) / 2.0 +
                    measuredLine.runs.at(runIndex).x
              : x + mathMetrics.at(static_cast<size_t>(i)).advance;
          cursor = mathSpan.start + mathSpan.length;
          continue;
        }
        if (mathLayout.valid()) {
          const auto& inlineMetrics = mathMetrics.at(static_cast<size_t>(i));
          if (paintedLabel.sequenceMathMlModel) {
            const muffin::math::MathCssBox cssBox =
                muffin::math::layoutMathMlCssBox(
                    mathLayout, fontPixelSize * 1.21, 16.0);
            painter.save();
            painter.translate(x, lineTop + measuredLine.baseline - cssBox.baseline);
            const auto paintBuild =
                muffin::math::paintMathMlOperations(
                painter, mathLayout, color, fontPixelSize * 1.21);
            painter.restore();
            Q_ASSERT(paintBuild.succeeded());
            x = runIndex < measuredLine.runs.size()
                ? rect.left() + (rect.width() - lineWidth) / 2.0 + measuredLine.runs.at(runIndex).x
                : x + inlineMetrics.advance;
            cursor = mathSpans.at(i).start + mathSpans.at(i).length;
            continue;
          }
          const qreal scaleX = mathRun
              ? mathRun->width / mathLayout.naturalSize.width() : 1.0;
          const qreal scaleY =
              inlineMetrics.height / mathLayout.naturalSize.height();
          const qreal targetTop = lineTop + measuredLine.baseline -
                                  mathLayout.baseline * scaleY;
          painter.save();
          painter.translate(x, targetTop);
          painter.scale(scaleX, scaleY);
          mathLayout.paint(painter, QPointF());
          painter.restore();
          x = runIndex < measuredLine.runs.size()
              ? rect.left() + (rect.width() - lineWidth) / 2.0 + measuredLine.runs.at(runIndex).x
              : x + inlineMetrics.advance;
        }
        cursor = mathSpans.at(i).start + mathSpans.at(i).length;
      }
      const qsizetype textLength = offset + line.size() - cursor;
      const qreal rawTextWidth = measureTextRange(paintedLabel, cursor, textLength, font);
      const FlowLabelVisualRun* textRun = nullptr;
      if (textLength > 0 && runIndex < measuredLine.runs.size() &&
          !measuredLine.runs.at(runIndex).math)
        textRun = &measuredLine.runs.at(runIndex);
      drawTextRange(painter, paintedLabel, cursor, textLength, font,
                    QPointF(x, lineTop + measuredLine.baseline - qtAscent),
                    rawTextWidth > 0.0 && textRun ? textRun->width / rawTextWidth : 1.0);
    }
    lineTop += actualLineHeight;
    offset += line.size() + 1;
  }
}

}  // namespace muffin::mermaid::flowchart
