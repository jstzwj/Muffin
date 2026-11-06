#include "theme/ThemeManager.h"
#include "diagnostics/ScopedPerfProbe.h"

#include "theme/CssThemeParser.h"
#include "theme/ThemeResourceDigest.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include "io/ApplicationPaths.h"
#include <QStringList>

namespace muffin {

namespace {
Q_LOGGING_CATEGORY(managerLoadPerf, "muffin.perf.detail", QtWarningMsg)

QHash<QString, QByteArray> sourceManifest(const QString& path) {
  diag::ScopedPerfProbe perf("theme.sourceManifest", managerLoadPerf());
  QHash<QString, QByteArray> sources;
  sources.insert(path, themeResourceDigest(path));
  if (path.endsWith(QLatin1String(".css"), Qt::CaseInsensitive)) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly))
      for (const auto& dependency :
           CssThemeParser::localResourcePaths(QString::fromUtf8(file.readAll()), QFileInfo(path).absolutePath(), true))
        sources.insert(dependency, themeResourceDigest(dependency));
  }
  return sources;
}

bool sourcesUnchanged(const QHash<QString, QByteArray>& sources) {
  diag::ScopedPerfProbe perf("theme.revalidateSources", managerLoadPerf());
  for (auto it = sources.cbegin(); it != sources.cend(); ++it)
    if (themeResourceDigest(it.key()) != it.value())
      return false;
  return true;
}

QString themeLabel(const QString& id) {
  QString label = id;
  bool capitalize = true;
  for (auto& c : label) {
    if (c == QLatin1Char('-') || c == QLatin1Char('_'))
      c = QLatin1Char(' ');
    if (capitalize)
      c = c.toUpper();
    capitalize = c.isSpace();
  }
  return label;
}

// True for a @import/url target that points at a local file (not http(s)/data/
// protocol-relative). Only local @imports can be inlined for a self-contained
// export; remote ones are kept verbatim.
bool isLocalCssReference(const QString& url) {
  const QString u = url.trimmed();
  if (u.isEmpty()) {
    return false;
  }
  if (u.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) ||
      u.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive) ||
      u.startsWith(QStringLiteral("data:"), Qt::CaseInsensitive) || u.startsWith(QStringLiteral("//"))) {
    return false;
  }
  return true;
}

// Recursively inlines local @import statements so the returned CSS is
// self-contained — multi-file themes (e.g. phycat: top file does
// `@import url(./phycat/phycat.light.css)`; the real styles live in that base
// sheet) otherwise lose their base styles when exported as a single HTML file,
// since the relative @import resolves against the export location, not the
// theme folder. Remote/data @imports and url() assets (fonts, background
// images) are left untouched — assets won't resolve from the export location
// and fonts fall back. `visited` guards against cycles/duplicate inclusion.
QString flattenCssImports(const QString& text, const QString& baseDir, QSet<QString>& visited) {
  static const QRegularExpression re(
      QStringLiteral("@import\\s+(?:url\\(\\s*)?['\"]?(?<url>[^'\"\\)\\s]+)['\"]?\\s*\\)?\\s*(?<media>[^;]*);"),
      QRegularExpression::CaseInsensitiveOption);
  QString out;
  out.reserve(text.size());
  qsizetype lastEnd = 0;
  auto it = re.globalMatch(text);
  while (it.hasNext()) {
    const QRegularExpressionMatch m = it.next();
    out += text.mid(lastEnd, m.capturedStart() - lastEnd);
    lastEnd = m.capturedEnd();
    const QString url = m.captured(QStringLiteral("url"));
    const QString media = m.captured(QStringLiteral("media")).trimmed();
    if (!isLocalCssReference(url)) {
      out += m.captured(0); // keep remote/data @import verbatim
      continue;
    }
    const QString absPath = QDir::cleanPath(baseDir + QLatin1Char('/') + url);
    if (visited.contains(absPath)) {
      continue; // already inlined (or cycle) — drop the duplicate @import
    }
    visited.insert(absPath);
    QFile sub(absPath);
    if (!sub.open(QIODevice::ReadOnly | QIODevice::Text)) {
      continue; // unreadable base sheet — drop the @import rather than leave a broken ref
    }
    const QString subText = QString::fromUtf8(sub.readAll());
    const QString inlined = flattenCssImports(subText, QFileInfo(absPath).absolutePath(), visited);
    if (!media.isEmpty()) {
      // @import with a media query → scope the inlined rules to that media.
      out += QStringLiteral("@media %1 {\n").arg(media) + inlined + QStringLiteral("\n}\n");
    } else {
      out += inlined;
      out += QLatin1Char('\n');
    }
  }
  out += text.mid(lastEnd);
  return out;
}

} // namespace

struct ThemeManager::DefinitionReadScope {
  explicit DefinitionReadScope(ThemeManager& manager) : owner(manager), previous(manager.readScope_) {
    manager.readScope_ = this;
  }
  ~DefinitionReadScope() { owner.readScope_ = previous; }
  DefinitionReadScope(const DefinitionReadScope&) = delete;
  DefinitionReadScope& operator=(const DefinitionReadScope&) = delete;
  ThemeManager& owner;
  DefinitionReadScope* previous;
  QHash<QString, ThemeDefinition> definitions;
};

QString ThemeManager::themesDirectory() {
  return ApplicationPaths::dataDirectory() + QStringLiteral("/themes");
}

bool ThemeManager::installCssTheme(const QString& srcPath, const QString& destDir) {
  QFile src(srcPath);
  if (!src.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return false;
  }
  const QString text = QString::fromUtf8(src.readAll());
  src.close();

  QDir().mkpath(destDir);
  const QString rootDir = QDir::cleanPath(QFileInfo(srcPath).absolutePath());
  const QDir destRoot(destDir);

  // Copy the top file verbatim — keep its @import, do NOT inline (inlining
  // would drop the folder structure and break @font-face url() in the base).
  const QString destFile = destRoot.absoluteFilePath(QFileInfo(srcPath).fileName().toLower());
  if (QFileInfo::exists(destFile)) {
    QFile::remove(destFile);
  }
  QFile::copy(srcPath, destFile);

  // Mirror every local file the theme transitively references (@import'd base
  // sheets + @font-face fonts + url() images), preserving each one's path
  // relative to the source theme's folder. Skip anything that escapes that
  // folder (..) so unrelated files aren't dragged in.
  const QStringList resources = CssThemeParser::localResourcePaths(text, rootDir);
  for (const QString& absPath : resources) {
    const QString cleaned = QDir::cleanPath(absPath);
    const QString rel = QDir(rootDir).relativeFilePath(cleaned);
    if (rel.startsWith(QStringLiteral(".."))) {
      continue;
    } // outside the theme folder
    const QString target = destRoot.absoluteFilePath(rel);
    QDir().mkpath(QFileInfo(target).absolutePath());
    if (QFileInfo::exists(target)) {
      QFile::remove(target);
    }
    QFile::copy(cleaned, target);
  }
  return true;
}

ThemeManager::ThemeManager(QObject* parent) : ThemeManager(themesDirectory(), parent) {
}

ThemeManager::ThemeManager(const QString& themeDirectory, QObject* parent)
    : QObject(parent), themeDirectory_(themeDirectory) {
  loadDefinitions();
}

void ThemeManager::loadDefinitions() {
  prepared_.clear();
  // Only discover names here. CSS parsing, JSON decoding and font registration
  // happen on selection, never while enumerating a directory/menu.
  themes_ = ThemeDefinition::builtInDescriptors();
  sourcePaths_.clear();
  cssSourcePaths_.clear();
  for (const auto& d : themes_)
    cssSourcePaths_.insert(d.id, QStringLiteral(":/themes/%1.css").arg(d.id));

  // Then any user-supplied themes from the themes directory — both native *.json
  // and community-CSS *.css. Built-in ids win: a custom file whose stem
  // matches a built-in is skipped so the built-ins stay canonical.
  const QDir dir(themeDirectory_);
  if (dir.exists()) {
    QSet<QString> known;
    for (const auto& d : themes_) {
      known.insert(d.id);
    }
    const QStringList files = dir.entryList({QStringLiteral("*.json"), QStringLiteral("*.css")}, QDir::Files);
    for (const QString& file : files) {
      const QString id = QFileInfo(file).baseName().toLower();
      if (known.contains(id)) {
        continue; // don't shadow a built-in (or an already-loaded stem)
      }
      const QString absPath = dir.absoluteFilePath(file);
      sourcePaths_.insert(id, absPath);
      if (file.endsWith(QStringLiteral(".css"), Qt::CaseInsensitive)) {
        cssSourcePaths_.insert(id, absPath); // record real path for currentThemeCss()
      }
      const auto* cached = loaded_.object(id);
      themes_.push_back({id, cached ? cached->definition.label : themeLabel(id), false});
      known.insert(id);
    }
  }
  for (const auto& id : loaded_.keys())
    if (!sourcePaths_.contains(id) || !loaded_.object(id)->sources.contains(sourcePaths_.value(id)))
      loaded_.remove(id);
}

QString ThemeManager::currentThemeName() const {
  return currentThemeName_;
}

QString ThemeManager::currentThemeCss() const {
  auto it = cssSourcePaths_.constFind(currentThemeName_);
  if (it == cssSourcePaths_.constEnd()) {
    return {}; // JSON-only custom theme (or unknown) — no CSS source to embed.
  }
  QFile f(it.value());
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return {};
  }
  const QString text = QString::fromUtf8(f.readAll());
  // Inline local @imports so multi-file themes export as one self-contained
  // stylesheet (their base sheets otherwise don't resolve from the export path).
  QSet<QString> visited;
  visited.insert(QDir::cleanPath(it.value()));
  return flattenCssImports(text, QFileInfo(it.value()).absolutePath(), visited);
}

RenderTheme ThemeManager::currentTheme(int zoomPercent, int fontSizePx, TypographyOverrides typography) const {
  // Build the editor theme from the same definition the chrome reads, so a
  // custom (JSON) theme drives the document through the exact same path as a
  // built-in. fromDefinition reproduces the five built-in factories bit-for-bit.
  const auto d = definition(currentThemeName_);
  const auto revision = d.sourceSheet ? d.sourceSheet->revision() : 0;
  const auto key = currentThemeName_ + '/' + QString::number(zoomPercent) + '/' + QString::number(fontSizePx);
  auto* cached = prepared_.object(key);
  if (!cached || cached->source != d.sourceSheet || cached->revision != revision) {
    cached = new PreparedTheme{d.sourceSheet, revision, RenderTheme::fromDefinition(d, zoomPercent, fontSizePx)};
    prepared_.insert(key, cached);
  }
  auto result = cached->theme.detachedPrototype();
  result.setTypographyOverrides(std::move(typography));
  return result;
}

QStringList ThemeManager::availableThemes() const {
  QStringList names;
  names.reserve(int(themes_.size()));
  for (const auto& d : themes_) {
    names << d.id;
  }
  return names;
}

const std::vector<ThemeDescriptor>& ThemeManager::themes() const {
  return themes_;
}

ThemeDefinition ThemeManager::loadDefinition(const QString& id, bool revalidate, bool* changed) const {
  if (changed)
    *changed = false;
  if (readScope_) {
    const auto found = readScope_->definitions.constFind(id);
    if (found != readScope_->definitions.cend())
      return *found;
  }
  const auto remember = [&](ThemeDefinition definition) {
    if (readScope_)
      readScope_->definitions.insert(id, definition);
    return definition;
  };
  const auto source = sourcePaths_.constFind(id);
  if (source == sourcePaths_.cend())
    return remember(ThemeDefinition::builtIn(id).value_or(ThemeDefinition{}));
  if (const auto* cached = loaded_.object(id); cached && (!revalidate || sourcesUnchanged(cached->sources)))
    return remember(cached->definition);
  loaded_.remove(id);
  prepared_.clear();
  if (changed)
    *changed = true;
  const auto sources = sourceManifest(*source);
  ThemeDefinition d;
  if (source->endsWith(QLatin1String(".css"), Qt::CaseInsensitive)) {
    d = ThemeDefinition::fromCss(*source, id);
  } else {
    QFile file(*source);
    if (file.open(QIODevice::ReadOnly)) {
      const auto json = QJsonDocument::fromJson(file.readAll());
      if (json.isObject())
        d = ThemeDefinition::fromJson(json.object(), id);
    }
  }
  if (!d.valid())
    return remember(std::move(d));
  for (auto& descriptor : themes_)
    if (descriptor.id == id)
      descriptor.label = d.label;
  // If an editor replaced a file during loading, don't cache a definition
  // under a manifest from a different version. The next request retries.
  if (sourcesUnchanged(sources))
    loaded_.insert(id, new CachedTheme{d, sources});
  return remember(std::move(d));
}

ThemeDefinition ThemeManager::definition(const QString& name) const {
  // Public reads revalidate contents, except inside a synchronous selection
  // operation: its editor/chrome consumers share the same verified snapshot.
  // The scope ends before returning to the caller/event loop, so the next
  // independent read still observes same-size, same-timestamp file changes.
  auto d = loadDefinition(name.toLower(), true);
  if (d.valid())
    return d;
  // Unknown name — fall back to github rather than an empty definition.
  return ThemeDefinition::builtIn(QStringLiteral("github")).value();
}

ThemeDefinition ThemeManager::currentDefinition() const {
  return definition(currentThemeName_);
}

bool ThemeManager::setTheme(QString name) {
  DefinitionReadScope operation(*this);
  return selectTheme(std::move(name));
}

bool ThemeManager::selectTheme(QString name) {
  name = name.toLower();
  bool found = false;
  for (const auto& d : themes_) {
    if (d.id == name) {
      found = true;
      break;
    }
  }
  if (!found) {
    return false;
  }
  bool changed = false;
  if (!loadDefinition(name, true, &changed).valid())
    return false;
  if (currentThemeName_ == name && !changed) {
    return true;
  }
  currentThemeName_ = std::move(name);
  emit themeChanged(currentThemeName_);
  return true;
}

void ThemeManager::reloadCustomThemes() {
  DefinitionReadScope operation(*this);
  loadDefinitions();
  // If the active theme was a custom one that no longer exists, fall back.
  if (!selectTheme(currentThemeName_))
    selectTheme(QStringLiteral("github"));
  emit themesChanged();
}

} // namespace muffin
