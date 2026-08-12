#include "mermaid/MermaidFontRegistry.h"

#include <QFont>
#include <QFontDatabase>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

static void initMermaidFontsResource() {
  Q_INIT_RESOURCE(mermaid_fonts);
}

namespace muffin::mermaid {
namespace {

QStringList& loadedFamilies() {
  static QStringList families;
  return families;
}

}  // namespace

void MermaidFontRegistry::ensureLoaded() {
  static const bool loaded = [] {
    initMermaidFontsResource();
    const QStringList resources = {
        QStringLiteral(":/mermaid/fonts/NotoSans-Regular.ttf"),
        QStringLiteral(":/mermaid/fonts/NotoSansCJKsc-Regular.otf"),
        QStringLiteral(":/mermaid/fonts/NotoSansArabic-Regular.ttf"),
        QStringLiteral(":/mermaid/fonts/NotoSansHebrew-Regular.ttf"),
    };
    QSet<QString> seen;
    QStringList& families = loadedFamilies();
    for (const QString& resource : resources) {
      const int id = QFontDatabase::addApplicationFont(resource);
      if (id < 0) qFatal("Failed to register bundled Mermaid font: %s", qPrintable(resource));
      for (const QString& family : QFontDatabase::applicationFontFamilies(id)) {
        if (!seen.contains(family)) {
          seen.insert(family);
          families.append(family);
        }
      }
    }
#ifdef Q_OS_WIN
    // Mermaid's Event Modeling helper measures with the browser's fixed CSS
    // stack headed by Trebuchet MS. Conan Qt does not discover Windows system
    // fonts in the offscreen/runtime bundle, while Chromium does. Register the
    // installed faces so QFont's CSS fallback list resolves the same face. Do
    // not add them to familyStack(): Noto remains Muffin's deterministic root
    // font and these faces are selected only when a diagram asks for them.
    const QString windowsFonts =
        qEnvironmentVariable("WINDIR", QStringLiteral("C:/Windows")) +
        QStringLiteral("/Fonts/");
    const QStringList compatibilityFaces = {
        QStringLiteral("trebuc.ttf"), QStringLiteral("trebucbd.ttf"),
        QStringLiteral("trebucit.ttf"), QStringLiteral("trebucbi.ttf")};
    for (const QString& face : compatibilityFaces) {
      const QString path = windowsFonts + face;
      if (QFileInfo::exists(path)) QFontDatabase::addApplicationFont(path);
    }
#endif
    return true;
  }();
  (void)loaded;
}

QString MermaidFontRegistry::primaryFamily() {
  ensureLoaded();
  return QStringLiteral("Noto Sans");
}

QStringList MermaidFontRegistry::familyStack() {
  ensureLoaded();
  return loadedFamilies();
}

QString MermaidFontRegistry::cssFamilyStack() {
  return QStringLiteral("\"Noto Sans\", \"Noto Sans CJK SC\", "
                        "\"Noto Sans Arabic\", \"Noto Sans Hebrew\", sans-serif");
}

void MermaidFontRegistry::configureFont(QFont& font, const QString& familyExpression) {
  ensureLoaded();
  if (!familyExpression.contains(QStringLiteral("Noto Sans"),
                                 Qt::CaseInsensitive))
    return;

  QStringList ordered;
  for (QString family : familyExpression.split(QLatin1Char(','),
                                                Qt::SkipEmptyParts)) {
    family = family.trimmed();
    if (family.size() >= 2 &&
        ((family.front() == QLatin1Char('"') &&
          family.back() == QLatin1Char('"')) ||
         (family.front() == QLatin1Char('\'') &&
          family.back() == QLatin1Char('\'')))) {
      family = family.mid(1, family.size() - 2);
    }
    const auto known = std::find_if(
        loadedFamilies().cbegin(), loadedFamilies().cend(),
        [&](const QString& candidate) {
          return candidate.compare(family, Qt::CaseInsensitive) == 0;
        });
    if (known != loadedFamilies().cend() && !ordered.contains(*known))
      ordered.append(*known);
  }
  for (const QString& family : loadedFamilies())
    if (!ordered.contains(family)) ordered.append(family);
  if (!ordered.isEmpty()) font.setFamilies(ordered);
}

}  // namespace muffin::mermaid
