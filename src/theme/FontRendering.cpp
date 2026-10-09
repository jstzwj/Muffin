#include "theme/FontRendering.h"

#include <QtGlobal>
#include <QRawFont>
#include <QtEndian>
#include <cmath>

void muffin::font_rendering::configureForScreen(QFont& font) {
#if defined(Q_OS_WIN)
  // Qt 6.11 maps the default preference at DPR 1 to full horizontal hinting
  // and GDI-classic metrics for small text. Vertical hinting selects
  // DirectWrite ClearType Natural instead: vertical grid fitting stays crisp,
  // while horizontal advances remain fractional and layout-device independent.
  font.setHintingPreference(QFont::PreferVerticalHinting);
#else
  Q_UNUSED(font);
#endif
}

void muffin::font_rendering::configureCssFont(QFont& font, qreal letterSpacing, qreal wordSpacing) {
  configureForScreen(font);
  qreal correction = 0;
#if defined(Q_OS_WIN)
  // DirectWrite's synthetic bold adds an advance to ink glyphs, while browser
  // emboldening preserves the original advances. Measure the simulation rather
  // than assuming a particular family or a fixed percentage of the font size.
  if (font.weight() >= QFont::DemiBold) {
    const auto bold = QRawFont::fromFont(font);
    const auto os2 = bold.fontTable("OS/2");
    if (bold.isValid() && os2.size() >= 6 && bold.fontTable("fvar").isEmpty()) {
      const auto faceWeight = qFromBigEndian<quint16>(os2.constData() + 4);
      if (faceWeight >= 1 && faceWeight < QFont::DemiBold && faceWeight < font.weight()) {
        auto originalFont = font;
        originalFont.setWeight(static_cast<QFont::Weight>(faceWeight));
        const auto original = QRawFont::fromFont(originalFont);
        const auto glyphs = bold.glyphIndexesForString(QStringLiteral("HMn "));
        if (original.isValid() && bold.familyName() == original.familyName() &&
            bold.fontTable("head") == original.fontTable("head") && glyphs.size() == 4 && !glyphs.contains(0)) {
          const auto simulated = bold.advancesForGlyphIndexes(glyphs);
          const auto unsimulated = original.advancesForGlyphIndexes(glyphs);
          qreal minimum = 1e6, maximum = 0, sum = 0;
          for (int i = 0; i < 3; ++i) {
            const qreal delta = simulated[i].x() - unsimulated[i].x();
            minimum = qMin(minimum, delta); maximum = qMax(maximum, delta); sum += delta;
          }
          // Only compensate the verified uniform simulation. Real bold faces,
          // variable axes and nonuniform design changes keep their own metrics.
          if (minimum > 1.0 / 64 && maximum - minimum <= 2.0 / 64 &&
              maximum < bold.pixelSize() / 10 && qAbs(simulated[3].x() - unsimulated[3].x()) <= 1.0 / 64)
            correction = std::round(sum / 3 * 64) / 64;
        }
      }
    }
  }
#endif
  font.setLetterSpacing(QFont::AbsoluteSpacing, letterSpacing - correction);
  // Whitespace isn't emboldened, so cancel the letter correction on spaces.
  font.setWordSpacing(wordSpacing + correction);
}
