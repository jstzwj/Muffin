#include "math/OpenTypeMathFont.h"

#include <QFile>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QVector>

#include <algorithm>

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
  // Loading at one pixel per design unit avoids platform hinting from rounding
  // outline bounds before they are converted to CSS pixels.
  font_.loadFromData(file.readAll(), kOutlineLoadPixelSize, QFont::PreferNoHinting);
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
    constants_.superscriptBottomMin = value(9);
    constants_.superscriptBaselineDropMax = value(10);
    constants_.subSuperscriptGapMin = value(11);
    constants_.spaceAfterScript = value(13);
    constants_.upperLimitGapMin = value(14);
    constants_.upperLimitBaselineRiseMin = value(15);
    constants_.lowerLimitGapMin = value(16);
    constants_.lowerLimitBaselineDropMin = value(17);
    constants_.fractionNumeratorShiftUp = value(28);
    constants_.fractionDenominatorShiftDown = value(30);
    constants_.fractionNumeratorGapMin = value(32);
    constants_.fractionRuleThickness = value(34);
    constants_.fractionDenominatorGapMin = value(35);
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
    const quint16 coverageOffset = u16(table, variants + 2);
    const quint16 count = u16(table, variants + 6);
    const QVector<quint16> bases = coverageGlyphs(table, variants + coverageOffset);
    if (bases.size() >= count && variants + 10 + qsizetype(count) * 2 <= table.size()) {
      for (quint16 i = 0; i < count; ++i) {
        const quint16 constructionOffset = u16(table, variants + 10 + i * 2);
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
        verticalVariants_.insert(bases[i], records);
        const qsizetype assembly = construction + assemblyOffset;
        if (assemblyOffset && assembly + 6 <= table.size()) {
          const quint16 partCount = u16(table, assembly + 4);
          if (assembly + 6 + qsizetype(partCount) * 10 <= table.size()) {
            QVector<quint16> parts;
            parts.reserve(partCount);
            for (quint16 part = 0; part < partCount; ++part)
              parts.push_back(u16(table, assembly + 6 + part * 10));
            verticalAssemblyParts_.insert(bases[i], parts);
            verticalAssemblyCorrections_.insert(bases[i], s16(table, assembly));
          }
        }
      }
    }
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
  else
    return glyph(QString(character));
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
    const QVector<quint16> parts = verticalAssemblyParts_.value(base->glyphIndex);
    if (!parts.isEmpty()) {
      QList<quint32> indexes;
      indexes.reserve(parts.size());
      for (quint16 part : parts) indexes.push_back(part);
      const QList<QPointF> advances = font_.advancesForGlyphIndexes(indexes);
      qreal advance = base->advance * kOutlineLoadPixelSize / kCssMathPixelSize;
      for (qsizetype i = 0; i < indexes.size(); ++i) {
        if (i < advances.size())
          advance = std::max(advance, advances[i].x() +
                                      italicCorrections_.value(indexes[i]));
        advance = std::max(advance, font_.boundingRect(indexes[i]).width());
      }
      const qreal outlineScale = kCssMathPixelSize / kOutlineLoadPixelSize;
      return MathGlyphVariant{
          parts.front(),
          advance * outlineScale + designUnitsToPixels(
              verticalAssemblyCorrections_.value(base->glyphIndex)),
          minimumExtent};
    }
  }
  if (selected == nullptr) selected = &records.back();
  const QList<quint32> indexes{selected->glyphIndex};
  const QList<QPointF> advances = font_.advancesForGlyphIndexes(indexes);
  const qreal outlineScale = kCssMathPixelSize / kOutlineLoadPixelSize;
  return MathGlyphVariant{selected->glyphIndex,
                          advances.isEmpty() ? 0.0 : advances.front().x() * outlineScale,
                          designUnitsToPixels(selected->extent)};
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
