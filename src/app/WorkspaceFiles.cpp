#include "app/WorkspaceFiles.h"
#include "app/SearchSettings.h"
#include "app/WorkspaceIgnoreRules.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>

namespace {
int segmentScore(const QString& needle, const QString& haystack) {
  if (needle.isEmpty())
    return 0;
  if (needle == haystack)
    return 1000;
  if (haystack.startsWith(needle))
    return 800;
  const auto position = haystack.indexOf(needle);
  if (position >= 0)
    return 600 - qMin<qsizetype>(position, 100);
  qsizetype matched = 0, first = -1, last = -1;
  for (qsizetype i = 0; i < haystack.size() && matched < needle.size(); ++i) {
    if (haystack.at(i) != needle.at(matched))
      continue;
    if (first < 0)
      first = i;
    last = i;
    ++matched;
  }
  return matched == needle.size() ? 200 - qMin<qsizetype>(last - first, 150) : -1;
}
}
QStringList muffin::WorkspaceFileIndex::textExtensions() {
  return SearchSettings::load().extensions;
}
QString muffin::WorkspaceScanOptions::defaultIgnorePatterns() {
  return QStringLiteral(
      ".git/\n.hg/\n.svn/\nnode_modules/\nbuild/\ndist/\nout/\ntarget/\n.cache/\n.conan/\n.venv/\n__pycache__/");
}
QString muffin::WorkspaceFileIndex::pathKey(const QString& path) {
  if (path.isEmpty())
    return {};
  const QFileInfo info(path);
  QString key = info.canonicalFilePath();
  if (key.isEmpty())
    key = info.absoluteFilePath();
#ifdef Q_OS_WIN
  key = key.toCaseFolded();
#endif
  return QDir::cleanPath(key);
}
bool muffin::WorkspaceFileIndex::contains(const QString& root, const QString& path) {
  if (root.isEmpty() || path.isEmpty())
    return false;
  const QString base = pathKey(root), candidate = pathKey(path);
  return candidate == base || candidate.startsWith(base.endsWith('/') ? base : base + '/');
}
muffin::WorkspaceFiles muffin::WorkspaceFileIndex::scan(const QString& root, const QStringList& extensions,
                                                        const SearchCancellation& cancel, int limit) {
  // Reference maintenance (e.g. renaming a linked note) must not inherit the
  // user's search scope or newly added project ignore files.
  WorkspaceScanOptions options;
  options.readGitIgnore = options.readIgnore = false;
  return scan(root, extensions, cancel, options, limit);
}
muffin::WorkspaceFiles muffin::WorkspaceFileIndex::scan(const QString& root, const QStringList& extensions,
                                                        const SearchCancellation& cancel,
                                                        const WorkspaceScanOptions& options, int limit) {
  WorkspaceFiles result;
  if (root.isEmpty() || !QFileInfo(root).isDir())
    return result;
  const QDir rootDirectory(QFileInfo(root).absoluteFilePath());
  WorkspaceIgnoreRules customRules;
  customRules.append(options.ignorePatterns);
  struct Directory {
    QString path;
    WorkspaceIgnoreRules gitRules, ignoreRules;
  };
  QVector<Directory> pending{{rootDirectory.absolutePath(), {}, {}}};
  QSet<QString> visited;
  while (!pending.isEmpty() && !(cancel && cancel->load())) {
    auto current = pending.takeLast();
    const auto& directory = current.path;
    const QFileInfo info(directory);
    if (info.isSymLink() || info.isJunction() || !contains(root, directory))
      continue;
    const QString key = pathKey(directory);
    if (visited.contains(key))
      continue;
    visited.insert(key);
    if (!info.isReadable()) {
      result.unreadableDirectories.append(directory);
      continue;
    }
    auto loadRules = [&](const QString& name, WorkspaceIgnoreRules& rules) {
      const auto path = QDir(directory).filePath(name);
      const QFileInfo source(path);
      if (!source.exists() || source.isSymLink() || source.isJunction())
        return;
      QFile file(path);
      constexpr qint64 limitBytes = 4 * 1024 * 1024;
      if (!file.open(QIODevice::ReadOnly)) {
        result.unreadableIgnoreFiles.append(path);
        return;
      }
      const auto bytes = file.read(limitBytes + 1);
      QStringDecoder decoder(QStringDecoder::Utf8);
      const QString text = decoder.decode(bytes);
      const auto final = decoder.finalize();
      if (file.error() != QFileDevice::NoError || bytes.size() > limitBytes || decoder.hasError() ||
          final.invalidChars || text.contains(QChar(0))) {
        result.unreadableIgnoreFiles.append(path);
        return;
      }
      auto base = rootDirectory.relativeFilePath(directory);
      rules.append(text, base == "." ? QString() : base);
    };
    if (options.readGitIgnore)
      loadRules(QStringLiteral(".gitignore"), current.gitRules);
    if (options.readIgnore)
      loadRules(QStringLiteral(".ignore"), current.ignoreRules);
    QDir::Filters filters = QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot;
    if (options.includeHidden)
      filters |= QDir::Hidden | QDir::System;
    const auto entries = QDir(directory).entryInfoList(filters, QDir::Name);
    for (const auto& entry : entries) {
      if (cancel && cancel->load())
        return result;
      if (entry.isSymLink() || entry.isJunction())
        continue;
      const auto relative = rootDirectory.relativeFilePath(entry.absoluteFilePath());
      const bool ignored = customRules.ignored(
          relative, entry.isDir(),
          current.ignoreRules.ignored(relative, entry.isDir(), current.gitRules.ignored(relative, entry.isDir())));
      if (entry.isDir()) {
        if (!ignored)
          pending.append({entry.absoluteFilePath(), current.gitRules, current.ignoreRules});
        else
          result.ignoredDirectories.append(entry.absoluteFilePath());
      } else if (entry.isFile()) {
        bool allowed = false;
        for (const auto& ext : extensions) {
          if (ext == "*" || entry.fileName().endsWith('.' + ext, Qt::CaseInsensitive)) {
            allowed = true;
            break;
          }
        }
        if (!allowed)
          continue;
        if (ignored) {
          result.ignoredFiles.append(entry.absoluteFilePath());
          continue;
        }
        if (result.paths.size() >= limit) {
          result.truncated = true;
          return result;
        }
        result.paths.append(entry.absoluteFilePath());
      }
    }
  }
  return result;
}
int muffin::PathMatcher::score(const QString& query, const QString& relativePath) {
  QString normalizedQuery = query.trimmed().toCaseFolded();
  normalizedQuery.replace('\\', '/');
  if (normalizedQuery.isEmpty())
    return 0;
  QString path = relativePath.toCaseFolded();
  path.replace('\\', '/');
  const auto parts = path.split('/', Qt::SkipEmptyParts);
  if (parts.isEmpty())
    return -1;
  int score = 0;
  for (const auto& term : normalizedQuery.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts)) {
    const auto segments = term.split('/', Qt::SkipEmptyParts);
    if (segments.isEmpty())
      continue;
    if (segments.size() == 1) {
      const int filename = segmentScore(term, parts.last());
      const int fullPath = segmentScore(term, path);
      if (filename < 0 && fullPath < 0)
        return -1;
      score += qMax(filename >= 0 ? filename + 1000 : -1, fullPath);
    } else {
      QVector<int> best(parts.size() + 1, 0);
      for (const auto& segment : segments) {
        QVector<int> next(parts.size() + 1, -1);
        for (qsizetype i = 0; i < parts.size(); ++i) {
          const int matched = segmentScore(segment, parts[i]);
          next[i + 1] = qMax(next[i], best[i] >= 0 && matched >= 0 ? best[i] + matched : -1);
        }
        best = std::move(next);
      }
      if (best.last() < 0)
        return -1;
      score += best.last();
    }
  }
  return score;
}
