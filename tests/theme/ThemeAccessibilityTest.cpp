#include "editor/VirtualSourceEdit.h"
#include "theme/ChromeStyleSheet.h"
#include "theme/RenderTheme.h"
#include "theme/ThemeDefinition.h"

#include "../TestUtils.h"

#include <QCoreApplication>

#include <cmath>

using namespace muffin;

namespace {

qreal linearChannel(qreal channel) {
  return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}

qreal relativeLuminance(const QColor& color) {
  return 0.2126 * linearChannel(color.redF()) +
         0.7152 * linearChannel(color.greenF()) +
         0.0722 * linearChannel(color.blueF());
}

qreal contrastRatio(const QColor& a, const QColor& b) {
  const qreal lighter = qMax(relativeLuminance(a), relativeLuminance(b));
  const qreal darker = qMin(relativeLuminance(a), relativeLuminance(b));
  return (lighter + 0.05) / (darker + 0.05);
}

void testBuiltInSourceGuttersStayIntegratedAndReadable() {
  for (const ThemeDefinition& definition : ThemeDefinition::builtIns()) {
    const SourceEditorColors colors = SourceEditorColors::fromTheme(
        RenderTheme::fromDefinition(definition));
    require(contrastRatio(colors.background, colors.gutterBackground) < 1.25,
            definition.id + QStringLiteral(": source gutter should remain a subtle page tone"));
    const qreal lineNumberContrast = contrastRatio(colors.lineNumber, colors.gutterBackground);
    require(lineNumberContrast >= 3.0,
            definition.id +
                QStringLiteral(": source line numbers need at least 3:1 contrast (actual %1, %2 on %3)")
                    .arg(lineNumberContrast, 0, 'f', 2)
                    .arg(colors.lineNumber.name(), colors.gutterBackground.name()));
  }
}

void testNightMenusUsePrimaryChromeText() {
  const ThemeDefinition definition = ThemeDefinition::builtIn(QStringLiteral("night")).value();
  require(contrastRatio(definition.colors.chromeText, definition.colors.chromeBackground) >= 4.5,
          QStringLiteral("Night chrome text needs at least 4.5:1 contrast"));
  require(contrastRatio(definition.colors.chromeText, definition.colors.surface) >= 4.5,
          QStringLiteral("Night popup-menu text needs at least 4.5:1 contrast"));

  const QString text = definition.colors.chromeText.name(QColor::HexArgb);
  const QString sheet = mainWindowStyleSheet(definition);
  require(sheet.contains(
              QStringLiteral("QMenuBar::item { padding: 4px 9px; background: transparent; color: %1;").arg(text)),
          QStringLiteral("menu-bar items should explicitly use chromeText"));
  require(sheet.contains(
              QStringLiteral("QMenu::item { padding: 5px 34px 5px 16px; color: %1;").arg(text)),
          QStringLiteral("popup-menu items should explicitly use chromeText"));
}

void testGitHubMenusUsePrimaryInk() {
  const ThemeDefinition definition = ThemeDefinition::builtIn(QStringLiteral("github")).value();
  require(definition.colors.chromeText.name(QColor::HexRgb) == QStringLiteral("#333333"),
          QStringLiteral("GitHub menus should use primary ink, not muted --control-text-color"));
  require(definition.colors.chromeMuted.name(QColor::HexRgb) == QStringLiteral("#777777"),
          QStringLiteral("GitHub secondary controls should retain --control-text-color"));
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  runTest("testBuiltInSourceGuttersStayIntegratedAndReadable",
          testBuiltInSourceGuttersStayIntegratedAndReadable);
  runTest("testNightMenusUsePrimaryChromeText", testNightMenusUsePrimaryChromeText);
  runTest("testGitHubMenusUsePrimaryInk", testGitHubMenusUsePrimaryInk);
  return 0;
}
