#include "theme/FontRendering.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QSet>
#include <QtGlobal>
#include <initializer_list>
#include <tuple>

#include "mermaid/MermaidFontRegistry.h"
#include "render/TextLayout.h"
#include "theme/CssContent.h"
#include "theme/FontMetricsPolicy.h"

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
  configureCssFont(font, letterSpacing, wordSpacing, documentTextBackend());
}
void muffin::font_rendering::configureCssFont(QFont& font, qreal letterSpacing, qreal wordSpacing,
                                              TextBackend backend) {
  configureForScreen(font);
  const qreal correction = syntheticBoldAdvanceCorrection(font);
  setTextLetterSpacing(font, letterSpacing - correction, backend);
  // Whitespace isn't emboldened, so cancel the letter correction on spaces.
  font.setWordSpacing(wordSpacing + correction);
}

namespace muffin::font_rendering {
namespace {
QHash<QString, QString> defaultFamilies;
const QStringList& bundledFallbackFamilies() {
  static const QStringList families = mermaid::MermaidFontRegistry::familyStack();
  return families;
}
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
  for (const auto& candidate : candidates)
    key += candidate + QLatin1Char('\n');
  if (const auto found = defaultFamilies.constFind(key); found != defaultFamilies.cend())
    return *found;
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

} // namespace
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
QFont sourceEditorFont() {
  const QFont systemFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  QFont font(firstAvailableFontFamily({
#if defined(Q_OS_WIN)
      QStringLiteral("Consolas"),
      QStringLiteral("Cascadia Mono"),
#elif defined(Q_OS_MACOS)
      QStringLiteral("Menlo"),
      QStringLiteral("Monaco"),
#else
      QStringLiteral("DejaVu Sans Mono"),
      QStringLiteral("Noto Sans Mono"),
      QStringLiteral("Liberation Mono"),
#endif
      systemFont.family(),
  }));
  font.setStyleHint(QFont::Monospace);
  font.setFixedPitch(true);
  font.setPointSizeF(13.0);
  configureForScreen(font);
  return font;
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

QStringList splitCssFamilyExpression(const QString& raw) {
  QStringList result;
  QString token;
  QChar quote;
  bool escaped = false;
  for (const QChar character : raw) {
    if (escaped) {
      token.append(character);
      escaped = false;
      continue;
    }
    if (character == '\\') {
      token.append(character);
      escaped = true;
      continue;
    }
    if (quote.isNull() && (character == QLatin1Char(',') || character == QLatin1Char('\n'))) {
      const QString value = token.trimmed();
      if (!value.isEmpty())
        result.append(value);
      token.clear();
      continue;
    }
    if ((character == QLatin1Char('\'') || character == QLatin1Char('"'))) {
      if (quote.isNull()) {
        quote = character;
      } else if (quote == character) {
        quote = {};
      }
    }
    token.append(character);
  }
  const QString value = token.trimmed();
  if (!value.isEmpty())
    result.append(value);
  return result;
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

QString fontFaceAliasKey(const QString& family, int weight, bool italic) {
  return family.toLower() + QChar(0x1f) + QString::number(weight) + '/' + (italic ? '1' : '0');
}

namespace {
QString matchingFace(const QHash<QString, QString>& aliases, const QString& family, int weight, bool italic) {
  if (const auto exact = aliases.constFind(fontFaceAliasKey(family, weight, italic)); exact != aliases.cend())
    return *exact;
  const auto prefix = family.toLower() + QChar(0x1f);
  QString result = aliases.value(family.toLower());
  auto best = std::tuple(2, 3, 1001);
  for (auto face = aliases.cbegin(); face != aliases.cend(); ++face) {
    if (!face.key().startsWith(prefix))
      continue;
    const auto descriptor = QStringView(face.key()).mid(prefix.size());
    const auto slash = descriptor.indexOf('/');
    if (slash < 0)
      continue;
    const int candidate = descriptor.left(slash).toInt();
    if (candidate < 1 || candidate > 1000)
      continue;
    const bool candidateItalic = descriptor.mid(slash + 1) == u"1";
    // CSS Fonts weight matching: 400..500 search upward to 500, then below
    // the requested weight, then above 500. Other weights search outward in
    // their preferred direction. Style matching precedes weight matching.
    const int group = weight >= 400 && weight <= 500
                          ? (candidate >= weight && candidate <= 500 ? 0
                             : candidate < weight                    ? 1
                                                                     : 2)
                          : (weight < 400 ? (candidate <= weight ? 0 : 1) : (candidate >= weight ? 0 : 1));
    const auto rank = std::tuple(int(candidateItalic != italic), group, qAbs(candidate - weight));
    if (rank < best) {
      best = rank;
      result = *face;
    }
  }
  return result;
}
} // namespace

QStringList cssFamilyList(const QString& raw, const QString& platformTail, const QHash<QString, QString>& aliases,
                          int weight, bool italic) {
  // Themes historically stored one family per newline, while Mermaid and
  // ordinary CSS use comma-separated stacks. Parse both forms here so every
  // renderer applies the same ordering and quote handling.
  QStringList requested = splitCssFamilyExpression(raw);

  // Register the deterministic application faces before taking the family
  // snapshot. Otherwise a first CSS lookup can cache a system-only list.
  bundledFallbackFamilies();
  const QStringList& availableFamilies = availableDocumentFamilies();
  QStringList out;
  for (const QString& token : requested) {
    const bool quoted =
        token.size() >= 2 && (token.front() == '\'' || token.front() == '"') && token.front() == token.back();
    QString family = token;
    if (quoted || token.contains('\\')) {
      const auto decoded = parseContentTokens(quoted ? token : '"' + token + '"');
      if (decoded.size() != 1)
        continue;
      family = decoded.front().text;
    }
    if (!quoted)
      family = family.simplified();
    if (family.isEmpty())
      continue;
    // @font-face: a CSS theme may declare `font-family: CascadiaCode` while the
    // font file's internal name (what QFontDatabase registers) is "Cascadia Code",
    // or `"LXGW WenKai"` whose internal name is 霞鹜文楷. Substitute the declared
    // alias with the registered name so the stack resolves to the bundled font.
    QString resolved = family;
    if (const QString alias = matchingFace(aliases, family, weight, italic); !alias.isEmpty()) {
      resolved = alias;
    } else if (const auto bundled = mermaid::MermaidFontRegistry::bundledFamily(family); !bundled.isEmpty()) {
      resolved = bundled;
    }
    if (!quoted &&
        (family.compare("serif", Qt::CaseInsensitive) == 0 || family.compare("sans-serif", Qt::CaseInsensitive) == 0 ||
         family.compare("monospace", Qt::CaseInsensitive) == 0))
      resolved = genericFamilyTail(family);
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
  for (QString family : splitCssFamilyExpression(platformTail)) {
    family = family.trimmed();
    if (family.compare(QStringLiteral("sans-serif"), Qt::CaseInsensitive) == 0) {
      family = sansFamily();
    } else if (family.compare(QStringLiteral("serif"), Qt::CaseInsensitive) == 0) {
      family = serifFamily();
    } else if (family.compare(QStringLiteral("monospace"), Qt::CaseInsensitive) == 0) {
      family = codeFamily();
    }
    const QString resolved = platformCssFamilyAlias(family, availableFamilies);
    if (!resolved.isEmpty() && !out.contains(resolved, Qt::CaseInsensitive))
      out << resolved;
  }
  return out;
}

QString documentFallbackTail() {
  const auto& bundled = bundledFallbackFamilies();
  QStringList families = bundled;
  const auto append = [&families](const QString& family) {
    if (!family.isEmpty() && !families.contains(family, Qt::CaseInsensitive))
      families.append(family);
  };
  append(sansFamily());
  append(serifFamily());
  append(codeFamily());
  // Explicit symbol faces also work with Qt's offscreen font database, whose
  // automatic fallback can miss emoji/status glyphs in otherwise valid text.
  for (const auto* name :
       {"Segoe UI Emoji", "Segoe UI Symbol", "Apple Color Emoji", "Noto Color Emoji", "Noto Sans Symbols 2"})
    append(availableFamilyNamed(QLatin1String(name), availableDocumentFamilies()));
  return families.join(QLatin1Char('\n'));
}

} // namespace muffin::font_rendering
