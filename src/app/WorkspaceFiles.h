#pragma once

#include "Export.h"
#include <QStringList>
#include <atomic>
#include <memory>

namespace muffin {
using SearchCancellation = std::shared_ptr<std::atomic_bool>;
struct MUFFIN_UI_EXPORT WorkspaceScanOptions {
  QString ignorePatterns = defaultIgnorePatterns();
  bool includeHidden = false;
  bool readGitIgnore = true;
  bool readIgnore = true;
  static QString defaultIgnorePatterns();
};
struct WorkspaceFiles {
  QStringList paths;
  QStringList unreadableDirectories;
  QStringList ignoredDirectories;
  QStringList ignoredFiles;
  QStringList unreadableIgnoreFiles;
  bool truncated = false;
};
// Metadata only, independent of file-tree expansion. Safe on a worker thread.
class MUFFIN_UI_EXPORT WorkspaceFileIndex final {
public:
  static WorkspaceFiles scan(const QString& root, const QStringList& extensions, const SearchCancellation& cancel,
                             int limit = 200000);
  static WorkspaceFiles scan(const QString& root, const QStringList& extensions, const SearchCancellation& cancel,
                             const WorkspaceScanOptions& options, int limit = 200000);
  static QStringList textExtensions();
  static QString pathKey(const QString& path);
  static bool contains(const QString& root, const QString& path);
};
class MUFFIN_UI_EXPORT PathMatcher final {
public:
  // Space-separated terms are ANDed; slash-separated segments match in order.
  static int score(const QString& query, const QString& relativePath);
};
} // namespace muffin
