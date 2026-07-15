#include "mermaid/MermaidFontRegistry.h"

#include <QFont>
#include <QFontDatabase>
#include <QSet>

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
  if (familyExpression.contains(QStringLiteral("Noto Sans"), Qt::CaseInsensitive))
    font.setFamilies(familyStack());
}

}  // namespace muffin::mermaid
