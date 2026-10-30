#pragma once
#include "app/WorkspaceFiles.h"
#include "editor/DocumentSearch.h"
#include "io/TextFileFormat.h"
#include <QHash>
#include <QStringView>
#include <functional>

namespace muffin {
class PieceTable;
struct OpenSearchBuffer {
  QString text;
  bool modified = false;
  bool busy = false;
  // Immutable piece-table copy: large open notes do not materialize on the UI thread.
  std::shared_ptr<const PieceTable> snapshot;
  bool readOnly = false;
};
using OpenSearchBuffers = QHash<QString, OpenSearchBuffer>;
struct WorkspaceQuery {
  QString root, pattern, includePath, excludePath;
  QStringList extensions;
  WorkspaceScanOptions scanOptions;
  SearchOptions options;
  qint64 maxFileBytes = 512 * 1024 * 1024;
  qint64 maxRegexBytes = 32 * 1024 * 1024;
  qint64 maxReplaceBytes = 8 * 1024 * 1024;
  qint64 maxResultBytes = 16 * 1024 * 1024;
  int maxMatches = 10000;
  int workerCount = 4;
  int chunkBytes = 64 * 1024;
};
struct WorkspaceHit {
  SearchMatch match;
  int line = 1;
  QString context;
  int highlight = 0;
};
struct WorkspaceSearchFile {
  QString path;
  QByteArray fingerprint;
  QVector<WorkspaceHit> hits;
  bool modified = false;
  bool replaceable = false;
};
enum class WorkspaceSkipReason {
  TooLarge,
  RegexTooLarge,
  Busy,
  Unreadable,
  InvalidText,
  Changed,
  ResourceLimit,
  IgnoredDirectory,
  IgnoredFile,
  UnreadableIgnoreFile
};
struct WorkspaceSkip {
  QString path;
  WorkspaceSkipReason reason;
};
struct WorkspaceSearchResult {
  WorkspaceQuery query;
  QVector<WorkspaceSearchFile> files;
  QStringList skipped;
  QVector<WorkspaceSkip> details;
  QString error;
  bool complete = true;
  int matches = 0;
  int searchedFiles = 0;
  bool resultLimitReached = false;
};
class MUFFIN_UI_EXPORT WorkspaceSearch final {
public:
  static WorkspaceSearchResult run(const WorkspaceQuery& query, const OpenSearchBuffers& buffers,
                                   const SearchCancellation& cancel,
                                   const std::function<void(const WorkspaceSearchFile&)>& progress = {});
  static QByteArray fingerprint(QStringView text);
  static QByteArray fingerprint(const PieceTable& text);
  static bool bufferMatches(const OpenSearchBuffer& buffer, QStringView text);
};
} // namespace muffin
