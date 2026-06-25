#pragma once

#include "theme/RenderTheme.h"
#include "theme/ThemeDefinition.h"

#include <QHash>
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

  // Raw CSS source text of the current theme, for embedding into exported HTML
  // so the export carries the active theme. Re-reads from the theme's source
  // file (Qt resource for built-ins, the on-disk file for custom *.css themes).
  // Returns empty when there is no CSS source — i.e. a JSON-only custom theme
  // or a read failure — in which case callers fall back to a default stylesheet.
  QString currentThemeCss() const;

  bool setTheme(QString name);

  // Re-scan the user themes directory and merge any new *.json files in. Built-in
  // ids are authoritative: a custom file shadowing a built-in id is ignored. If
  // the active theme was a custom one that has since been removed, falls back to
  // github. Call after importing/deleting a theme so menus & dialogs refresh.
  void reloadCustomThemes();

  // Directory custom themes are read from / written to (AppDataLocation/themes).
  // Static — it depends only on the standard writable location, not on state.
  static QString themesDirectory();

  // Install a community CSS theme as a faithful multi-file mirror: copy the top
  // .css verbatim into `destDir` (preserving its @import), then copy every local
  // file it transitively references (@import'd base sheets + @font-face fonts +
  // url() images) next to it, preserving relative paths — so the theme resolves
  // identically to its source folder (e.g. my-theme.css + a my-theme/ folder
  // carrying the base CSS and .ttf fonts). Returns false only if the top file can't
  // be read. Static + destDir-parametrised so it is unit-testable with a temp
  // dir (no GUI). Replaces the former @import-inlining import, which flattened
  // the theme to one file and broke the font/image urls in its @import'd base.
  static bool installCssTheme(const QString& srcPath, const QString& destDir);

signals:
  void themeChanged(QString name);
  void themesChanged();

private:
  void loadDefinitions();

  QString currentThemeName_ = QStringLiteral("github");
  std::vector<ThemeDefinition> definitions_;
  // id → absolute source path of the theme's *.css (":/themes/<id>.css" for
  // built-ins, the disk file for custom *.css themes). Absent for JSON-only
  // custom themes. Populated by loadDefinitions(); used by currentThemeCss().
  QHash<QString, QString> cssSourcePaths_;
};

}  // namespace muffin
