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
#include <QPainterPath>
#include <QRectF>
#include <QRegularExpression>
#include <QRawFont>

#include <hb.h>
#include <hb-ot.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace muffin::mermaid::flowchart {

// Single source of truth for the QFont used across the FlowLabel measurement
// chain (ink/advance, wrap, layout, bounding metrics, paint) and by other scene
// builders that need font-relative CSS metrics (e.g. Requirement ex/ch). Every
// QFont in this TU is built here so baseWeight/baseStyle and letter/word spacing
// are applied consistently and nowhere is forgotten. Mermaid bold/italic
// Markdown still overrides weight/style per run via format ranges.
QFont makeFlowLabelFont(const QString& fontFamily, qreal fontPixelSize,
                        QFont::Weight weight, QFont::Style style,
                        qreal letterSpacingPx, qreal wordSpacingPx) {
  QFont font(fontFamily);
  MermaidFontRegistry::configureFont(font, fontFamily);
  font.setPixelSize(static_cast<int>(std::round(fontPixelSize)));
  font.setHintingPreference(QFont::PreferNoHinting);
  font.setWeight(weight);
  font.setStyle(style);
  // Guard the spacing setters so the default (0) path is untouched — default
  // rendering must stay byte-identical, and an explicit AbsoluteSpacing(0) could
  // in principle differ from Qt's unset state on some platforms.
  if (letterSpacingPx != 0.0)
    font.setLetterSpacing(QFont::AbsoluteSpacing, letterSpacingPx);
  if (wordSpacingPx != 0.0)
    font.setWordSpacing(wordSpacingPx);
  return font;
}

struct FlowLabelPreparedMath {
  qreal fontPixelSize = 0.0;
  qreal operationScale = 1.0;
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
  qreal inlineInkRight = 0.0;
  qreal height = 22.0;
  FlowLabelMathStructure structure = FlowLabelMathStructure::None;
  qreal baseline = 0.0;
  qreal inkTop = 0.0;
  qreal inkBottom = 0.0;
};

constexpr qreal kMathMlCanonicalCssPixelSize = 16.0;

void scaleMathCssBox(muffin::math::MathCssBox& box, qreal scale) {
  box.width *= scale;
  box.height *= scale;
  box.advance *= scale;
  box.baseline *= scale;
  box.inkTop *= scale;
  box.inkBottom *= scale;
  for (auto& child : box.children) scaleMathCssBox(child, scale);
}

qreal mathMlInlineInkRight(
    const muffin::math::MathCssPaintOperation& operation,
    qreal borderBoxWidth, qreal inlineAdvance) {
  const auto* fraction = std::get_if<muffin::math::MathCssFractionPaint>(
      &operation.payload);
  if (!fraction) return inlineAdvance;
  qreal right = 0.0;
  for (const QRectF& box : {fraction->box.numerator,
                            fraction->box.denominator,
                            fraction->box.rule,
                            fraction->box.leftDelimiter,
                            fraction->box.rightDelimiter})
    if (!box.isEmpty()) right = std::max(right, box.right());
  return right > 0.0 ? std::min(borderBoxWidth, right) : borderBoxWidth;
}

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
                                           qreal renderFontPixelSize,
                                           qreal outputScale) {
  const muffin::math::MathCssBox box = muffin::math::layoutMathMlCssBox(
      layout, renderFontPixelSize, kMathMlCanonicalCssPixelSize);
  const auto operation = muffin::math::buildMathMlPaintOperations(
      layout, renderFontPixelSize, kMathMlCanonicalCssPixelSize);
  const qreal inkRight = operation.operation
      ? mathMlInlineInkRight(*operation.operation, box.width, box.advance)
      : box.advance;
  return {box.width * outputScale, box.advance * outputScale,
          inkRight * outputScale, box.height * outputScale,
          flowMathStructure(box.semanticKind),
          box.baseline * outputScale, box.inkTop * outputScale,
          box.inkBottom * outputScale};
}

MathMlInlineMetrics sequenceMathMlMetrics(
    const FlowLabelPreparedMath& prepared) {
  const auto& box = prepared.box;
  return {box.width, box.advance,
          mathMlInlineInkRight(prepared.operation, box.width, box.advance),
          box.height,
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
  return {box.width, box.width, box.width, box.height,
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
  qreal inkLeft = 0.0;
  qreal inkRight = 0.0;
  QVector<FlowLabelVisualRun> runs;
};

quint16 readBigEndianU16(const QByteArray& bytes, qsizetype offset) {
  if (offset < 0 || offset + 2 > bytes.size()) return 0;
  return (quint16(quint8(bytes.at(offset))) << 8) |
         quint16(quint8(bytes.at(offset + 1)));
}

qint16 readBigEndianI16(const QByteArray& bytes, qsizetype offset) {
  return static_cast<qint16>(readBigEndianU16(bytes, offset));
}

FlowLabelFontMetrics openTypeFontBoundingMetrics(const QRawFont& raw,
                                                 qreal fontPixelSize) {
  if (!raw.isValid() || raw.unitsPerEm() <= 0.0) return {};
  const QByteArray hhea = raw.fontTable("hhea");
  if (hhea.size() < 8) return {};
  const qreal scale = fontPixelSize / raw.unitsPerEm();
  qreal xHeight = 0.0;
  const QByteArray os2 = raw.fontTable("OS/2");
  if (readBigEndianU16(os2, 0) >= 2 && os2.size() >= 88) {
    const qreal scaledXHeight =
        std::max<qreal>(0.0, readBigEndianI16(os2, 86) * scale);
    // DirectWrite exposes SVG baseline coordinates on its 1/64 px grid.
    xHeight = std::floor(scaledXHeight * 64.0) / 64.0;
  }
  return {std::round(readBigEndianI16(hhea, 4) * scale),
          std::round(-readBigEndianI16(hhea, 6) * scale), xHeight};
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

ShapedTextMetrics shapeTextRangePass(const FlowLabelDocument& label,
                                     qsizetype start, qsizetype length,
                                     const QFont& font, bool styledPaintFace) {
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
      if (!styledPaintFace &&
          font.family().contains(QStringLiteral("Noto Sans"),
                                 Qt::CaseInsensitive)) {
        // Chromium synthesizes weight and slant from the registered Regular
        // face without changing its horizontal advance table.
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
      QList<quint32> preparedGlyphIndexes;
      QList<QPointF> preparedGlyphPositions;
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
        preparedGlyphIndexes.push_back(glyphs.at(glyphIndex));
        preparedGlyphPositions.push_back(positions.at(glyphIndex));
      }
      const qreal preparedGlyphWidth = std::max<qreal>(
          0.0, groupRight - groupLeft);
      if (!run.isRightToLeft())
        groupRight += groupTableAdvance - groupQtAdvance;
      FlowLabelVisualRun visual;
      visual.start = start + logicalMinimum;
      visual.length = logicalMaximum - logicalMinimum + 1;
      visual.x = groupLeft;
      visual.width = std::max<qreal>(0.0, groupRight - groupLeft);
      visual.rightToLeft = logicalStep < 0 ||
          (logicalStep == 0 && run.isRightToLeft());
      visual.fontWeight = run.rawFont().weight();
      visual.fontItalic = run.rawFont().style() != QFont::StyleNormal;
      visual.fontFamily = run.rawFont().familyName();
      const FlowLabelFontMetrics vertical = openTypeFontBoundingMetrics(
          run.rawFont(), font.pixelSize());
      visual.fontAscent = vertical.ascent;
      visual.fontDescent = vertical.descent;
      for (QPointF& position : preparedGlyphPositions)
        position.rx() -= groupLeft;
      visual.preparedGlyphs.setRawFont(run.rawFont());
      visual.preparedGlyphs.setGlyphIndexes(preparedGlyphIndexes);
      visual.preparedGlyphs.setPositions(preparedGlyphPositions);
      visual.preparedGlyphWidth = preparedGlyphWidth;
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
  if (inkLeft != std::numeric_limits<qreal>::max()) {
    result.inkWidth = std::max<qreal>(0.0, inkRight - inkLeft);
    result.inkLeft = inkLeft - left;
    result.inkRight = inkRight - left;
  }
  std::sort(result.runs.begin(), result.runs.end(),
            [](const FlowLabelVisualRun& a, const FlowLabelVisualRun& b) {
              return a.x < b.x;
            });
  return result;
}

ShapedTextMetrics shapeTextRange(const FlowLabelDocument& label,
                                 qsizetype start, qsizetype length,
                                 const QFont& font) {
  ShapedTextMetrics painted = shapeTextRangePass(
      label, start, length, font, true);
  const bool hasSyntheticStyle = std::any_of(
      label.formats.cbegin(), label.formats.cend(),
      [start, length](const QTextLayout::FormatRange& range) {
        const bool overlaps = range.start < start + length &&
                              range.start + range.length > start;
        return overlaps &&
            (range.format.fontWeight() > QFont::Normal ||
             range.format.fontItalic());
      });
  if (!hasSyntheticStyle ||
      !font.family().contains(QStringLiteral("Noto Sans"),
                              Qt::CaseInsensitive))
    return painted;

  const ShapedTextMetrics geometry = shapeTextRangePass(
      label, start, length, font, false);
  painted.width = geometry.width;
  for (FlowLabelVisualRun& paintRun : painted.runs) {
    const auto metricRun = std::find_if(
        geometry.runs.cbegin(), geometry.runs.cend(),
        [&paintRun](const FlowLabelVisualRun& candidate) {
          return candidate.start == paintRun.start &&
                 candidate.length == paintRun.length;
        });
    if (metricRun == geometry.runs.cend()) continue;
    paintRun.x = metricRun->x;
    paintRun.width = metricRun->width;
  }
  return painted;
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

FlowLabelDocument parseMarkup(QString source, bool markdown, bool mathEnabled,
                              bool htmlFormatting = true) {
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
    if (markdown && rest.size() >= 2 && rest.at(0) == QLatin1Char('\\') &&
        QStringView(uR"(!\"#$%&'()*+,-./:;<=>?@[\]^_`{|}~)").contains(rest.at(1))) {
      plain += rest.at(1);
      i += 2;
      continue;
    }
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
    } else if (htmlFormatting &&
               (htmlToken(QStringLiteral("strong"), false) ||
                htmlToken(QStringLiteral("b"), false))) {
      format.setFontWeight(QFont::Bold); blockToken = true;
    } else if (htmlFormatting &&
               ((closing = htmlToken(QStringLiteral("strong"), true)) ||
                (closing = htmlToken(QStringLiteral("b"), true)))) {
      blockToken = true;
    } else if (htmlFormatting &&
               (htmlToken(QStringLiteral("em"), false) ||
                htmlToken(QStringLiteral("i"), false))) {
      format.setFontItalic(true); blockToken = true;
    } else if (htmlFormatting &&
               ((closing = htmlToken(QStringLiteral("em"), true)) ||
                (closing = htmlToken(QStringLiteral("i"), true)))) {
      blockToken = true;
    } else if (htmlFormatting && htmlToken(QStringLiteral("code"), false)) {
      format.setFontFamilies({QStringLiteral("monospace")}); blockToken = true;
    } else if (htmlFormatting &&
               (closing = htmlToken(QStringLiteral("code"), true))) {
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
      result.formattingContext =
          FlowLabelFormattingContext::FlowForeignObjectFlex;
      if (source.contains(
              QRegularExpression(QStringLiteral("<br\\s*/?>"),
                                 QRegularExpression::CaseInsensitiveOption)))
        result.breakBehavior =
            FlowLabelBreakBehavior::CollapseIntoMathFlexLine;
      return result;
    }
    return parseMarkup(source, true, mathEnabled);
  }
  if (source.contains(QLatin1Char('<')) ||
      (mathEnabled && source.contains(QStringLiteral("$$")))) {
    FlowLabelDocument result = parseMarkup(source, false, mathEnabled);
    if (!result.math.isEmpty())
      result.formattingContext =
          FlowLabelFormattingContext::FlowForeignObjectFlex;
    return result;
  }
  return {.text = normalizeBreaks(source)};
}

FlowLabelDocument parseFlowSvgLabel(const QString& source,
                                    const QString& labelType) {
  QString formatted = normalizeBreaks(source);
  static const QRegularExpression openingTag(
      QStringLiteral(R"((<(?:strong|b|em|i|code)>))"),
      QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression closingTag(
      QStringLiteral(R"((</(?:strong|b|em|i|code)>))"),
      QRegularExpression::CaseInsensitiveOption);
  formatted.replace(openingTag, QStringLiteral("\\1 "));
  formatted.replace(closingTag, QStringLiteral(" \\1"));
  formatted.replace(QRegularExpression(QStringLiteral("[ \\t]+")),
                    QStringLiteral(" "));
  const bool markdown = labelType == QLatin1String("markdown");
  FlowLabelDocument result =
      parseMarkup(std::move(formatted), markdown, false, false);
  result.formattingContext =
      FlowLabelFormattingContext::FlowSvgFormattedText;
  return result;
}

qsizetype prepareFlowLabelMath(FlowLabelDocument& label,
                               qreal fontPixelSize) {
  if (label.math.isEmpty()) return 0;
  const qreal renderFontPixelSize = kMathMlCanonicalCssPixelSize * 1.21;
  const qreal operationScale = fontPixelSize / kMathMlCanonicalCssPixelSize;
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
        layout, renderFontPixelSize, kMathMlCanonicalCssPixelSize);
    if (!build.succeeded())
      throw muffin::math::MathMlPaintError(std::move(*build.failure));
    auto prepared = std::make_shared<FlowLabelPreparedMath>();
    prepared->fontPixelSize = fontPixelSize;
    prepared->operationScale = operationScale;
    prepared->box = muffin::math::layoutMathMlCssBox(
        layout, renderFontPixelSize, kMathMlCanonicalCssPixelSize);
    scaleMathCssBox(prepared->box, operationScale);
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
  const bool useDesignMetrics =
      label.formattingContext !=
      FlowLabelFormattingContext::FlowForeignObjectFlex;
  option.setUseDesignMetrics(useDesignMetrics);
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

namespace {

// The single QFont every label measurement/paint path shares: baseWeight +
// baseStyle + letter/word spacing all flow through makeFlowLabelFont, so the
// whole chain (ink/advance, wrap, layout, bounding metrics, paint) agrees with
// the drawn font. Neutral document defaults keep default rendering byte-ident.
QFont flowLabelDocumentFont(const FlowLabelDocument& label,
                            const QString& fontFamily, qreal fontPixelSize) {
  return makeFlowLabelFont(fontFamily, fontPixelSize, label.baseWeight,
                           label.baseStyle, label.letterSpacingPx,
                           label.wordSpacingPx);
}

}  // namespace

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
  QFont font = flowLabelDocumentFont(label, fontFamily, fontPixelSize);
  return shapeTextRange(label, start, length, font).inkWidth;
}

qreal measureFlowTextAdvanceWidth(const FlowLabelDocument& label,
                                  qsizetype start, qsizetype length,
                                  const QString& fontFamily,
                                  qreal fontPixelSize) {
  QFont font = flowLabelDocumentFont(label, fontFamily, fontPixelSize);
  return shapeTextRange(label, start, length, font).width;
}

namespace {

struct FlowHarfBuzzRawFont {
  QRawFont raw;
};

void destroyFlowHarfBuzzTable(void* data) {
  delete static_cast<QByteArray*>(data);
}

hb_blob_t* referenceFlowHarfBuzzTable(hb_face_t*, hb_tag_t tag,
                                      void* userData) {
  const auto* source = static_cast<const FlowHarfBuzzRawFont*>(userData);
  const char bytes[] = {char((tag >> 24) & 0xff),
                        char((tag >> 16) & 0xff),
                        char((tag >> 8) & 0xff), char(tag & 0xff)};
  auto* table = new QByteArray(source->raw.fontTable(QByteArray(bytes, 4)));
  if (table->isEmpty()) {
    delete table;
    return hb_blob_reference(hb_blob_get_empty());
  }
  return hb_blob_create(table->constData(), unsigned(table->size()),
                        HB_MEMORY_MODE_READONLY, table,
                        destroyFlowHarfBuzzTable);
}

void destroyFlowHarfBuzzFont(void* data) {
  delete static_cast<FlowHarfBuzzRawFont*>(data);
}

bool flowRawFontSupportsRange(const QRawFont& raw, const QString& text,
                              qsizetype start, qsizetype length) {
  const qsizetype end = start + length;
  for (qsizetype i = start; i < end; ++i) {
    uint codepoint = text.at(i).unicode();
    if (QChar::isHighSurrogate(codepoint) && i + 1 < end &&
        QChar::isLowSurrogate(text.at(i + 1).unicode())) {
      codepoint = QChar::surrogateToUcs4(text.at(i), text.at(++i));
    }
    if (!raw.supportsCharacter(codepoint)) return false;
  }
  return true;
}

}  // namespace

std::optional<qreal> measureOpenTypeDesignAdvance(
    const FlowLabelDocument& label, qsizetype start, qsizetype length,
    const QString& fontFamily, qreal fontPixelSize) {
  if (length <= 0) return 0.0;
  constexpr qreal kReferenceSize = 16.0;
  QFont font = flowLabelDocumentFont(label, fontFamily, kReferenceSize);
  QRawFont raw = QRawFont::fromFont(font);
  if (raw.familyName().contains(QStringLiteral("Noto Sans"),
                                Qt::CaseInsensitive) &&
      (font.weight() != QFont::Normal || font.italic())) {
    // Mermaid's browser fixture registers only Regular. Chromium synthesizes
    // weight/slant without changing that face's horizontal advance table.
    font.setWeight(QFont::Normal);
    font.setItalic(false);
    raw = QRawFont::fromFont(font);
  }
  if (!raw.isValid() ||
      !flowRawFontSupportsRange(raw, label.text, start, length)) {
    return std::nullopt;
  }

  auto* source = new FlowHarfBuzzRawFont{raw};
  hb_face_t* face = hb_face_create_for_tables(
      referenceFlowHarfBuzzTable, source, destroyFlowHarfBuzzFont);
  const unsigned upem = hb_face_get_upem(face);
  if (upem == 0) {
    hb_face_destroy(face);
    return std::nullopt;
  }
  hb_font_t* hbFont = hb_font_create(face);
  hb_ot_font_set_funcs(hbFont);
  hb_font_set_scale(hbFont, int(upem), int(upem));
  hb_buffer_t* buffer = hb_buffer_create();
  const auto* utf16 = reinterpret_cast<const uint16_t*>(label.text.utf16());
  hb_buffer_add_utf16(buffer, utf16, label.text.size(), unsigned(start),
                      int(length));
  hb_buffer_set_direction(buffer, label.direction == Qt::RightToLeft
                                      ? HB_DIRECTION_RTL
                                      : HB_DIRECTION_LTR);
  hb_buffer_guess_segment_properties(buffer);
  hb_shape(hbFont, buffer, nullptr, 0);

  unsigned glyphCount = 0;
  const hb_glyph_position_t* positions =
      hb_buffer_get_glyph_positions(buffer, &glyphCount);
  qint64 designAdvance = 0;
  for (unsigned i = 0; i < glyphCount; ++i)
    designAdvance += positions[i].x_advance;
  hb_buffer_destroy(buffer);
  hb_font_destroy(hbFont);
  hb_face_destroy(face);

  qreal result = std::abs(qreal(designAdvance)) * fontPixelSize / qreal(upem);
  const QString segment = label.text.mid(start, length);
  if (label.letterSpacingPx != 0.0)
    result += label.letterSpacingPx * segment.toUcs4().size();
  if (label.wordSpacingPx != 0.0)
    result += label.wordSpacingPx * segment.count(QLatin1Char(' '));
  return result;
}

std::optional<qreal> measureOpenTypeDesignAdvance(
    const FlowLabelDocument& label, const QString& fontFamily,
    qreal fontPixelSize) {
  return measureOpenTypeDesignAdvance(label, 0, label.text.size(), fontFamily,
                                      fontPixelSize);
}

FlowLabelFontMetrics flowLabelFontBoundingMetrics(
    const QString& fontFamily, qreal fontPixelSize, QFont::Weight weight,
    QFont::Style style) {
  QFont font = makeFlowLabelFont(fontFamily, fontPixelSize, weight, style);
  const QRawFont raw = QRawFont::fromFont(font);
  const FlowLabelFontMetrics metrics =
      openTypeFontBoundingMetrics(raw, fontPixelSize);
  if (metrics.height() > 0.0) return metrics;
  const QFontMetricsF fallback(font);
  return {std::round(fallback.ascent()), std::round(fallback.descent()),
          fallback.xHeight()};
}

qreal flowSvgFormattedTextLineStep(qreal fontPixelSize) {
  return fontPixelSize * 1.1;
}

qreal flowSvgFormattedTextBlockHeight(const QString& fontFamily,
                                      qreal fontPixelSize,
                                      qsizetype lineCount, qreal padding) {
  if (lineCount <= 0) return 0.0;
  const FlowLabelFontMetrics font =
      flowLabelFontBoundingMetrics(fontFamily, fontPixelSize);
  return font.height() + (lineCount - 1) *
             flowSvgFormattedTextLineStep(fontPixelSize) + 2.0 * padding;
}

namespace {

QVector<FlowLabelLineRange> flowLabelLineRanges(
    const FlowLabelDocument& label) {
  if (!label.visualLines.isEmpty()) return label.visualLines;
  QVector<FlowLabelLineRange> ranges;
  qsizetype start = 0;
  while (true) {
    const qsizetype newline = label.text.indexOf(QLatin1Char('\n'), start);
    if (newline < 0) {
      ranges.push_back({start, label.text.size() - start});
      break;
    }
    ranges.push_back({start, newline - start});
    start = newline + 1;
  }
  return ranges;
}

}  // namespace

QRectF measureFlowSvgTextBounds(const FlowLabelDocument& label,
                                const QString& fontFamily,
                                qreal fontPixelSize) {
  QFont font = flowLabelDocumentFont(label, fontFamily, fontPixelSize);

  const QVector<FlowLabelLineRange> lines = flowLabelLineRanges(label);
  qreal left = std::numeric_limits<qreal>::max();
  qreal right = std::numeric_limits<qreal>::lowest();
  for (const FlowLabelLineRange& line : lines) {
    const ShapedTextMetrics shaped = shapeTextRange(
        label, line.start, line.length, font);
    const bool syntheticBold = std::any_of(
        label.formats.cbegin(), label.formats.cend(),
        [&](const QTextLayout::FormatRange& range) {
          return range.start < line.start + line.length &&
              range.start + range.length > line.start &&
              range.format.fontWeight() > QFont::Normal;
        });
    const bool syntheticItalic = std::any_of(
        label.formats.cbegin(), label.formats.cend(),
        [&](const QTextLayout::FormatRange& range) {
          return range.start < line.start + line.length &&
              range.start + range.length > line.start &&
              range.format.fontItalic();
        });
    const bool cjkOutline = std::any_of(
        shaped.runs.cbegin(), shaped.runs.cend(),
        [](const FlowLabelVisualRun& run) {
          return run.fontFamily.contains(QStringLiteral("CJK"),
                                         Qt::CaseInsensitive);
        });
    const bool hasFallbackRun = std::any_of(
        shaped.runs.cbegin(), shaped.runs.cend(),
        [](const FlowLabelVisualRun& run) {
          return run.fontFamily.compare(QStringLiteral("Noto Sans"),
                                        Qt::CaseInsensitive) != 0;
        });
    // Skia emboldens a registered Regular webfont by one CSS pixel at 16px.
    // Qt's synthetic face has a different outline overhang, while both retain
    // the same OpenType advance. Model Chromium's synthetic stroke directly.
    const qreal boldOverhang = syntheticBold ? fontPixelSize / 16.0 : 0.0;
    const ShapedTextMetrics regularOutline = syntheticBold
        ? shapeTextRangePass(label, line.start, line.length, font, false)
        : ShapedTextMetrics{};
    const qreal lineLeft = syntheticBold ? -boldOverhang
        : syntheticItalic ? 0.0
        : shaped.inkWidth > 0.0 ? std::min<qreal>(0.0, shaped.inkLeft) : 0.0;
    const qreal lineRight = syntheticBold
        ? (hasFallbackRun
               ? shaped.width + boldOverhang
               : std::max(regularOutline.width,
                          regularOutline.inkLeft + regularOutline.inkRight +
                              boldOverhang))
        : syntheticItalic ? shaped.width
        : shaped.inkWidth > 0.0 ? std::max(shaped.width, shaped.inkRight)
                                : shaped.width;
    const qreal chromiumRight = lineRight +
        (!syntheticBold && !syntheticItalic && cjkOutline
             ? fontPixelSize / 16.0 : 0.0);
    left = std::min(left, lineLeft);
    right = std::max(right, chromiumRight);
  }
  if (left == std::numeric_limits<qreal>::max()) left = right = 0.0;

  const FlowLabelFontMetrics vertical =
      flowLabelFontBoundingMetrics(fontFamily, fontPixelSize, label.baseWeight, label.baseStyle);
  const qsizetype lineCount = std::max<qsizetype>(1, lines.size());
  const qreal top = fontPixelSize - vertical.ascent;
  const qreal height = vertical.height() +
      (lineCount - 1) * flowSvgFormattedTextLineStep(fontPixelSize);
  return {left, top, std::max<qreal>(0.0, right - left), height};
}

FlowLabelDocument wrapFlowLabel(const FlowLabelDocument& label,
                                const QString& fontFamily,
                                qreal fontPixelSize,
                                qreal maximumLineWidth) {
  FlowLabelDocument wrapped = label;
  wrapped.visualLines.clear();
  wrapped.visualLineAdvance = 0.0;
  if (maximumLineWidth <= 0.0 || label.text.contains(QLatin1Char('\n')))
    return wrapped;

  QFont font = flowLabelDocumentFont(label, fontFamily, fontPixelSize);
  QTextLayout layout(label.text, font);
  QTextOption option;
  option.setUseDesignMetrics(true);
  option.setTextDirection(label.direction);
  option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  layout.setTextOption(option);
  layout.setFormats(label.formats);
  layout.beginLayout();
  while (true) {
    QTextLine line = layout.createLine();
    if (!line.isValid()) break;
    line.setLineWidth(maximumLineWidth);
    qsizetype length = line.textLength();
    while (length > 0 &&
           label.text.at(line.textStart() + length - 1).isSpace())
      --length;
    wrapped.visualLines.push_back({line.textStart(), length});
  }
  layout.endLayout();
  if (wrapped.visualLines.size() <= 1) wrapped.visualLines.clear();
  return wrapped;
}

FlowLabelLayoutMetrics layoutFlowLabel(const FlowLabelDocument& label,
                                       const QString& fontFamily,
                                       qreal fontPixelSize, qreal lineHeight) {
  QFont font = flowLabelDocumentFont(label, fontFamily, fontPixelSize);
  const QFontMetricsF metrics(font);
  const QRectF inkMetrics = metrics.tightBoundingRect(QStringLiteral("Mg"));
  // Canvas TextMetrics reports the pixel-aligned ink box used by Chromium's
  // foreignObject labels. Qt exposes the fractional outline box; align its top
  // outward and bottom inward to reproduce actualBoundingBoxAscent/Descent.
  const qreal cssAscent = std::ceil(std::max<qreal>(0.0, -inkMetrics.top()));
  const qreal cssDescent = std::floor(std::max<qreal>(0.0, inkMetrics.bottom()));
  const FlowLabelFontMetrics fontBoundingMetrics =
      flowLabelFontBoundingMetrics(fontFamily, fontPixelSize, label.baseWeight, label.baseStyle);
  const bool flowForeignObject =
      label.formattingContext ==
      FlowLabelFormattingContext::FlowForeignObjectFlex;
  const qreal lineAscent = flowForeignObject
      ? fontBoundingMetrics.ascent : cssAscent;
  const qreal lineDescent = flowForeignObject
      ? fontBoundingMetrics.descent : cssDescent;
  FlowLabelLayoutMetrics result;
  const bool sequenceMathMl =
      label.formattingContext ==
      FlowLabelFormattingContext::SequenceForeignObjectFlex;
  const bool collapsedMathFlexLine =
      label.breakBehavior ==
      FlowLabelBreakBehavior::CollapseIntoMathFlexLine;
  const QVector<FlowLabelLineRange> lines = flowLabelLineRanges(label);
  for (const FlowLabelLineRange& lineRange : lines) {
    const qsizetype offset = lineRange.start;
    const QString line = label.text.mid(offset, lineRange.length);
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
      const bool mathOnlyLine = !sequenceMathMl &&
          !collapsedMathFlexLine && allMathPrepared &&
          onlyMathPlaceholders;
      qreal actualLineHeight = mathOnlyLine ? 0.0 : lineHeight;
      qsizetype cursor = offset;
      muffin::math::MathRenderer renderer;
      auto shapeMathTextRange = [&](qsizetype start, qsizetype length) {
        return shapeDomTextRange(label, start, length, font);
      };
      qreal inlineInkRight = 0.0;
      qreal borderRight = 0.0;
      for (const FlowLabelMathSpan& math : mathSpans) {
        const auto shapedText = shapeMathTextRange(
            cursor, math.start - cursor);
        for (auto run : shapedText.runs) {
          run.x += lineWidth;
          measured.runs.push_back(std::move(run));
        }
        lineWidth += shapedText.width;
        if (shapedText.width > 0.0)
          inlineInkRight = std::max(inlineInkRight, lineWidth);
        if (math.prepared &&
            qFuzzyCompare(math.prepared->fontPixelSize, fontPixelSize)) {
          const MathMlInlineMetrics mathMetrics = sequenceMathMl
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
          inlineInkRight = std::max(
              inlineInkRight, lineWidth + mathMetrics.inlineInkRight);
          borderRight = std::max(
              borderRight, lineWidth + mathMetrics.visualWidth);
          lineWidth += mathMetrics.advance;
          actualLineHeight = std::max(actualLineHeight, mathMetrics.height);
          cursor = math.start + math.length;
          continue;
        }
        const qreal operationScale =
            fontPixelSize / kMathMlCanonicalCssPixelSize;
        const qreal renderFontPixelSize =
            kMathMlCanonicalCssPixelSize * 1.21;
        const muffin::math::MathLayoutResult layout = renderer.render(
            math.source, renderFontPixelSize, Qt::black, true);
        if (layout.valid()) {
          if (sequenceMathMl) {
            auto paintBuild = muffin::math::buildMathMlPaintOperations(
                layout, renderFontPixelSize,
                kMathMlCanonicalCssPixelSize);
            if (!paintBuild.succeeded())
              throw muffin::math::MathMlPaintError(
                  std::move(*paintBuild.failure));
          }
          const MathMlInlineMetrics mathMetrics = sequenceMathMl
              ? sequenceMathMlMetrics(
                    layout, renderFontPixelSize, operationScale)
              : [&] {
                  muffin::math::MathCssBox box =
                      muffin::math::layoutMathMlCssBox(
                          layout, renderFontPixelSize,
                          kMathMlCanonicalCssPixelSize);
                  scaleMathCssBox(box, operationScale);
                  return MathMlInlineMetrics{
                      box.width, box.width, box.width, box.height,
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
          inlineInkRight = std::max(
              inlineInkRight, lineWidth + mathMetrics.inlineInkRight);
          borderRight = std::max(
              borderRight, lineWidth + mathMetrics.visualWidth);
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
      if (shapedTail.width > 0.0)
        inlineInkRight = std::max(inlineInkRight, lineWidth);
      borderRight = std::max(borderRight, lineWidth);
      if (sequenceMathMl)
        for (const FlowLabelVisualRun& run : measured.runs)
          if (!run.math)
            actualLineHeight = std::max(
                actualLineHeight, run.fontAscent + run.fontDescent);
      measured.width = sequenceMathMl ? inlineInkRight : lineWidth;
      measured.height = collapsedMathFlexLine
          ? (sequenceMathMl ? fontBoundingMetrics.ascent
                            : fontBoundingMetrics.height())
          : actualLineHeight;
      measured.blockHeight = actualLineHeight;
      measured.ascent = lineAscent;
      measured.descent = lineDescent;
      measured.baseline =
          (actualLineHeight - lineAscent - lineDescent) / 2.0 + lineAscent;
      result.size.setWidth(std::max(result.size.width(), sequenceMathMl
          ? std::round(borderRight) : lineWidth));
      result.size.setHeight(result.size.height() + actualLineHeight);
      result.lines.push_back(std::move(measured));
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
    measured.ascent = lineAscent;
    measured.descent = lineDescent;
    measured.baseline =
        (lineHeight - lineAscent - lineDescent) / 2.0 + lineAscent;
    measured.runs = shaped.runs;
    result.size.setWidth(std::max(result.size.width(), measured.width));
    result.size.setHeight(result.size.height() + lineHeight);
    result.lines.push_back(std::move(measured));
  }
  qreal visualLineAdvance = label.visualLineAdvance;
  if (label.formattingContext ==
          FlowLabelFormattingContext::FlowSvgFormattedText &&
      result.lines.size() > 1)
    visualLineAdvance = flowSvgFormattedTextLineStep(fontPixelSize);
  if (visualLineAdvance > 0.0 && result.lines.size() > 1) {
    for (FlowLabelLineMetrics& line : result.lines) {
      line.blockHeight = visualLineAdvance;
      line.baseline = fontBoundingMetrics.ascent;
      line.ascent = fontBoundingMetrics.ascent;
      line.descent = fontBoundingMetrics.descent;
    }
    result.size.setHeight(fontBoundingMetrics.height() +
                          (result.lines.size() - 1) *
                              visualLineAdvance);
  }
  return result;
}

void paintFlowLabel(QPainter& painter, const FlowLabelDocument& label,
                    const QRectF& rect, const QString& fontFamily,
                    qreal fontPixelSize, qreal lineHeight,
                    const QColor& color, bool centerVertically,
                    FlowLabelAlign align, qreal alignMargin) {
  QFont font = flowLabelDocumentFont(label, fontFamily, fontPixelSize);
  FlowLabelDocument paintedLabel = label;
  prepareFlowLabelMath(paintedLabel, fontPixelSize);
  const FlowLabelLayoutMetrics layoutMetrics =
      layoutFlowLabel(paintedLabel, fontFamily, fontPixelSize, lineHeight);
  const QSizeF measured = layoutMetrics.size;
  const QFontMetricsF fontMetrics(font);
  const qreal fallbackAscent = fontMetrics.ascent();
  qreal lineTop = centerVertically
                      ? rect.top() + std::max<qreal>(0.0, (rect.height() - measured.height()) / 2.0)
                      : rect.top();
  painter.setPen(color);
  const QVector<FlowLabelLineRange> lines = flowLabelLineRanges(paintedLabel);
  qsizetype lineIndex = 0;
  for (const FlowLabelLineRange& lineRange : lines) {
    const qsizetype offset = lineRange.start;
    const QString line = paintedLabel.text.mid(offset, lineRange.length);
    const FlowLabelLineMetrics& measuredLine = layoutMetrics.lines.at(lineIndex++);
    QVector<FlowLabelMathSpan> mathSpans;
    for (const FlowLabelMathSpan& math : paintedLabel.math)
      if (math.start >= offset && math.start < offset + line.size()) mathSpans.push_back(math);

    const qreal lineWidth = measuredLine.width;
    // Align mirrors mermaid drawText() anchor semantics within the label rect,
    // with textMargin inset on the aligned edge: start -> text left edge at
    // rect.left + margin, middle -> centered, end -> text right edge at
    // rect.right - margin. Align only moves the line origin, so wrapping and
    // per-line metrics are unaffected.
    qreal x = rect.left() + (rect.width() - lineWidth) / 2.0;
    if (align == FlowLabelAlign::Left)
      x = rect.left() + alignMargin;
    else if (align == FlowLabelAlign::Right)
      x = rect.right() - alignMargin - lineWidth;
    const qreal lineOrigin = x;
    const bool mathFlexLine = std::any_of(
        measuredLine.runs.cbegin(), measuredLine.runs.cend(),
        [](const FlowLabelVisualRun& candidate) { return candidate.math; });
    auto drawVisualTextRun = [&](const FlowLabelVisualRun& run) {
      if (!run.preparedGlyphs.isEmpty()) {
        const bool fallbackFont = !run.fontFamily.isEmpty() &&
            run.fontFamily.compare(font.family(), Qt::CaseInsensitive) != 0;
        qreal glyphOriginAscent = fallbackAscent;
        if (fallbackFont && run.fontAscent > 0.0) {
          // Chromium honors an explicit OpenType BASE table (notably for CJK
          // alphabetic/ideographic alignment). In a Math flex line, faces
          // without BASE inherit the CSS inline box baseline and cannot raise
          // their top-origin past it; ordinary text lines retain face ascent.
          const bool hasBaselineTable =
              !run.preparedGlyphs.rawFont().fontTable("BASE").isEmpty();
          glyphOriginAscent = !mathFlexLine || hasBaselineTable
              ? run.fontAscent
              : std::min(run.fontAscent, measuredLine.baseline);
        }
        painter.save();
        painter.translate(lineOrigin + run.x,
                          lineTop + measuredLine.baseline - glyphOriginAscent);
        if (run.preparedGlyphWidth > 0.0)
          painter.scale(run.width / run.preparedGlyphWidth, 1.0);
        if (run.rightToLeft) {
          QPainterPath outline;
          const QRawFont rawFont = run.preparedGlyphs.rawFont();
          const QList<quint32> glyphs = run.preparedGlyphs.glyphIndexes();
          const QList<QPointF> positions = run.preparedGlyphs.positions();
          for (qsizetype glyphIndex = 0;
               glyphIndex < glyphs.size() && glyphIndex < positions.size();
               ++glyphIndex) {
            QTransform transform;
            transform.translate(positions.at(glyphIndex).x(),
                                positions.at(glyphIndex).y());
            outline.addPath(transform.map(rawFont.pathForGlyph(
                glyphs.at(glyphIndex))));
          }
          painter.fillPath(outline, color);
        } else {
          painter.drawGlyphRun(QPointF(), run.preparedGlyphs);
        }
        painter.restore();
      }
    };
    if (mathSpans.isEmpty()) {
      for (const FlowLabelVisualRun& run : measuredLine.runs)
        if (!run.math)
          drawVisualTextRun(run);
    } else {
      qsizetype runIndex = 0;
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
          if (paintedLabel.formattingContext ==
              FlowLabelFormattingContext::SequenceForeignObjectFlex) {
            painter.translate(
                mathX, lineTop + measuredLine.baseline - prepared.box.baseline);
          } else {
            painter.translate(
                mathX, lineTop + (measuredLine.blockHeight - prepared.box.height) / 2.0);
          }
          painter.scale(prepared.operationScale, prepared.operationScale);
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
    if (label.underline || label.strikeOut || label.overline) {
      // CSS text-decoration: drawn explicitly (drawGlyphRun does not reliably
      // paint it) over each visual line, spanning the whole inline line width
      // (text + inline Math). It uses the current text color and the font's
      // underline/overline/strikeout metrics + line thickness, so it never
      // changes layout/measure size and never touches the Math paint. Default
      // false skips this block entirely (default rendering byte-identical).
      const qreal thickness = fontMetrics.lineWidth();
      const qreal baselineY = lineTop + measuredLine.baseline;
      if (label.underline) {
        const qreal y = baselineY + fontMetrics.underlinePos();
        painter.fillRect(QRectF(lineOrigin, y - thickness / 2.0, lineWidth, thickness), color);
      }
      if (label.overline) {
        const qreal y = baselineY - fontMetrics.overlinePos();
        painter.fillRect(QRectF(lineOrigin, y - thickness / 2.0, lineWidth, thickness), color);
      }
      if (label.strikeOut) {
        const qreal y = baselineY - fontMetrics.strikeOutPos();
        painter.fillRect(QRectF(lineOrigin, y - thickness / 2.0, lineWidth, thickness), color);
      }
    }
    lineTop += measuredLine.blockHeight;
  }
}

}  // namespace muffin::mermaid::flowchart
