#pragma once

#include <QByteArray>
#include <QHash>
#include <QRawFont>
#include <QRectF>
#include <QString>
#include <QVector>

#include <optional>

namespace muffin::math {

struct MathFontConstants {
  qreal scriptPercentScaleDown = 0.0;
  qreal scriptScriptPercentScaleDown = 0.0;
  qreal displayOperatorMinHeight = 0.0;
  qreal axisHeight = 0.0;
  qreal subscriptShiftDown = 0.0;
  qreal subscriptTopMax = 0.0;
  qreal subscriptBaselineDropMin = 0.0;
  qreal superscriptShiftUp = 0.0;
  qreal superscriptBottomMin = 0.0;
  qreal superscriptBaselineDropMax = 0.0;
  qreal subSuperscriptGapMin = 0.0;
  qreal spaceAfterScript = 0.0;
  qreal upperLimitGapMin = 0.0;
  qreal upperLimitBaselineRiseMin = 0.0;
  qreal lowerLimitGapMin = 0.0;
  qreal lowerLimitBaselineDropMin = 0.0;
  qreal fractionNumeratorShiftUp = 0.0;
  qreal fractionNumeratorDisplayStyleShiftUp = 0.0;
  qreal fractionDenominatorShiftDown = 0.0;
  qreal fractionDenominatorDisplayStyleShiftDown = 0.0;
  qreal fractionNumeratorGapMin = 0.0;
  qreal fractionNumeratorDisplayStyleGapMin = 0.0;
  qreal fractionRuleThickness = 0.0;
  qreal fractionDenominatorGapMin = 0.0;
  qreal fractionDenominatorDisplayStyleGapMin = 0.0;
  qreal stackTopShiftUp = 0.0;
  qreal stackTopDisplayStyleShiftUp = 0.0;
  qreal stackBottomShiftDown = 0.0;
  qreal stackBottomDisplayStyleShiftDown = 0.0;
  qreal stackGapMin = 0.0;
  qreal stackDisplayStyleGapMin = 0.0;
  qreal overbarVerticalGap = 0.0;
  qreal overbarRuleThickness = 0.0;
  qreal overbarExtraAscender = 0.0;
  qreal underbarVerticalGap = 0.0;
  qreal underbarRuleThickness = 0.0;
  qreal underbarExtraDescender = 0.0;
  qreal radicalVerticalGap = 0.0;
  qreal radicalRuleThickness = 0.0;
  qreal radicalExtraAscender = 0.0;
  qreal radicalKernBeforeDegree = 0.0;
  qreal radicalKernAfterDegree = 0.0;
  qreal radicalDegreeBottomRaisePercent = 0.0;
};

struct MathGlyphMetrics {
  quint32 glyphIndex = 0;
  qreal advance = 0.0;
  QRectF inkBounds;
  qreal italicCorrection = 0.0;
  qreal topAccentAttachment = 0.0;
  bool extendedShape = false;
};

struct MathGlyphVariant {
  quint32 glyphIndex = 0;
  qreal advance = 0.0;
  qreal extent = 0.0;
};

// Loads the bundled strict-oracle font and exposes the OpenType MATH data used
// by Chromium's MathML layout. All returned dimensions are CSS pixels.
class OpenTypeMathFont {
public:
  static const OpenTypeMathFont& instance();

  bool valid() const;
  QString familyName() const;
  qreal pixelSize() const;
  qreal unitsPerEm() const;
  const MathFontConstants& constants() const;
  std::optional<MathGlyphMetrics> glyph(const QString& character) const;
  std::optional<MathGlyphMetrics> mathItalicGlyph(QChar character) const;
  std::optional<MathGlyphVariant> verticalVariant(const QString& character,
                                                  qreal minimumExtent,
                                                  bool allowAssembly = false) const;
  std::optional<MathGlyphVariant> verticalAssembly(const QString& character,
                                                   qreal targetExtent) const;
  qreal textAdvance(const QString& text) const;
  QRectF textInkBounds(const QString& text) const;

private:
  OpenTypeMathFont();

  qreal designUnitsToPixels(qint16 value) const;
  void parseMathTable(const QByteArray& table);

  QRawFont font_;
  MathFontConstants constants_;
  QHash<quint32, qint16> italicCorrections_;
  QHash<quint32, qint16> accentAttachments_;
  QHash<quint32, bool> extendedShapes_;
  struct RawVariant { quint16 glyphIndex = 0; quint16 extent = 0; };
  QHash<quint32, QVector<RawVariant>> verticalVariants_;
  struct RawAssemblyPart {
    quint16 glyphIndex = 0;
    quint16 startConnector = 0;
    quint16 endConnector = 0;
    quint16 fullAdvance = 0;
    bool extender = false;
  };
  qreal minimumConnectorOverlap_ = 0.0;
  QHash<quint32, QVector<RawAssemblyPart>> verticalAssemblyParts_;
  QHash<quint32, qint16> verticalAssemblyCorrections_;
};

}  // namespace muffin::math
