#include "theme/ThemeManager.h"

#include "theme/CssThemeParser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>

namespace muffin {

namespace {

// User-supplied custom themes live here as *.json (one per file, id = file stem).
// Created on demand when the user imports a theme.
QString userThemesDir() {
  return ThemeManager::themesDirectory();
}

// True for a @import/url target that points at a local file (not http(s)/data/
// protocol-relative). Only local @imports can be inlined for a self-contained
// export; remote ones are kept verbatim.
bool isLocalCssReference(const QString& url) {
  const QString u = url.trimmed();
  if (u.isEmpty()) {
    return false;
  }
  if (u.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
      || u.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)
      || u.startsWith(QStringLiteral("data:"), Qt::CaseInsensitive)
      || u.startsWith(QStringLiteral("//"))) {
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
      QStringLiteral(
          "@import\\s+(?:url\\(\\s*)?['\"]?(?<url>[^'\"\\)\\s]+)['\"]?\\s*\\)?\\s*(?<media>[^;]*);"),
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
      out += m.captured(0);  // keep remote/data @import verbatim
      continue;
    }
    const QString absPath = QDir::cleanPath(baseDir + QLatin1Char('/') + url);
    if (visited.contains(absPath)) {
      continue;  // already inlined (or cycle) — drop the duplicate @import
    }
    visited.insert(absPath);
    QFile sub(absPath);
    if (!sub.open(QIODevice::ReadOnly | QIODevice::Text)) {
      continue;  // unreadable base sheet — drop the @import rather than leave a broken ref
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

}  // namespace

QString ThemeManager::themesDirectory() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
         + QStringLiteral("/themes");
}

bool ThemeManager::installCssTheme(const QString& srcPath, const QString& destDir) {
  QFile src(srcPath);
  if (!src.open(QIODevice::ReadOnly | QIODevice::Text)) { return false; }
  const QString text = QString::fromUtf8(src.readAll());
  src.close();

  QDir().mkpath(destDir);
  const QString rootDir = QDir::cleanPath(QFileInfo(srcPath).absolutePath());
  const QDir destRoot(destDir);

  // Copy the top file verbatim — keep its @import, do NOT inline (inlining
  // would drop the folder structure and break @font-face url() in the base).
  const QString destFile = destRoot.absoluteFilePath(QFileInfo(srcPath).fileName().toLower());
  if (QFileInfo::exists(destFile)) { QFile::remove(destFile); }
  QFile::copy(srcPath, destFile);

  // Mirror every local file the theme transitively references (@import'd base
  // sheets + @font-face fonts + url() images), preserving each one's path
  // relative to the source theme's folder. Skip anything that escapes that
  // folder (..) so unrelated files aren't dragged in.
  const QStringList resources = CssThemeParser::localResourcePaths(text, rootDir);
  for (const QString& absPath : resources) {
    const QString cleaned = QDir::cleanPath(absPath);
    const QString rel = QDir(rootDir).relativeFilePath(cleaned);
    if (rel.startsWith(QStringLiteral(".."))) { continue; }  // outside the theme folder
    const QString target = destRoot.absoluteFilePath(rel);
    QDir().mkpath(QFileInfo(target).absolutePath());
    if (QFileInfo::exists(target)) { QFile::remove(target); }
    QFile::copy(cleaned, target);
  }
  return true;
}

ThemeManager::ThemeManager(QObject* parent) : QObject(parent) {
  loadDefinitions();
}

void ThemeManager::loadDefinitions() {
  definitions_.clear();
  // Built-ins first, in their canonical display order.
  definitions_ = ThemeDefinition::builtIns();
  cssSourcePaths_.clear();
  for (const ThemeDefinition& d : definitions_) {
    if (d.isBuiltIn) {
      cssSourcePaths_.insert(d.id, QStringLiteral(":/themes/%1.css").arg(d.id));
    }
  }

  // Then any user-supplied themes from the themes directory — both native *.json
  // and community-CSS *.css. Built-in ids win: a custom file whose stem
  // matches a built-in is skipped so the built-ins stay canonical.
  const QDir dir(userThemesDir());
  if (dir.exists()) {
    QSet<QString> known;
    for (const auto& d : definitions_) {
      known.insert(d.id);
    }
    const QStringList files =
        dir.entryList({QStringLiteral("*.json"), QStringLiteral("*.css")}, QDir::Files);
    for (const QString& file : files) {
      const QString id = QFileInfo(file).baseName().toLower();
      if (known.contains(id)) {
        continue;  // don't shadow a built-in (or an already-loaded stem)
      }
      ThemeDefinition d;
      if (file.endsWith(QStringLiteral(".css"), Qt::CaseInsensitive)) {
        const QString absPath = dir.absoluteFilePath(file);
        d = ThemeDefinition::fromCss(absPath, id);
        cssSourcePaths_.insert(id, absPath);  // record real path for currentThemeCss()
      } else {
        QFile f(dir.absoluteFilePath(file));
        if (!f.open(QIODevice::ReadOnly)) {
          continue;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject()) {
          continue;
        }
        d = ThemeDefinition::fromJson(doc.object(), id);
      }
      if (d.valid()) {
        definitions_.push_back(std::move(d));
        known.insert(id);
      }
    }
  }
}

QString ThemeManager::currentThemeName() const {
  return currentThemeName_;
}

QString ThemeManager::currentThemeCss() const {
  auto it = cssSourcePaths_.constFind(currentThemeName_);
  if (it == cssSourcePaths_.constEnd()) {
    return {};  // JSON-only custom theme (or unknown) — no CSS source to embed.
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

RenderTheme ThemeManager::currentTheme(int zoomPercent, int fontSizePx) const {
  // Build the editor theme from the same definition the chrome reads, so a
  // custom (JSON) theme drives the document through the exact same path as a
  // built-in. fromDefinition reproduces the five built-in factories bit-for-bit.
  return RenderTheme::fromDefinition(definition(currentThemeName_), zoomPercent, fontSizePx);
}

QStringList ThemeManager::availableThemes() const {
  QStringList names;
  names.reserve(int(definitions_.size()));
  for (const auto& d : definitions_) {
    names << d.id;
  }
  return names;
}

const std::vector<ThemeDefinition>& ThemeManager::definitions() const {
  return definitions_;
}

ThemeDefinition ThemeManager::definition(const QString& name) const {
  const QString lower = name.toLower();
  for (const auto& d : definitions_) {
    if (d.id == lower) {
      return d;
    }
  }
  // Unknown name — fall back to github rather than an empty definition.
  return ThemeDefinition::builtIn(QStringLiteral("github")).value();
}

ThemeDefinition ThemeManager::currentDefinition() const {
  return definition(currentThemeName_);
}

bool ThemeManager::setTheme(QString name) {
  name = name.toLower();
  bool found = false;
  for (const auto& d : definitions_) {
    if (d.id == name) {
      found = true;
      break;
    }
  }
  if (!found) {
    return false;
  }
  if (currentThemeName_ == name) {
    return true;
  }
  currentThemeName_ = std::move(name);
  emit themeChanged(currentThemeName_);
  return true;
}

void ThemeManager::reloadCustomThemes() {
  loadDefinitions();
  // If the active theme was a custom one that no longer exists, fall back.
  bool currentStillKnown = false;
  for (const auto& d : definitions_) {
    if (d.id == currentThemeName_) {
      currentStillKnown = true;
      break;
    }
  }
  if (!currentStillKnown) {
    currentThemeName_ = QStringLiteral("github");
  }
  emit themesChanged();
}

}  // namespace muffin
