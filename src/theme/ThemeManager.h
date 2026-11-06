#pragma once

#include "Export.h"
#include "theme/RenderTheme.h"
#include "theme/ThemeDefinition.h"

#include <QHash>
#include <QCache>
#include <QObject>
#include <QStringList>

#include <vector>

namespace muffin {

class MUFFIN_UI_EXPORT ThemeManager final : public QObject {
  Q_OBJECT

public:
  explicit ThemeManager(QObject* parent = nullptr);
  explicit ThemeManager(const QString& themeDirectory, QObject* parent = nullptr);

  QString currentThemeName() const;
  RenderTheme currentTheme(int zoomPercent = 100, int fontSizePx = 16, TypographyOverrides typography = {}) const;
  QStringList availableThemes() const;

  // Metadata only: enumerating menus/preferences never loads a theme.
  // Custom labels use the filename until their authored definition is loaded.
  const std::vector<ThemeDescriptor>& themes() const;

  // The unified theme definition (document + chrome colours) for a given name,
  // or the github default if unknown. Loads on demand; setTheme() and
  // reloadCustomThemes() revalidate content digests before reusing a custom
  // definition. This is the single source the chrome,
  // sidebar, dialogs and editor (via RenderTheme::fromDefinition) read from.
  ThemeDefinition definition(const QString& name) const;
  ThemeDefinition currentDefinition() const;

  // Raw CSS source text of the current theme, for embedding into exported HTML
  // so the export carries the active theme. Re-reads from the theme's source
  // file (Qt resource for built-ins, the on-disk file for custom *.css themes).
  // Returns empty when there is no CSS source — i.e. a JSON-only custom theme
  // or a read failure — in which case callers fall back to a default stylesheet.
  QString currentThemeCss() const;

  // Synchronous selection notifications share the definition verified here.
  // Independent/queued reads still revalidate the complete resource contents.
  bool setTheme(QString name);

  // Re-scan *.css/*.json filenames and revalidate the active theme. Built-in
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
  ThemeDefinition loadDefinition(const QString& id, bool revalidate, bool* changed = nullptr) const;
  bool selectTheme(QString name);
  struct DefinitionReadScope;
  DefinitionReadScope* readScope_ = nullptr;

  QString currentThemeName_ = QStringLiteral("github");
  QString themeDirectory_;
  mutable std::vector<ThemeDescriptor> themes_;
  QHash<QString, QString> sourcePaths_;
  struct CachedTheme {
    ThemeDefinition definition;
    QHash<QString, QByteArray> sources;
  };
  // Bounded, process-local LRU. Runtime Qt/font objects are not serialized.
  mutable QCache<QString, CachedTheme> loaded_{8};
  struct PreparedTheme {
    std::shared_ptr<const CssThemeSheet> source;
    quint64 revision;
    RenderTheme theme;
  };
  mutable QCache<QString, PreparedTheme> prepared_{8};
  // id → absolute source path of the theme's *.css (":/themes/<id>.css" for
  // built-ins, the disk file for custom *.css themes). Absent for JSON-only
  // custom themes. Populated by loadDefinitions(); used by currentThemeCss().
  QHash<QString, QString> cssSourcePaths_;
};

} // namespace muffin
