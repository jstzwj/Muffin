#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/MermaidFontRegistry.h"

#include "mermaid/math/MathMlCssLayout.h"
#include "mermaid/math/MathMlCssPainter.h"
#include "math/MathRenderer.h"

#include <QFont>
#include <QByteArray>
#include <QFontMetricsF>
#include <QGlyphRun>
#include <QMap>
#include <QPainter>
#include <QRectF>
#include <QRegularExpression>
#include <QRawFont>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
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
  bool block = false;
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

MathMlInlineMetrics flowchartMathMlMetrics(
    const FlowLabelPreparedMath& prepared) {
  const auto& box = prepared.box;
  // Flowchart Markdown renders each KaTeX span as a block flex item and the
  // MathML root as display=block. Unlike sequence labels, its horizontal
  // contribution is the complete border box rather than MathML's inline
  // advance (notably for mtable trailing space).
  return {box.width, box.width, box.height,
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

struct ShapedTextMetrics {
  qreal width = 0.0;
  qreal inkWidth = 0.0;
  QVector<FlowLabelVisualRun> runs;
};

quint16 readBigEndianU16(const QByteArray& bytes, qsizetype offset) {
  if (offset < 0 || offset + 2 > bytes.size()) return 0;
  return (quint16(quint8(bytes.at(offset))) << 8) |
         quint16(quint8(bytes.at(offset + 1)));
}

class OpenTypeHorizontalMetrics {
public:
  OpenTypeHorizontalMetrics(const QRawFont& font, qreal cssPixelSize)
      : hmtx_(font.fontTable("hmtx")),
        longMetrics_(readBigEndianU16(font.fontTable("hhea"), 34)),
        unitsPerEm_(font.unitsPerEm()), pixelSize_(cssPixelSize) {}

  qreal advance(quint32 glyph, qreal fallback) const {
    if (hmtx_.isEmpty() || longMetrics_ == 0 || unitsPerEm_ <= 0 ||
        pixelSize_ <= 0.0)
      return fallback;
    const quint32 metric = std::min<quint32>(glyph, longMetrics_ - 1);
    const quint16 designAdvance = readBigEndianU16(hmtx_, metric * 4);
    return qreal(designAdvance) * pixelSize_ / unitsPerEm_;
  }

private:
  QByteArray hmtx_;
  quint16 longMetrics_ = 0;
  qreal unitsPerEm_ = 0.0;
  qreal pixelSize_ = 0.0;
};

ShapedTextMetrics shapeTextRange(const FlowLabelDocument& label, qsizetype start,
                                 qsizetype length, const QFont& font) {
  ShapedTextMetrics result;
  if (length <= 0) return result;
  const QString text = label.text.mid(start, length);
  QTextLayout layout(text, font);
  QTextOption option;
  option.setUseDesignMetrics(true);
  option.setTextDirection(label.direction);
  layout.setTextOption(option);
  QVector<QTextLayout::FormatRange> formats;
  for (const auto& range : label.formats) {
    const qsizetype rangeStart = range.start;
    const qsizetype rangeEnd = range.start + range.length;
    const qsizetype begin = std::max(start, rangeStart);
    const qsizetype end = std::min(start + length, rangeEnd);
    if (end > begin) {
      auto local = range;
      local.start = begin - start;
      local.length = end - begin;
      // The bundled CSS oracle registers a single regular face. Chromium
      // synthesizes weight/slant without changing that face's advance table;
      // Qt may otherwise substitute an unrelated installed bold/italic face.
      if (font.family().contains(QStringLiteral("Noto Sans"),
                                 Qt::CaseInsensitive)) {
        local.format.setFontWeight(QFont::Normal);
        local.format.setFontItalic(false);
      }
      formats.push_back(local);
    }
  }
  layout.setFormats(formats);
  layout.beginLayout();
  QTextLine line = layout.createLine();
  if (line.isValid()) line.setLineWidth(1e9);
  layout.endLayout();
  if (!line.isValid()) return result;

  qreal left = std::numeric_limits<qreal>::max();
  qreal right = std::numeric_limits<qreal>::lowest();
  qreal inkLeft = std::numeric_limits<qreal>::max();
  qreal inkRight = std::numeric_limits<qreal>::lowest();
  const auto glyphRuns = line.glyphRuns(0, -1, QTextLayout::RetrieveAll);
  for (const QGlyphRun& run : glyphRuns) {
    const auto indexes = run.stringIndexes();
    const auto glyphs = run.glyphIndexes();
    const auto positions = run.positions();
    if (indexes.isEmpty() || glyphs.isEmpty() || positions.isEmpty()) continue;
    const auto advances = run.rawFont().advancesForGlyphIndexes(glyphs);
    const OpenTypeHorizontalMetrics fontMetrics(run.rawFont(), font.pixelSize());
    qreal runLeft = std::numeric_limits<qreal>::max();
    qreal runRight = std::numeric_limits<qreal>::lowest();
    qreal runInkLeft = std::numeric_limits<qreal>::max();
    qreal runInkRight = std::numeric_limits<qreal>::lowest();
    qreal qtAdvanceSum = 0.0;
    qreal tableAdvanceSum = 0.0;
    for (qsizetype i = 0; i < positions.size(); ++i) {
      const qreal x = positions.at(i).x();
      const qreal fallbackAdvance =
          i < advances.size() ? advances.at(i).x() : 0.0;
      const qreal advance = fontMetrics.advance(glyphs.at(i), fallbackAdvance);
      qtAdvanceSum += fallbackAdvance;
      tableAdvanceSum += advance;
      runLeft = std::min(runLeft, x);
      runRight = std::max(runRight, x + fallbackAdvance);
      const QRectF glyphInk = run.rawFont().boundingRect(glyphs.at(i))
                                  .translated(positions.at(i));
      if (!glyphInk.isEmpty()) {
        runInkLeft = std::min(runInkLeft, glyphInk.left());
        runInkRight = std::max(runInkRight, glyphInk.right());
      }
    }
    if (!(runRight >= runLeft)) continue;
    if (!run.isRightToLeft())
      runRight += tableAdvanceSum - qtAdvanceSum;
    // Chromium's DOM text box retains shaped advances for LTR fallback runs;
    // RTL ranges use the shaper's visual run box. Both come from the glyph run,
    // never from source-script classification.
    if (runInkLeft != std::numeric_limits<qreal>::max()) {
      inkLeft = std::min(inkLeft, runInkLeft);
      inkRight = std::max(inkRight, runInkRight);
    }
    left = std::min(left, runLeft);
    right = std::max(right, runRight);

    QVector<qsizetype> visualOrder(positions.size());
    for (qsizetype index = 0; index < visualOrder.size(); ++index)
      visualOrder[index] = index;
    std::sort(visualOrder.begin(), visualOrder.end(),
              [&](qsizetype a, qsizetype b) {
                if (!qFuzzyCompare(positions.at(a).x(), positions.at(b).x()))
                  return positions.at(a).x() < positions.at(b).x();
                return indexes.at(a) < indexes.at(b);
              });
    for (qsizetype groupStart = 0; groupStart < visualOrder.size();) {
      qsizetype groupEnd = groupStart + 1;
      int logicalStep = 0;
      while (groupEnd < visualOrder.size()) {
        const qsizetype previous = indexes.at(visualOrder.at(groupEnd - 1));
        const qsizetype current = indexes.at(visualOrder.at(groupEnd));
        const qsizetype delta = current - previous;
        if (std::abs(delta) > 1) break;
        const int nextStep = delta == 0 ? logicalStep : (delta > 0 ? 1 : -1);
        if (logicalStep != 0 && nextStep != 0 && nextStep != logicalStep)
          break;
        if (nextStep != 0) logicalStep = nextStep;
        ++groupEnd;
      }
      qreal groupLeft = std::numeric_limits<qreal>::max();
      qreal groupRight = std::numeric_limits<qreal>::lowest();
      qreal groupQtAdvance = 0.0;
      qreal groupTableAdvance = 0.0;
      qsizetype logicalMinimum = std::numeric_limits<qsizetype>::max();
      qsizetype logicalMaximum = std::numeric_limits<qsizetype>::lowest();
      for (qsizetype item = groupStart; item < groupEnd; ++item) {
        const qsizetype glyphIndex = visualOrder.at(item);
        const qreal fallbackAdvance = glyphIndex < advances.size()
            ? advances.at(glyphIndex).x() : 0.0;
        const qreal advance = fontMetrics.advance(
            glyphs.at(glyphIndex), fallbackAdvance);
        groupQtAdvance += fallbackAdvance;
        groupTableAdvance += advance;
        groupLeft = std::min(groupLeft, positions.at(glyphIndex).x());
        groupRight = std::max(groupRight,
                              positions.at(glyphIndex).x() + fallbackAdvance);
        logicalMinimum = std::min(logicalMinimum, indexes.at(glyphIndex));
        logicalMaximum = std::max(logicalMaximum, indexes.at(glyphIndex));
      }
      if (!run.isRightToLeft())
        groupRight += groupTableAdvance - groupQtAdvance;
      FlowLabelVisualRun visual;
      visual.start = start + logicalMinimum;
      visual.length = logicalMaximum - logicalMinimum + 1;
      visual.x = groupLeft;
      visual.width = std::max<qreal>(0.0, groupRight - groupLeft);
      visual.rightToLeft = logicalStep < 0 ||
          (logicalStep == 0 && run.isRightToLeft());
      visual.fontFamily = run.rawFont().familyName();
      result.runs.push_back(std::move(visual));
      groupStart = groupEnd;
    }
  }
  if (left != std::numeric_limits<qreal>::max()) {
    result.width = std::max<qreal>(0.0, right - left);
    for (auto& run : result.runs) run.x -= left;

    // QGlyphRun::stringIndexes() may be local to a fallback-font subrun on
    // DirectWrite. Reconstruct source ranges from QTextLine cursor geometry so
    // visual runs retain document-relative logical indexes across fallback
    // fonts and bidi reordering.
    QVector<qsizetype> logicalMinimum(
        result.runs.size(), std::numeric_limits<qsizetype>::max());
    QVector<qsizetype> logicalMaximum(
        result.runs.size(), std::numeric_limits<qsizetype>::lowest());
    for (qsizetype character = 0; character < text.size(); ++character) {
      const qreal leading = line.cursorToX(character) - left;
      const qreal trailing = line.cursorToX(character + 1) - left;
      const qreal center = (leading + trailing) / 2.0;
      qsizetype closest = -1;
      qreal closestDistance = std::numeric_limits<qreal>::max();
      for (qsizetype runIndex = 0; runIndex < result.runs.size(); ++runIndex) {
        const auto& visual = result.runs.at(runIndex);
        const qreal runLeft = visual.x;
        const qreal runRight = visual.x + visual.width;
        const qreal distance = center < runLeft ? runLeft - center
            : center > runRight ? center - runRight : 0.0;
        if (distance < closestDistance) {
          closest = runIndex;
          closestDistance = distance;
        }
      }
      if (closest >= 0) {
        logicalMinimum[closest] = std::min(logicalMinimum[closest], character);
        logicalMaximum[closest] = std::max(logicalMaximum[closest], character);
      }
    }
    for (qsizetype runIndex = 0; runIndex < result.runs.size(); ++runIndex) {
      if (logicalMinimum.at(runIndex) ==
          std::numeric_limits<qsizetype>::max())
        continue;
      result.runs[runIndex].start = start + logicalMinimum.at(runIndex);
      result.runs[runIndex].length =
          logicalMaximum.at(runIndex) - logicalMinimum.at(runIndex) + 1;
    }
  }
  if (inkLeft != std::numeric_limits<qreal>::max())
    result.inkWidth = std::max<qreal>(0.0, inkRight - inkLeft);
  std::sort(result.runs.begin(), result.runs.end(),
            [](const FlowLabelVisualRun& a, const FlowLabelVisualRun& b) {
              return a.x < b.x;
            });
  return result;
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
  std::optional<qsizetype> blockItemStart;
  QString plain;
  auto appendItem = [&](qsizetype start, qsizetype length,
                        FlowLabelDomItemKind kind) {
    if (length <= 0) return;
    result.domItems.push_back({start, length, kind});
  };
  auto flush = [&]() {
    if (plain.isEmpty()) return;
    const qsizetype start = result.text.size();
    QTextCharFormat combined;
    for (const Marker& marker : stack) combined.merge(marker.format);
    appendFormatted(result, plain, combined);
    if (!blockItemStart)
      appendItem(start, result.text.size() - start,
                 FlowLabelDomItemKind::AnonymousText);
    plain.clear();
  };

  for (qsizetype i = 0; i < source.size();) {
    QString token;
    QTextCharFormat format;
    qsizetype consumed = 0;
    bool blockToken = false;
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
        if (!blockItemStart)
          appendItem(result.text.size() - 1, 1, FlowLabelDomItemKind::Math);
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
      format.setFontWeight(QFont::Bold); blockToken = true;
    } else if ((closing = htmlToken(QStringLiteral("strong"), true)) ||
               (closing = htmlToken(QStringLiteral("b"), true))) {
      blockToken = true;
    } else if (htmlToken(QStringLiteral("em"), false) || htmlToken(QStringLiteral("i"), false)) {
      format.setFontItalic(true); blockToken = true;
    } else if ((closing = htmlToken(QStringLiteral("em"), true)) ||
               (closing = htmlToken(QStringLiteral("i"), true))) {
      blockToken = true;
    } else if (htmlToken(QStringLiteral("code"), false)) {
      format.setFontFamilies({QStringLiteral("monospace")}); blockToken = true;
    } else if ((closing = htmlToken(QStringLiteral("code"), true))) {
      blockToken = true;
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
    if (!closing && blockToken && !blockItemStart)
      blockItemStart = result.text.size();
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
      stack.push_back({token, format, blockToken});
    }
    if (closing && blockItemStart &&
        std::none_of(stack.cbegin(), stack.cend(),
                     [](const Marker& marker) { return marker.block; })) {
      appendItem(*blockItemStart, result.text.size() - *blockItemStart,
                 FlowLabelDomItemKind::BlockText);
      blockItemStart.reset();
    }
    i += consumed;
  }
  flush();
  if (blockItemStart)
    appendItem(*blockItemStart, result.text.size() - *blockItemStart,
               FlowLabelDomItemKind::BlockText);
  return result;
}

}  // namespace

FlowLabelDocument parseFlowLabel(const QString& source, const QString& labelType,
                                 bool mathEnabled) {
  if (labelType == QLatin1String("markdown")) {
    // Mermaid's flowchart Math renderer bypasses Markdown formatting and wraps
    // the literal text and block MathML spans in one nowrap flex row. HTML
    // breaks therefore collapse as whitespace instead of creating a new line.
    if (mathEnabled && source.contains(QStringLiteral("$$"))) {
      QString collapsed = normalizeBreaks(source);
      collapsed.remove(QLatin1Char('\n'));
      FlowLabelDocument result = parseMarkup(std::move(collapsed), false, true);
      result.literalMarkdownMathFallback = source.contains(
          QRegularExpression(QStringLiteral("<br\\s*/?>"),
                             QRegularExpression::CaseInsensitiveOption));
      return result;
    }
    return parseMarkup(source, true, mathEnabled);
  }
  if (source.contains(QLatin1Char('<')) ||
      (mathEnabled && source.contains(QStringLiteral("$$"))))
    return parseMarkup(source, false, mathEnabled);
  return {.text = normalizeBreaks(source)};
}

qsizetype prepareFlowLabelMath(FlowLabelDocument& label,
                               qreal fontPixelSize) {
  if (label.math.isEmpty()) return 0;
  const qreal renderFontPixelSize = fontPixelSize * 1.21;
  muffin::math::MathRenderer renderer;
  qsizetype preparedCount = 0;
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
    ++preparedCount;
  }
  return preparedCount;
}

qreal measureTextRange(const FlowLabelDocument& label, qsizetype start, qsizetype length,
                       const QFont& font) {
  if (length <= 0) return 0.0;
  QTextLayout layout(label.text.mid(start, length), font);
  QTextOption option;
  // Chromium lays out foreignObject text next to block MathML with raster
  // advances. Sequence SVG text and ordinary flowchart labels retain design
  // metrics, matching their SVG textLength path.
  option.setUseDesignMetrics(label.sequenceMathMlModel || label.math.isEmpty());
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

struct FlowTextRange {
  qsizetype start = 0;
  qsizetype length = 0;
};

QVector<FlowTextRange> visibleDomTextRanges(const FlowLabelDocument& label,
                                           qsizetype start,
                                           qsizetype length) {
  QVector<FlowTextRange> ranges;
  if (length <= 0) return ranges;
  if (label.domItems.isEmpty()) {
    ranges.push_back({start, length});
    return ranges;
  }
  const qsizetype end = start + length;
  const auto collapsibleWhitespace = [](QChar character) {
    const ushort code = character.unicode();
    return code == 0x0009 || code == 0x000a || code == 0x000c ||
           code == 0x000d || code == 0x0020;
  };
  for (const FlowLabelDomItem& item : label.domItems) {
    if (item.kind == FlowLabelDomItemKind::Math) continue;
    qsizetype visibleStart = item.start;
    qsizetype visibleEnd = item.start + item.length;
    while (visibleStart < visibleEnd &&
           collapsibleWhitespace(label.text.at(visibleStart)))
      ++visibleStart;
    while (visibleEnd > visibleStart &&
           collapsibleWhitespace(label.text.at(visibleEnd - 1)))
      --visibleEnd;
    const qsizetype itemStart = std::max(start, visibleStart);
    const qsizetype itemEnd = std::min(end, visibleEnd);
    if (itemEnd <= itemStart) continue;
    ranges.push_back({itemStart, itemEnd - itemStart});
  }
  return ranges;
}

ShapedTextMetrics shapeDomTextRange(const FlowLabelDocument& label,
                                    qsizetype start, qsizetype length,
                                    const QFont& font) {
  ShapedTextMetrics result;
  for (const FlowTextRange& range :
       visibleDomTextRanges(label, start, length)) {
    const qreal itemOrigin = result.width;
    ShapedTextMetrics item = shapeTextRange(
        label, range.start, range.length, font);
    for (auto run : item.runs) {
      run.x += itemOrigin;
      result.runs.push_back(std::move(run));
    }
    result.width += item.width;
    result.inkWidth = std::max(result.inkWidth, itemOrigin + item.inkWidth);
  }
  return result;
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
  option.setUseDesignMetrics(label.sequenceMathMlModel || label.math.isEmpty());
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
  FlowLabelDocument prepared = label;
  prepareFlowLabelMath(prepared, fontPixelSize);
  return layoutFlowLabel(prepared, fontFamily, fontPixelSize, lineHeight).size;
}

qreal measureFlowTextInkWidth(const FlowLabelDocument& label,
                              const QString& fontFamily,
                              qreal fontPixelSize) {
  return measureFlowTextInkWidth(label, 0, label.text.size(), fontFamily,
                                 fontPixelSize);
}

qreal measureFlowTextInkWidth(const FlowLabelDocument& label,
                              qsizetype start, qsizetype length,
                              const QString& fontFamily,
                              qreal fontPixelSize) {
  QFont font(fontFamily);
  MermaidFontRegistry::configureFont(font, fontFamily);
  font.setPixelSize(static_cast<int>(std::round(fontPixelSize)));
  font.setHintingPreference(QFont::PreferNoHinting);
  return shapeTextRange(label, start, length, font).inkWidth;
}

qreal measureFlowTextAdvanceWidth(const FlowLabelDocument& label,
                                  qsizetype start, qsizetype length,
                                  const QString& fontFamily,
                                  qreal fontPixelSize) {
  QFont font(fontFamily);
  MermaidFontRegistry::configureFont(font, fontFamily);
  font.setPixelSize(static_cast<int>(std::round(fontPixelSize)));
  font.setHintingPreference(QFont::PreferNoHinting);
  return shapeTextRange(label, start, length, font).width;
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
      bool allMathPrepared = true;
      for (const FlowLabelMathSpan& math : mathSpans) {
        allMathPrepared = allMathPrepared && math.prepared &&
            qFuzzyCompare(math.prepared->fontPixelSize, fontPixelSize);
      }
      const bool onlyMathPlaceholders = std::all_of(
          line.cbegin(), line.cend(), [](QChar character) {
            return character.isSpace() ||
                   character == QChar::ObjectReplacementCharacter;
          });
      const bool mathOnlyLine = !label.sequenceMathMlModel &&
          !label.literalMarkdownMathFallback && allMathPrepared &&
          onlyMathPlaceholders;
      qreal actualLineHeight = mathOnlyLine ? 0.0 : lineHeight;
      qsizetype cursor = offset;
      muffin::math::MathRenderer renderer;
      auto shapeMathTextRange = [&](qsizetype start, qsizetype length) {
        return shapeDomTextRange(label, start, length, font);
      };
      qreal visualRight = 0.0;
      for (const FlowLabelMathSpan& math : mathSpans) {
        const auto shapedText = shapeMathTextRange(
            cursor, math.start - cursor);
        for (auto run : shapedText.runs) {
          run.x += lineWidth;
          measured.runs.push_back(std::move(run));
        }
        lineWidth += shapedText.width;
        if (math.prepared &&
            qFuzzyCompare(math.prepared->fontPixelSize, fontPixelSize)) {
          const MathMlInlineMetrics mathMetrics = label.sequenceMathMlModel
              ? sequenceMathMlMetrics(*math.prepared)
              : flowchartMathMlMetrics(*math.prepared);
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
          const MathMlInlineMetrics mathMetrics = label.sequenceMathMlModel
              ? sequenceMathMlMetrics(layout, fontPixelSize * 1.21)
              : [&] {
                  const muffin::math::MathCssBox box =
                      muffin::math::layoutMathMlCssBox(
                          layout, fontPixelSize * 1.21, 16.0);
                  return MathMlInlineMetrics{
                      box.width, box.width, box.height,
                      flowMathStructure(box.semanticKind), box.baseline,
                      box.inkTop, box.inkBottom};
                }();
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
      const auto shapedTail = shapeMathTextRange(
          cursor, offset + line.size() - cursor);
      for (auto run : shapedTail.runs) {
        run.x += lineWidth;
        measured.runs.push_back(std::move(run));
      }
      lineWidth += shapedTail.width;
      if (label.sequenceMathMlModel &&
          cursor >= offset + line.size() && !measured.runs.isEmpty() &&
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
      measured.height = label.literalMarkdownMathFallback
          ? 17.0 : actualLineHeight;
      measured.blockHeight = actualLineHeight;
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
    const auto shaped = shapeTextRange(label, offset, line.size(), font);
    measured.width = shaped.width;
    measured.height = lineHeight;
    measured.blockHeight = lineHeight;
    measured.ascent = cssAscent;
    measured.descent = cssDescent;
    measured.baseline = (lineHeight - cssAscent - cssDescent) / 2.0 + cssAscent;
    measured.runs = shaped.runs;
    result.size.setWidth(std::max(result.size.width(), measured.width));
    result.size.setHeight(result.size.height() + lineHeight);
    result.lines.push_back(std::move(measured));
    offset += line.size() + 1;
  }
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
  for (const QString& line : lines) {
    const FlowLabelLineMetrics& measuredLine = layoutMetrics.lines.at(lineIndex++);
    QVector<FlowLabelMathSpan> mathSpans;
    for (const FlowLabelMathSpan& math : paintedLabel.math)
      if (math.start >= offset && math.start < offset + line.size()) mathSpans.push_back(math);

    const qreal lineWidth = measuredLine.width;
    qreal x = rect.left() + (rect.width() - lineWidth) / 2.0;
    if (mathSpans.isEmpty()) {
      const qreal rawWidth = measureTextRange(paintedLabel, offset, line.size(), font);
      drawTextRange(painter, paintedLabel, offset, line.size(), font,
                    QPointF(x, lineTop + measuredLine.baseline - qtAscent),
                    rawWidth > 0.0 ? measuredLine.width / rawWidth : 1.0);
    } else {
      qsizetype runIndex = 0;
      const qreal lineOrigin = x;
      auto drawVisualTextRun = [&](const FlowLabelVisualRun& run) {
        const qreal rawTextWidth = measureTextRange(
            paintedLabel, run.start, run.length, font);
        drawTextRange(painter, paintedLabel, run.start, run.length, font,
                      QPointF(lineOrigin + run.x,
                              lineTop + measuredLine.baseline - qtAscent),
                      rawTextWidth > 0.0 ? run.width / rawTextWidth : 1.0);
      };
      for (qsizetype i = 0; i < mathSpans.size(); ++i) {
        while (runIndex < measuredLine.runs.size() &&
               !measuredLine.runs.at(runIndex).math)
          drawVisualTextRun(measuredLine.runs.at(runIndex++));
        const FlowLabelVisualRun* mathRun =
            runIndex < measuredLine.runs.size() &&
                    measuredLine.runs.at(runIndex).math
                ? &measuredLine.runs.at(runIndex++)
                : nullptr;
        const qreal mathX = mathRun ? lineOrigin + mathRun->x : lineOrigin;
        const auto& mathSpan = mathSpans.at(i);
        if (mathSpan.prepared &&
            qFuzzyCompare(mathSpan.prepared->fontPixelSize,
                          fontPixelSize)) {
          const auto& prepared = *mathSpan.prepared;
          painter.save();
          if (paintedLabel.sequenceMathMlModel) {
            painter.translate(
                mathX, lineTop + measuredLine.baseline - prepared.box.baseline);
          } else {
            painter.translate(
                mathX, lineTop + (measuredLine.blockHeight - prepared.box.height) / 2.0);
          }
          muffin::math::paintMathMlOperation(
              painter, prepared.operation, color);
          painter.restore();
          continue;
        }
      }
      while (runIndex < measuredLine.runs.size()) {
        if (!measuredLine.runs.at(runIndex).math)
          drawVisualTextRun(measuredLine.runs.at(runIndex));
        ++runIndex;
      }
    }
    lineTop += measuredLine.blockHeight;
    offset += line.size() + 1;
  }
}

}  // namespace muffin::mermaid::flowchart
