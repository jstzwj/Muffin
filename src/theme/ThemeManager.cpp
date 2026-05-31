#include "theme/ThemeManager.h"

namespace muffin {

ThemeManager::ThemeManager(QObject* parent) : QObject(parent) {}

QString ThemeManager::currentThemeName() const {
  return currentThemeName_;
}

RenderTheme ThemeManager::currentTheme(int zoomPercent) const {
  if (currentThemeName_ == QStringLiteral("newsprint")) {
    return RenderTheme::newsprint(zoomPercent);
  }
  if (currentThemeName_ == QStringLiteral("night")) {
    return RenderTheme::night(zoomPercent);
  }
  return RenderTheme::github(zoomPercent);
}

QStringList ThemeManager::availableThemes() const {
  return {
      QStringLiteral("github"),
      QStringLiteral("newsprint"),
      QStringLiteral("night"),
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
