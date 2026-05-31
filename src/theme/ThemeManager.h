#pragma once

#include "theme/RenderTheme.h"

#include <QObject>
#include <QStringList>

namespace muffin {

class ThemeManager final : public QObject {
  Q_OBJECT

public:
  explicit ThemeManager(QObject* parent = nullptr);

  QString currentThemeName() const;
  RenderTheme currentTheme(int zoomPercent = 100) const;
  QStringList availableThemes() const;

  bool setTheme(QString name);

signals:
  void themeChanged(QString name);

private:
  QString currentThemeName_ = QStringLiteral("github");
};

}  // namespace muffin
