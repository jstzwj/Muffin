#include "theme/FontRendering.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QRawFont>
#include <QtEndian>
#include <QtGlobal>
#include <cmath>
#include <initializer_list>

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
        if (original.isValid() && bold.familyName() == original.familyName() && bold.fontTable("head") == original.fontTable("head") &&
            glyphs.size() == 4 && !glyphs.contains(0)) {
          const auto simulated = bold.advancesForGlyphIndexes(glyphs);
          const auto unsimulated = original.advancesForGlyphIndexes(glyphs);
          qreal minimum = 1e6, maximum = 0, sum = 0;
          for (int i = 0; i < 3; ++i) {
            const qreal delta = simulated[i].x() - unsimulated[i].x();
            minimum = qMin(minimum, delta);
            maximum = qMax(maximum, delta);
            sum += delta;
          }
          // Only compensate the verified uniform simulation. Real bold faces,
          // variable axes and nonuniform design changes keep their own metrics.
          if (minimum > 1.0 / 64 && maximum - minimum <= 2.0 / 64 && maximum < bold.pixelSize() / 10 &&
              qAbs(simulated[3].x() - unsimulated[3].x()) <= 1.0 / 64)
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

namespace muffin::font_rendering {
namespace {
QHash<QString, QString> defaultFamilies;
const QStringList& availableDocumentFamilies() {
  static QStringList families = QFontDatabase::families();
  static const auto connection = QObject::connect(qGuiApp, &QGuiApplication::fontDatabaseChanged, qGuiApp, [] {
    families = QFontDatabase::families();
    defaultFamilies.clear();
  });
  Q_UNUSED(connection);
  return families;
}
QString firstAvailableFontFamily(std::initializer_list<QString> candidates) {
  const QStringList& availableFamilies = availableDocumentFamilies();
  QString key;
  for (const auto& candidate : candidates) key += candidate + QLatin1Char('\n');
  if (const auto found = defaultFamilies.constFind(key); found != defaultFamilies.cend()) return *found;
  for (const QString& candidate : candidates) {
    for (const QString& family : availableFamilies) {
      if (family.compare(candidate, Qt::CaseInsensitive) == 0) {
        defaultFamilies.insert(key, family);
        return family;
      }
    }
  }
  const QString systemFamily = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
  const auto result = systemFamily.isEmpty() ? QStringLiteral("sans-serif") : systemFamily;
  defaultFamilies.insert(key, result);
  return result;
}

}  // namespace
// Resolved per-platform fallback families, cached until fonts change. Used both as
// the legacy default (when a theme supplies no font) and as the substitution tail
// appended after a theme-supplied family so missing glyphs (CJK, symbols) resolve.
QString sansFamily() {
  const QString f = firstAvailableFontFamily({
#if defined(Q_OS_WIN)
      QStringLiteral("Microsoft YaHei UI"),
      QStringLiteral("Segoe UI"),
      QStringLiteral("Arial"),
#elif defined(Q_OS_MACOS)
      QStringLiteral("PingFang SC"),
      QStringLiteral("Hiragino Sans GB"),
      QStringLiteral("Helvetica Neue"),
      QStringLiteral("Arial"),
#else
      QStringLiteral("Noto Sans CJK SC"),
      QStringLiteral("Noto Sans"),
      QStringLiteral("DejaVu Sans"),
      QStringLiteral("Arial"),
#endif
  });
  return f;
}
QString serifFamily() {
  const QString f = firstAvailableFontFamily({
#if defined(Q_OS_WIN)
      QStringLiteral("Georgia"),
      QStringLiteral("Cambria"),
      QStringLiteral("Times New Roman"),
#elif defined(Q_OS_MACOS)
      QStringLiteral("New York"),
      QStringLiteral("Times New Roman"),
      QStringLiteral("Georgia"),
#else
      QStringLiteral("Noto Serif"),
      QStringLiteral("DejaVu Serif"),
      QStringLiteral("Times New Roman"),
#endif
      QStringLiteral("serif"),
  });
  return f;
}
QString codeFamily() {
  const QString f = firstAvailableFontFamily({
#if defined(Q_OS_WIN)
      QStringLiteral("Lucida Console"),
      QStringLiteral("Consolas"),
      QStringLiteral("Courier"),
#elif defined(Q_OS_MACOS)
      QStringLiteral("Menlo"),
      QStringLiteral("Monaco"),
      QStringLiteral("Courier New"),
#else
      QStringLiteral("DejaVu Sans Mono"),
      QStringLiteral("Noto Sans Mono"),
      QStringLiteral("Liberation Mono"),
#endif
      QStringLiteral("monospace"),
  });
  return f;
}
QString genericFamilyTail(const QString& generic) {
  const QString lower = generic.toLower();
  if (lower == QStringLiteral("serif")) {
    return serifFamily();
  }
  if (lower == QStringLiteral("monospace")) {
    return codeFamily();
  }
  return sansFamily();
}

QString availableFamilyNamed(const QString& wanted, const QStringList& available) {
  for (const QString& family : available) {
    if (family.compare(wanted, Qt::CaseInsensitive) == 0) {
      return family;
    }
  }
  return {};
}

QString platformCssFamilyAlias(const QString& requested, const QStringList& available) {
  if (const QString exact = availableFamilyNamed(requested, available); !exact.isEmpty()) {
    return exact;
  }
#if defined(Q_OS_WIN)
  // Chromium resolves these traditional CSS/PostScript names through Windows
  // aliases. DirectWrite via QFont::setFamilies does not, so make the same
  // substitutions only when the concrete Windows family is installed.
  const QString lower = requested.toLower();
  QString target;
  if (lower == QStringLiteral("times"))
    target = QStringLiteral("Times New Roman");
  else if (lower == QStringLiteral("helvetica"))
    target = QStringLiteral("Arial");
  else if (lower == QStringLiteral("courier"))
    target = QStringLiteral("Courier New");
  if (!target.isEmpty()) {
    if (const QString actual = availableFamilyNamed(target, available); !actual.isEmpty()) {
      return actual;
    }
  }
#else
  Q_UNUSED(available);
#endif
  return requested;
}

QStringList cssFamilyList(const QString& raw, const QString& platformTail, const QHash<QString, QString>& aliases) {
  QStringList requested = raw.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  for (QString& f : requested) {
    f = f.trimmed();
  }
  requested.removeAll(QString());

  QString genericTail;
  if (!requested.isEmpty()) {
    const QString last = requested.last().toLower();
    if (last == QStringLiteral("serif") || last == QStringLiteral("sans-serif") || last == QStringLiteral("monospace")) {
      genericTail = genericFamilyTail(requested.takeLast());
    }
  }

  const QStringList& availableFamilies = availableDocumentFamilies();
  QStringList out;
  for (const QString& family : requested) {
    // @font-face: a CSS theme may declare `font-family: CascadiaCode` while the
    // font file's internal name (what QFontDatabase registers) is "Cascadia Code",
    // or `"LXGW WenKai"` whose internal name is 霞鹜文楷. Substitute the declared
    // alias with the registered name so the stack resolves to the bundled font.
    QString resolved = family;
    if (const QString alias = aliases.value(family.toLower()); !alias.isEmpty()) {
      resolved = alias;
    }
    resolved = platformCssFamilyAlias(resolved, availableFamilies);
    // Keep unresolved CSS names in their declared order. Qt's font matcher owns
    // platform aliases/substitutions (for example CSS `Times` -> Times New Roman
    // on Windows) and per-glyph fallback. Pre-filtering against the exact names
    // returned by QFontDatabase removes those aliases and can promote a later CJK
    // family to the primary font for Latin text.
    if (!out.contains(resolved, Qt::CaseInsensitive)) {
      out << resolved;
    }
  }
  if (!genericTail.isEmpty() && !out.contains(genericTail, Qt::CaseInsensitive)) {
    out << genericTail;
  }
  if (!platformTail.isEmpty() && !out.contains(platformTail, Qt::CaseInsensitive)) {
    out << platformTail;
  }
  return out;
}

}  // namespace muffin::font_rendering
