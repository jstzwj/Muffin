#pragma once

#include "theme/RenderTheme.h"
#include "theme/ThemeDefinition.h"

#include <QObject>
#include <QStringList>

#include <vector>

namespace muffin {

class ThemeManager final : public QObject {
  Q_OBJECT

public:
  explicit ThemeManager(QObject* parent = nullptr);

  QString currentThemeName() const;
  RenderTheme currentTheme(int zoomPercent = 100, int fontSizePx = 16) const;
  QStringList availableThemes() const;

  // Every definition the manager knows about: built-ins first (in display
  // order github/newsprint/night/pixyll/whitey), then user-loaded custom themes
  // read from the themes directory. Stable unless reloadCustomThemes() runs.
  const std::vector<ThemeDefinition>& definitions() const;

  // The unified theme definition (document + chrome colours) for a given name,
  // or the github default if unknown. This is the single source the chrome,
  // sidebar, dialogs and editor (via RenderTheme::fromDefinition) read from.
  ThemeDefinition definition(const QString& name) const;
  ThemeDefinition currentDefinition() const;

  bool setTheme(QString name);

  // Re-scan the user themes directory and merge any new *.json files in. Built-in
  // ids are authoritative: a custom file shadowing a built-in id is ignored. If
  // the active theme was a custom one that has since been removed, falls back to
  // github. Call after importing/deleting a theme so menus & dialogs refresh.
  void reloadCustomThemes();

  // Directory custom themes are read from / written to (AppDataLocation/themes).
  // Static — it depends only on the standard writable location, not on state.
  static QString themesDirectory();

signals:
  void themeChanged(QString name);
  void themesChanged();

private:
  void loadDefinitions();

  QString currentThemeName_ = QStringLiteral("github");
  std::vector<ThemeDefinition> definitions_;
};

}  // namespace muffin
