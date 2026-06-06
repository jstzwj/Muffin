#include "theme/ThemeManager.h"

namespace muffin {

ThemeManager::ThemeManager(QObject* parent) : QObject(parent) {}

QString ThemeManager::currentThemeName() const {
  return currentThemeName_;
}

RenderTheme ThemeManager::currentTheme(int zoomPercent, int fontSizePx) const {
  RenderTheme theme;
  if (currentThemeName_ == QStringLiteral("newsprint")) {
    theme = RenderTheme::newsprint(zoomPercent);
  } else if (currentThemeName_ == QStringLiteral("night")) {
    theme = RenderTheme::night(zoomPercent);
  } else if (currentThemeName_ == QStringLiteral("pixyll")) {
    theme = RenderTheme::pixyll(zoomPercent);
  } else if (currentThemeName_ == QStringLiteral("whitey")) {
    theme = RenderTheme::whitey(zoomPercent);
  } else {
    theme = RenderTheme::github(zoomPercent);
  }
  theme.setFontSizePx(fontSizePx);
  return theme;
}

QStringList ThemeManager::availableThemes() const {
  return {
      QStringLiteral("github"),
      QStringLiteral("newsprint"),
      QStringLiteral("night"),
      QStringLiteral("pixyll"),
      QStringLiteral("whitey"),
  };
}

bool ThemeManager::setTheme(QString name) {
  name = name.toLower();
  if (!availableThemes().contains(name)) {
    return false;
  }
  if (currentThemeName_ == name) {
    return true;
  }
  currentThemeName_ = std::move(name);
  emit themeChanged(currentThemeName_);
  return true;
}

}  // namespace muffin
