#include "math/OpenTypeMathFont.h"

#include <QFile>
#include <QFontDatabase>
#include <QGlyphRun>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QVector>
#include <QTextLayout>

#include <algorithm>
#include <limits>
#include <tuple>

static void initKatexFontsResource() {
  Q_INIT_RESOURCE(katex_fonts);
}

namespace muffin::math {
namespace {

constexpr qreal kCssMathPixelSize = 16.0;
constexpr qreal kOutlineLoadPixelSize = 1000.0;

quint16 u16(const QByteArray& data, qsizetype offset) {
  if (offset < 0 || offset + 2 > data.size()) return 0;
  const auto* p = reinterpret_cast<const uchar*>(data.constData() + offset);
  return (quint16(p[0]) << 8) | quint16(p[1]);
}

qint16 s16(const QByteArray& data, qsizetype offset) {
  return static_cast<qint16>(u16(data, offset));
}

QVector<quint16> coverageGlyphs(const QByteArray& data, qsizetype offset) {
  QVector<quint16> glyphs;
  if (offset <= 0 || offset + 4 > data.size()) return glyphs;
  const quint16 format = u16(data, offset);
  const quint16 count = u16(data, offset + 2);
  if (format == 1) {
    if (offset + 4 + qsizetype(count) * 2 > data.size()) return {};
    glyphs.reserve(count);
    for (quint16 i = 0; i < count; ++i) glyphs.push_back(u16(data, offset + 4 + i * 2));
    return glyphs;
  }
  if (format != 2 || offset + 4 + qsizetype(count) * 6 > data.size()) return {};
  for (quint16 i = 0; i < count; ++i) {
    const qsizetype range = offset + 4 + i * 6;
    const quint16 first = u16(data, range);
    const quint16 last = u16(data, range + 2);
    const quint16 coverageIndex = u16(data, range + 4);
    if (last < first) return {};
    const qsizetype needed = qsizetype(coverageIndex) + last - first + 1;
    if (needed > glyphs.size()) glyphs.resize(needed);
    for (quint32 glyph = first; glyph <= last; ++glyph)
      glyphs[coverageIndex + glyph - first] = static_cast<quint16>(glyph);
  }
  return glyphs;
}

void parseValueMap(const QByteArray& data, qsizetype subtable,
                   QHash<quint32, qint16>* values) {
  if (subtable <= 0 || subtable + 4 > data.size()) return;
  const quint16 coverageOffset = u16(data, subtable);
  const quint16 count = u16(data, subtable + 2);
  const QVector<quint16> glyphs = coverageGlyphs(data, subtable + coverageOffset);
  if (glyphs.size() < count || subtable + 4 + qsizetype(count) * 4 > data.size()) return;
  for (quint16 i = 0; i < count; ++i)
    values->insert(glyphs[i], s16(data, subtable + 4 + i * 4));
}

}  // namespace

const OpenTypeMathFont& OpenTypeMathFont::instance() {
  static const OpenTypeMathFont font;
  return font;
}

OpenTypeMathFont::OpenTypeMathFont() {
  initKatexFontsResource();
  QFile file(QStringLiteral(":/katex/fonts/STIXTwoMath-Regular.otf"));
  if (!file.open(QIODevice::ReadOnly)) return;
  const QByteArray fontData = file.readAll();
  QFontDatabase::addApplicationFontFromData(fontData);
  // Loading at one pixel per design unit avoids platform hinting from rounding
  // outline bounds before they are converted to CSS pixels.
  font_.loadFromData(fontData, kOutlineLoadPixelSize, QFont::PreferNoHinting);
  // Chromium obtains assembly ink bounds from Skia's actual-size glyph strike.
  // Keep a separately hinted font for those bounds and for QGlyphRun painting.
  rasterFont_.loadFromData(fontData, kCssMathPixelSize,
                           QFont::PreferDefaultHinting);
  if (font_.isValid()) parseMathTable(font_.fontTable("MATH"));
}

bool OpenTypeMathFont::valid() const { return font_.isValid(); }

QString OpenTypeMathFont::familyName() const { return font_.familyName(); }

qreal OpenTypeMathFont::pixelSize() const { return kCssMathPixelSize; }

qreal OpenTypeMathFont::unitsPerEm() const { return font_.unitsPerEm(); }

const MathFontConstants& OpenTypeMathFont::constants() const { return constants_; }

qreal OpenTypeMathFont::designUnitsToPixels(qint16 value) const {
  return unitsPerEm() > 0.0 ? qreal(value) * pixelSize() / unitsPerEm() : 0.0;
}

void OpenTypeMathFont::parseMathTable(const QByteArray& table) {
  if (table.size() < 10 || u16(table, 0) != 1) return;
  const qsizetype constants = u16(table, 4);
  const qsizetype glyphInfo = u16(table, 6);
  const qsizetype variants = u16(table, 8);
  if (constants > 0 && constants + 214 <= table.size()) {
    constants_.scriptPercentScaleDown = s16(table, constants) / 100.0;
    constants_.scriptScriptPercentScaleDown = s16(table, constants + 2) / 100.0;
    constants_.displayOperatorMinHeight = unitsPerEm() > 0.0
        ? qreal(u16(table, constants + 6)) * pixelSize() / unitsPerEm() : 0.0;
    const auto value = [&](int index) {
      return designUnitsToPixels(s16(table, constants + 8 + index * 4));
    };
    constants_.axisHeight = value(1);
    constants_.subscriptShiftDown = value(4);
    constants_.subscriptTopMax = value(5);
    constants_.subscriptBaselineDropMin = value(6);
    constants_.superscriptShiftUp = value(7);
    constants_.superscriptShiftUpCramped = value(8);
    constants_.superscriptBottomMin = value(9);
    constants_.superscriptBaselineDropMax = value(10);
    constants_.subSuperscriptGapMin = value(11);
    constants_.superscriptBottomMaxWithSubscript = value(12);
    constants_.spaceAfterScript = value(13);
    constants_.upperLimitGapMin = value(14);
    constants_.upperLimitBaselineRiseMin = value(15);
    constants_.lowerLimitGapMin = value(16);
    constants_.lowerLimitBaselineDropMin = value(17);
    constants_.fractionNumeratorShiftUp = value(28);
    constants_.fractionNumeratorDisplayStyleShiftUp = value(29);
    constants_.fractionDenominatorShiftDown = value(30);
    constants_.fractionDenominatorDisplayStyleShiftDown = value(31);
    constants_.fractionNumeratorGapMin = value(32);
    constants_.fractionNumeratorDisplayStyleGapMin = value(33);
    constants_.fractionRuleThickness = value(34);
    constants_.fractionDenominatorGapMin = value(35);
    constants_.fractionDenominatorDisplayStyleGapMin = value(36);
    constants_.stackTopShiftUp = value(18);
    constants_.stackTopDisplayStyleShiftUp = value(19);
    constants_.stackBottomShiftDown = value(20);
    constants_.stackBottomDisplayStyleShiftDown = value(21);
    constants_.stackGapMin = value(22);
    constants_.stackDisplayStyleGapMin = value(23);
    constants_.overbarVerticalGap = value(39);
    constants_.overbarRuleThickness = value(40);
    constants_.overbarExtraAscender = value(41);
    constants_.underbarVerticalGap = value(42);
    constants_.underbarRuleThickness = value(43);
    constants_.underbarExtraDescender = value(44);
    constants_.radicalVerticalGap = value(45);
    constants_.radicalRuleThickness = value(47);
    constants_.radicalExtraAscender = value(48);
    constants_.radicalKernBeforeDegree = value(49);
    constants_.radicalKernAfterDegree = value(50);
    constants_.radicalDegreeBottomRaisePercent = s16(table, constants + 212) / 100.0;
  }
  if (glyphInfo <= 0 || glyphInfo + 8 > table.size()) return;
  const quint16 italics = u16(table, glyphInfo);
  const quint16 accents = u16(table, glyphInfo + 2);
  const quint16 extended = u16(table, glyphInfo + 4);
  if (italics) parseValueMap(table, glyphInfo + italics, &italicCorrections_);
  if (accents) parseValueMap(table, glyphInfo + accents, &accentAttachments_);
  if (extended) {
    for (quint16 glyph : coverageGlyphs(table, glyphInfo + extended))
      extendedShapes_.insert(glyph, true);
  }
  if (variants > 0 && variants + 10 <= table.size()) {
    minimumConnectorOverlap_ = designUnitsToPixels(u16(table, variants));
    const quint16 verticalCount = u16(table, variants + 6);
    const quint16 horizontalCount = u16(table, variants + 8);
    const auto parseConstructions = [&](bool vertical) {
      const quint16 count = vertical ? verticalCount : horizontalCount;
      const quint16 coverageOffset = u16(table, variants + (vertical ? 2 : 4));
      const qsizetype constructionArray = variants + 10 +
          (vertical ? 0 : qsizetype(verticalCount) * 2);
      const QVector<quint16> bases = coverageGlyphs(
          table, variants + coverageOffset);
      if (bases.size() < count ||
          constructionArray + qsizetype(count) * 2 > table.size())
        return;
      for (quint16 i = 0; i < count; ++i) {
        const quint16 constructionOffset = u16(table, constructionArray + i * 2);
        const qsizetype construction = variants + constructionOffset;
        if (!constructionOffset || construction + 4 > table.size()) continue;
        const quint16 assemblyOffset = u16(table, construction);
        const quint16 variantCount = u16(table, construction + 2);
        if (construction + 4 + qsizetype(variantCount) * 4 > table.size()) continue;
        QVector<RawVariant> records;
        records.reserve(variantCount);
        for (quint16 record = 0; record < variantCount; ++record) {
          const qsizetype offset = construction + 4 + record * 4;
          records.push_back({u16(table, offset), u16(table, offset + 2)});
        }
        (vertical ? verticalVariants_ : horizontalVariants_).insert(bases[i], records);
        const qsizetype assembly = construction + assemblyOffset;
        if (assemblyOffset && assembly + 6 <= table.size()) {
          const quint16 partCount = u16(table, assembly + 4);
          if (assembly + 6 + qsizetype(partCount) * 10 <= table.size()) {
            QVector<RawAssemblyPart> parts;
            parts.reserve(partCount);
            for (quint16 part = 0; part < partCount; ++part) {
              const qsizetype partOffset = assembly + 6 + part * 10;
              parts.push_back({u16(table, partOffset),
                               u16(table, partOffset + 2),
                               u16(table, partOffset + 4),
                               u16(table, partOffset + 6),
                               (u16(table, partOffset + 8) & 1u) != 0});
            }
            (vertical ? verticalAssemblyParts_ : horizontalAssemblyParts_)
                .insert(bases[i], parts);
            (vertical ? verticalAssemblyCorrections_ : horizontalAssemblyCorrections_)
                .insert(bases[i], s16(table, assembly));
          }
        }
      }
    };
    parseConstructions(true);
    parseConstructions(false);
  }
}

std::optional<MathGlyphMetrics> OpenTypeMathFont::glyph(const QString& character) const {
  if (!valid()) return std::nullopt;
  const QList<quint32> indexes = font_.glyphIndexesForString(character);
  if (indexes.size() != 1 || indexes.front() == 0) return std::nullopt;
  const quint32 index = indexes.front();
  const QList<QPointF> advances = font_.advancesForGlyphIndexes(indexes);
  MathGlyphMetrics result;
  result.glyphIndex = index;
  const qreal outlineScale = kCssMathPixelSize / kOutlineLoadPixelSize;
  result.advance = advances.isEmpty() ? 0.0 : advances.front().x() * outlineScale;
  const QRectF outline = font_.boundingRect(index);
  result.inkBounds = QRectF(outline.x() * outlineScale, outline.y() * outlineScale,
                           outline.width() * outlineScale, outline.height() * outlineScale);
  result.italicCorrection = designUnitsToPixels(italicCorrections_.value(index));
  result.topAccentAttachment = accentAttachments_.contains(index)
      ? designUnitsToPixels(accentAttachments_.value(index)) : result.advance / 2.0;
  result.extendedShape = extendedShapes_.contains(index);
  return result;
}

std::optional<MathGlyphMetrics> OpenTypeMathFont::mathItalicGlyph(QChar character) const {
  char32_t codepoint = 0;
  if (character >= QLatin1Char('A') && character <= QLatin1Char('Z'))
    codepoint = 0x1D434 + character.unicode() - 'A';
  else if (character >= QLatin1Char('a') && character <= QLatin1Char('z'))
    codepoint = 0x1D44E + character.unicode() - 'a';
  else if (character.unicode() >= 0x03B1 && character.unicode() <= 0x03C9)
    codepoint = 0x1D6FC + character.unicode() - 0x03B1;
  else {
    switch (character.unicode()) {
      case 0x03F5: codepoint = 0x1D716; break;  // epsilon symbol
      case 0x03D1: codepoint = 0x1D717; break;  // theta symbol
      case 0x03F0: codepoint = 0x1D718; break;  // kappa symbol
      case 0x03D5: codepoint = 0x1D719; break;  // phi symbol
      case 0x03F1: codepoint = 0x1D71A; break;  // rho symbol
      case 0x03D6: codepoint = 0x1D71B; break;  // pi symbol
      default: return glyph(QString(character));
    }
  }
  // Mathematical italic h is the legacy Planck constant U+210E.
  if (character == QLatin1Char('h')) codepoint = 0x210E;
  return glyph(QString::fromUcs4(&codepoint, 1));
}

std::optional<MathGlyphVariant> OpenTypeMathFont::verticalVariant(
    const QString& character, qreal minimumExtent, bool allowAssembly) const {
  const auto base = glyph(character);
  if (!base) return std::nullopt;
  const QVector<RawVariant> records = verticalVariants_.value(base->glyphIndex);
  if (records.isEmpty()) return std::nullopt;
  const RawVariant* selected = nullptr;
  for (const RawVariant& record : records) {
    if (designUnitsToPixels(record.extent) >= minimumExtent) {
      selected = &record;
      break;
    }
  }
  if (selected == nullptr && allowAssembly) {
    if (const auto assembly = verticalAssembly(character, minimumExtent)) return assembly;
  }
  if (selected == nullptr) selected = &records.back();
  const QList<quint32> indexes{selected->glyphIndex};
  const QList<QPointF> advances = font_.advancesForGlyphIndexes(indexes);
  const qreal outlineScale = kCssMathPixelSize / kOutlineLoadPixelSize;
  qreal inlineAdvance = advances.isEmpty() ? 0.0 : advances.front().x();
  if (character.size() == 1 && QStringLiteral("()[]{}|").contains(character))
    inlineAdvance = std::max(inlineAdvance,
                             font_.boundingRect(selected->glyphIndex).width());
  qreal extent = designUnitsToPixels(selected->extent);
  if (character.size() == 1 && QStringLiteral("()[]{}|").contains(character))
    extent = std::max(extent,
                      font_.boundingRect(selected->glyphIndex).height() * outlineScale);
  return MathGlyphVariant{selected->glyphIndex,
                          inlineAdvance * outlineScale,
                          extent,
                          designUnitsToPixels(
                              italicCorrections_.value(selected->glyphIndex))};
}

std::optional<MathGlyphVariant> OpenTypeMathFont::verticalAssembly(
    const QString& character, qreal targetExtent) const {
  const auto assembly = verticalAssemblyParts(character, targetExtent);
  if (!assembly || assembly->parts.isEmpty()) return std::nullopt;
  return MathGlyphVariant{assembly->parts.front().glyphIndex,
                          assembly->advance, assembly->extent,
                          assembly->italicCorrection};
}

std::optional<MathGlyphAssembly> OpenTypeMathFont::verticalAssemblyParts(
    const QString& character, qreal targetExtent) const {
  return assemblyParts(character, targetExtent, true);
}

std::optional<MathGlyphVariant> OpenTypeMathFont::horizontalVariant(
    const QString& character, qreal minimumExtent, bool allowAssembly) const {
  const auto base = glyph(character);
  if (!base) return std::nullopt;
  const QVector<RawVariant> records = horizontalVariants_.value(base->glyphIndex);
  if (records.isEmpty()) return std::nullopt;
  const RawVariant* selected = nullptr;
  for (const RawVariant& record : records) {
    if (designUnitsToPixels(record.extent) >= minimumExtent) {
      selected = &record;
      break;
    }
  }
  if (selected == nullptr && allowAssembly) {
    if (const auto assembly = horizontalAssemblyParts(character, minimumExtent);
        assembly && !assembly->parts.isEmpty())
      return MathGlyphVariant{assembly->parts.front().glyphIndex,
                              assembly->advance, assembly->extent,
                              assembly->italicCorrection};
  }
  if (selected == nullptr) selected = &records.back();
  const qreal outlineScale = kCssMathPixelSize / kOutlineLoadPixelSize;
  return MathGlyphVariant{
      selected->glyphIndex,
      font_.boundingRect(selected->glyphIndex).height() * outlineScale,
      designUnitsToPixels(selected->extent),
      designUnitsToPixels(italicCorrections_.value(selected->glyphIndex))};
}

std::optional<MathGlyphAssembly> OpenTypeMathFont::horizontalAssemblyParts(
    const QString& character, qreal targetExtent) const {
  return assemblyParts(character, targetExtent, false);
}

std::optional<MathGlyphAssembly> OpenTypeMathFont::assemblyParts(
    const QString& character, qreal targetExtent, bool vertical) const {
  const auto base = glyph(character);
  if (!base) return std::nullopt;
  const QVector<RawAssemblyPart> parts = (vertical ? verticalAssemblyParts_
                                                    : horizontalAssemblyParts_)
      .value(base->glyphIndex);
  if (parts.isEmpty()) return std::nullopt;
  QList<quint32> indexes;
  indexes.reserve(parts.size());
  for (const RawAssemblyPart& part : parts) indexes.push_back(part.glyphIndex);
  const QList<QPointF> advances = font_.advancesForGlyphIndexes(indexes);
  qreal advance = vertical
      ? base->advance * kOutlineLoadPixelSize / kCssMathPixelSize
      : font_.boundingRect(base->glyphIndex).height();
  for (qsizetype i = 0; i < indexes.size(); ++i) {
    if (vertical && i < advances.size())
      advance = std::max(advance, advances[i].x() +
                                  italicCorrections_.value(indexes[i]));
    const QRectF bounds = font_.boundingRect(indexes[i]);
    advance = std::max(advance, vertical ? bounds.width() : bounds.height());
  }
  const qreal outlineScale = kCssMathPixelSize / kOutlineLoadPixelSize;
  qreal maximumConnectorOverlap = std::numeric_limits<qreal>::max();
  qreal nonExtenderAdvance = 0.0;
  qreal extenderAdvance = 0.0;
  int nonExtenderCount = 0;
  int extenderCount = 0;
  for (qsizetype index = 0; index < parts.size(); ++index) {
    const RawAssemblyPart& part = parts.at(index);
    const qreal fullAdvance = designUnitsToPixels(part.fullAdvance);
    if (part.extender) {
      ++extenderCount;
      extenderAdvance += fullAdvance;
    } else {
      ++nonExtenderCount;
      nonExtenderAdvance += fullAdvance;
    }
    if (part.extender || index > 0)
      maximumConnectorOverlap = std::min(
          maximumConnectorOverlap,
          designUnitsToPixels(part.startConnector));
    if (part.extender || index + 1 < parts.size())
      maximumConnectorOverlap = std::min(
          maximumConnectorOverlap,
          designUnitsToPixels(part.endConnector));
  }
  const qreal extenderNonOverlappingAdvance =
      extenderAdvance - minimumConnectorOverlap_ * extenderCount;
  if (extenderCount == 0 ||
      maximumConnectorOverlap < minimumConnectorOverlap_ ||
      extenderNonOverlappingAdvance <= 0.0)
    return std::nullopt;

  constexpr int kMaximumAssemblyGlyphs = 4096;
  int repetitionCount = std::max(
      0, static_cast<int>(std::ceil(
             (targetExtent - nonExtenderAdvance +
              minimumConnectorOverlap_ * (nonExtenderCount - 1)) /
             extenderNonOverlappingAdvance)));
  repetitionCount = std::min(
      repetitionCount,
      (kMaximumAssemblyGlyphs - nonExtenderCount) / extenderCount);
  const int glyphCount =
      nonExtenderCount + repetitionCount * extenderCount;
  if (glyphCount <= 0) return std::nullopt;

  qreal connectorOverlap = maximumConnectorOverlap;
  if (glyphCount > 1) {
    const qreal theoreticalMaximum =
        (nonExtenderAdvance + repetitionCount * extenderAdvance -
         targetExtent) /
        (glyphCount - 1);
    connectorOverlap = std::max(
        minimumConnectorOverlap_,
        std::min(maximumConnectorOverlap, theoreticalMaximum));
  }
  const qreal realizedExtent =
      nonExtenderAdvance + repetitionCount * extenderAdvance -
      connectorOverlap * (glyphCount - 1);

  QVector<const RawAssemblyPart*> sequence;
  sequence.reserve(glyphCount);
  for (const RawAssemblyPart& part : parts) {
    const int copies = part.extender ? repetitionCount : 1;
    for (int copy = 0; copy < copies; ++copy) sequence.push_back(&part);
  }
  if (vertical) std::reverse(sequence.begin(), sequence.end());

  MathGlyphAssembly result;
  result.advance = advance * outlineScale;
  result.italicCorrection = designUnitsToPixels(
      (vertical ? verticalAssemblyCorrections_ : horizontalAssemblyCorrections_)
          .value(base->glyphIndex));
  result.extent = realizedExtent;
  result.parts.reserve(sequence.size());
  qreal offset = 0.0;
  for (qsizetype i = 0; i < sequence.size(); ++i) {
    const RawAssemblyPart& part = *sequence[i];
    const qreal fullAdvance = designUnitsToPixels(part.fullAdvance);
    const qreal overlap = i + 1 < sequence.size()
        ? connectorOverlap : 0.0;
    result.parts.push_back({part.glyphIndex, offset, fullAdvance,
                            overlap, part.extender});
    offset += fullAdvance - overlap;
  }
  if (!result.parts.isEmpty())
    result.extent = result.parts.back().offset + result.parts.back().fullAdvance;
  for (const MathGlyphAssemblyPart& part : result.parts) {
    QRectF partBounds = glyphPath(part.glyphIndex).boundingRect();
    if (vertical)
      partBounds.translate(0.0, part.offset - partBounds.top());
    else
      partBounds.translate(part.offset, 0.0);
    result.inkBounds = result.inkBounds.isNull()
        ? partBounds : result.inkBounds.united(partBounds);
  }
  return result;
}

QPainterPath OpenTypeMathFont::glyphPath(quint32 glyphIndex) const {
  if (!valid() || glyphIndex == 0) return {};
  QPainterPath path = font_.pathForGlyph(glyphIndex);
  QTransform scale;
  scale.scale(kCssMathPixelSize / kOutlineLoadPixelSize,
              kCssMathPixelSize / kOutlineLoadPixelSize);
  return scale.map(path);
}

QRawFont OpenTypeMathFont::rasterFont(qreal fontScale) const {
  QRawFont result = rasterFont_;
  if (result.isValid())
    result.setPixelSize(kCssMathPixelSize * std::max<qreal>(0.0, fontScale));
  return result;
}

QRectF OpenTypeMathFont::rasterGlyphBounds(quint32 glyphIndex,
                                            qreal fontScale) const {
  if (glyphIndex == 0) return {};
  const QRawFont font = rasterFont(fontScale);
  return font.isValid() ? font.boundingRect(glyphIndex) : QRectF{};
}

std::optional<MathShapedTextRun> OpenTypeMathFont::shapeMathMlText(
    const QString& text, qreal fontScale) const {
  if (!valid() || text.isEmpty() || fontScale <= 0.0) return std::nullopt;
  const qreal requestedPixelSize = pixelSize() * fontScale;
  QFont shapingFont(familyName());
  shapingFont.setPixelSize(qRound(kOutlineLoadPixelSize));
  shapingFont.setHintingPreference(QFont::PreferNoHinting);
  shapingFont.setStyleStrategy(QFont::NoFontMerging);
  shapingFont.setFeature(QFont::Tag("liga"), 0);
  shapingFont.setFeature(QFont::Tag("clig"), 0);
  QTextLayout shaping(text, shapingFont);
  QTextOption textOption;
  textOption.setTextDirection(Qt::LeftToRight);
  shaping.setTextOption(textOption);
  shaping.beginLayout();
  QTextLine line = shaping.createLine();
  if (line.isValid()) line.setLineWidth(1e9);
  shaping.endLayout();
  if (!line.isValid()) return std::nullopt;

  const qreal scale = requestedPixelSize / kOutlineLoadPixelSize;
  const QRawFont rawFont = rasterFont(fontScale);
  MathShapedTextRun result;
  result.rawFont = rawFont;
  result.familyName = rawFont.familyName();
  result.text = text;
  const auto runs = line.glyphRuns(
      0, -1, QTextLayout::RetrieveGlyphIndexes |
                 QTextLayout::RetrieveGlyphPositions);
  for (const QGlyphRun& run : runs) {
    const QList<quint32> indexes = run.glyphIndexes();
    const QList<QPointF> positions = run.positions();
    if (indexes.isEmpty() || indexes.contains(0) ||
        indexes.size() != positions.size() ||
        run.rawFont().familyName() != familyName())
      return std::nullopt;
    for (qsizetype index = 0; index < indexes.size(); ++index) {
      const QPointF position(
          positions.at(index).x() * scale,
          (positions.at(index).y() - line.ascent()) * scale);
      result.glyphIndexes.push_back(indexes.at(index));
      result.positions.push_back(position);
      const QRectF glyphInk = rawFont.boundingRect(indexes.at(index))
                                  .translated(position);
      result.inkBounds = result.inkBounds.isNull()
          ? glyphInk : result.inkBounds.united(glyphInk);
    }
  }
  if (result.glyphIndexes.isEmpty()) return std::nullopt;
  result.advance = line.naturalTextWidth() * scale;
  return result;
}

std::optional<MathShapedText> OpenTypeMathFont::shapeMathMlTextWithFallback(
    const QString& text, const QStringList& fallbackFamilies,
    qreal fontScale) const {
  if (!valid() || text.isEmpty() || fontScale <= 0.0) return std::nullopt;
  QStringList families{familyName()};
  for (const QString& family : fallbackFamilies)
    if (!family.isEmpty() && !families.contains(family)) families.push_back(family);

  QFont shapingFont;
  shapingFont.setFamilies(families);
  shapingFont.setPixelSize(qRound(kOutlineLoadPixelSize));
  shapingFont.setHintingPreference(QFont::PreferNoHinting);
  shapingFont.setFeature(QFont::Tag("liga"), 0);
  shapingFont.setFeature(QFont::Tag("clig"), 0);
  QTextLayout shaping(text, shapingFont);
  QTextOption textOption;
  textOption.setTextDirection(Qt::LeftToRight);
  shaping.setTextOption(textOption);
  shaping.beginLayout();
  QTextLine line = shaping.createLine();
  if (line.isValid()) line.setLineWidth(1e9);
  shaping.endLayout();
  if (!line.isValid()) return std::nullopt;

  const qreal requestedPixelSize = pixelSize() * fontScale;
  const qreal scale = requestedPixelSize / kOutlineLoadPixelSize;
  MathShapedText result;
  result.advance = line.naturalTextWidth() * scale;
  result.fontPixelSize = requestedPixelSize;
  const auto glyphRuns = line.glyphRuns(
      0, -1, QTextLayout::RetrieveGlyphIndexes |
                 QTextLayout::RetrieveGlyphPositions |
                 QTextLayout::RetrieveStringIndexes);
  for (const QGlyphRun& glyphRun : glyphRuns) {
    const QList<quint32> indexes = glyphRun.glyphIndexes();
    const QList<QPointF> positions = glyphRun.positions();
    const QList<qsizetype> stringIndexes = glyphRun.stringIndexes();
    if (indexes.isEmpty() || indexes.contains(0) ||
        indexes.size() != positions.size())
      return std::nullopt;

    MathShapedTextRun run;
    const QRawFont outlineFont = glyphRun.rawFont();
    const bool mathFamily = outlineFont.familyName() == familyName();
    run.rawFont = mathFamily ? rasterFont(fontScale) : outlineFont;
    if (run.rawFont.pixelSize() != requestedPixelSize)
      run.rawFont.setPixelSize(requestedPixelSize);
    if (!run.rawFont.isValid()) return std::nullopt;
    run.familyName = run.rawFont.familyName();
    if (!stringIndexes.isEmpty()) {
      const auto [first, last] = std::minmax_element(
          stringIndexes.cbegin(), stringIndexes.cend());
      const qsizetype end = std::min<qsizetype>(text.size(), *last + 1);
      run.text = text.mid(*first, end - *first);
    } else {
      run.text = text;
    }
    run.glyphIndexes.reserve(indexes.size());
    run.positions.reserve(positions.size());
    const QList<QPointF> advances = mathFamily
        ? run.rawFont.advancesForGlyphIndexes(indexes)
        : outlineFont.advancesForGlyphIndexes(indexes);
    qreal runRight = 0.0;
    for (qsizetype index = 0; index < indexes.size(); ++index) {
      const QPointF position(
          positions.at(index).x() * scale,
          (positions.at(index).y() - line.ascent()) * scale);
      run.glyphIndexes.push_back(indexes.at(index));
      run.positions.push_back(position);
      QRectF glyphInk = mathFamily
          ? run.rawFont.boundingRect(indexes.at(index))
          : QTransform::fromScale(scale, scale).mapRect(
                outlineFont.boundingRect(indexes.at(index)));
      glyphInk.translate(position);
      run.inkBounds = run.inkBounds.isNull()
          ? glyphInk : run.inkBounds.united(glyphInk);
      runRight = std::max(runRight,
                          position.x() +
                              (index < advances.size()
                                   ? advances.at(index).x() *
                                         (mathFamily ? 1.0 : scale)
                                   : 0.0));
    }
    run.advance = runRight;
    result.runs.push_back(std::move(run));
  }
  if (result.runs.isEmpty()) return std::nullopt;

  for (const MathShapedTextRun& run : result.runs) {
    result.inkBounds = result.inkBounds.isNull()
        ? run.inkBounds : result.inkBounds.united(run.inkBounds);
  }
  bool hasStrongLtr = false;
  bool hasStrongRtl = false;
  bool hasFormatControl = false;
  bool hasFullEmScript = false;
  for (QChar character : text) {
    hasStrongLtr = hasStrongLtr || character.direction() == QChar::DirL;
    hasStrongRtl = hasStrongRtl ||
        character.direction() == QChar::DirR ||
        character.direction() == QChar::DirAL;
    hasFormatControl = hasFormatControl ||
        character.category() == QChar::Other_Format;
    const QChar::Script script = character.script();
    hasFullEmScript = hasFullEmScript || script == QChar::Script_Han ||
        script == QChar::Script_Hiragana ||
        script == QChar::Script_Katakana ||
        script == QChar::Script_Hangul;
  }
  result.compoundLineBox = result.runs.size() > 1 || hasFormatControl ||
                           (hasStrongLtr && hasStrongRtl);
  result.formatControlledLineBox = hasFormatControl;
  result.fullEmLineBox = hasFullEmScript;
  return result;
}

qreal OpenTypeMathFont::textAdvance(const QString& text) const {
  if (!valid() || text.isEmpty()) return 0.0;
  const QList<QPointF> advances = font_.advancesForGlyphIndexes(font_.glyphIndexesForString(text));
  qreal width = 0.0;
  for (const QPointF& advance : advances) width += advance.x();
  return width * kCssMathPixelSize / kOutlineLoadPixelSize;
}

QRectF OpenTypeMathFont::textInkBounds(const QString& text) const {
  if (!valid() || text.isEmpty()) return {};
  const QList<quint32> indexes = font_.glyphIndexesForString(text);
  const QList<QPointF> advances = font_.advancesForGlyphIndexes(indexes);
  QRectF bounds;
  qreal x = 0.0;
  for (qsizetype i = 0; i < indexes.size(); ++i) {
    const QRectF glyphBounds = font_.boundingRect(indexes[i]).translated(x, 0.0);
    bounds = bounds.isNull() ? glyphBounds : bounds.united(glyphBounds);
    if (i < advances.size()) x += advances[i].x();
  }
  const qreal scale = kCssMathPixelSize / kOutlineLoadPixelSize;
  return QRectF(bounds.x() * scale, bounds.y() * scale,
                bounds.width() * scale, bounds.height() * scale);
}

}  // namespace muffin::math
