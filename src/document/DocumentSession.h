#pragma once

#include "document/MarkdownDocument.h"
#include "document/TopLevelRangeChange.h"
#include "Export.h"
#include "io/TextFileFormat.h"
#include "parser/CmarkGfmParser.h"

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QVector>

#include <memory>
#include <utility>

namespace muffin {

struct LocalEditNodeHint {
  NodeId nodeId;
  qsizetype targetSourceOffset = -1;
  BlockType type = BlockType::Unknown;
};

class MUFFIN_CORE_EXPORT DocumentSession final : public QObject {
  Q_OBJECT

public:
  explicit DocumentSession(QObject* parent = nullptr);

  MarkdownDocument& document();
  const MarkdownDocument& document() const;

  QString filePath() const;
  QString displayName() const;
  // Zero-copy view of the document's piece-table text (delegates to MarkdownDocument). Callers that
  // need a contiguous QString call .toString(); most read size/mid/at directly. See MarkdownDocument.
  const PieceTable& markdownText() const { return document_.markdownText(); }
  qint64 lastParseElapsedMs() const;
  // Opt-in diagnostics for full parses only. Local edit slices stay on the zero-overhead path.
  void setFullParsePerformanceMetricsEnabled(bool enabled);
  const ParsePerformanceMetrics& lastFullParsePerformanceMetrics() const;
  bool lastParseWasLocalEdit() const;
  bool lastLocalEditChangedTopLevelStructure() const;
  TopLevelRangeChange lastLocalTopLevelRangeChange() const;
  const QVector<qsizetype>& pendingMarkerOffsets() const { return pendingMarkerOffsets_; }
  // True while an async open parse (openDocumentAsync) is in flight on the worker thread. document_
  // holds the stale pre-open text during this window, so callers MUST treat the document as
  // read-only: applyTextDelta rejects edits and InputController drops keystrokes, so a stray edit
  // can't land on the stale text and supersede (discard) the worker's parsed result for the open.
  bool isAsyncParseInProgress() const;

  // External-modification detection: snapshot the on-disk mtime+size so a later save can detect
  // drift (git pull, sync client, another editor) and the QFileSystemWatcher can prompt proactively.
  // Recorded by FileController after open/save/saveAs/moveTo; cleared on newDocument/setFilePath.
  void recordFileBaseline();
  bool hasFileBaseline() const { return baselineSize_ >= 0; }
  QDateTime fileBaselineMtime() const { return baselineMtime_; }
  qint64 fileBaselineSize() const { return baselineSize_; }
  const TextFileFormat& fileFormat() const { return fileFormat_; }
  void setFileFormat(TextFileFormat format) { fileFormat_ = std::move(format); }

  // RAII: suppresses externalFileChanged across FileController's own write. The fileChanged signal
  // from QSaveFile::commit races the synchronous recordFileBaseline; this guard (plus the baseline
  // re-stat in onFileChanged) closes the self-trigger false-fire.
  class SelfWriteGuard {
  public:
    explicit SelfWriteGuard(DocumentSession& s) : session_(s) { session_.suppressExternalChange_ = true; }
    ~SelfWriteGuard() { session_.suppressExternalChange_ = false; }
    Q_DISABLE_COPY(SelfWriteGuard)
  private:
    DocumentSession& session_;
  };

  void newDocument();
  void setFilePath(QString path);
  void setMarkdownText(QString text, bool modified);
  // Asynchronous full-parse entry used by FileController::open: runs parser_.parseDocument on a
  // worker thread so the UI stays responsive on huge files, then finishes (setMarkdownText +
  // relativize + emit parsed) on the GUI thread via finishAsyncParse. All other entry points stay
  // synchronous. Supersession: any newer parse or local edit bumps parseGeneration_, so a stale
  // worker's result is discarded.
  void openDocumentAsync(QString text);
  void updateFromEditor(QString text);
  // Applies new parse options. When they differ from the current ones the document is re-parsed
  // (full path: parseAndStore sets lastParseWasLocalEdit_=false, so the rendered view rebuilds via
  // the `parsed` signal). No-op (no re-parse) when unchanged, so callers can invoke unconditionally.
  void setParseOptions(ParseOptions options);
  void applyMarkdownText(QString text, bool modified, QVector<qsizetype> demoteAtOffsets = {});
  bool applyTextDelta(
      qsizetype sourceStart,
      qsizetype removedLength,
      QString insertedText,
      bool modified,
      QVector<LocalEditNodeHint> nodeHints = {});
  bool applyTableSnapshot(NodeId tableId, int tableIndex, const MarkdownNode& tableSnapshot, bool modified);
  bool applyNodeSnapshot(NodeId nodeId, BlockType nodeType, int nodeIndex, const MarkdownNode& nodeSnapshot, bool modified);
  bool applyInsertedNode(
      NodeId nodeId,
      BlockType nodeType,
      qsizetype sourceStart,
      qsizetype targetSourceOffset,
      qsizetype removedLength,
      QString insertedText,
      bool modified);

signals:
  void documentTextChanged(QString text);
  void documentLocallyEdited(qsizetype start, qsizetype removedLength, QString insertedText);
  void filePathChanged(QString path);
  void parsed(qint64 elapsedMs);
  void parseBusy(bool busy);  // true while an async open parse is in flight (view shows a loading state)
  void modifiedChanged(bool modified);
  void externalFileChanged();  // the file changed on disk vs. our last known baseline

private slots:
  void onFileChanged();  // QFileSystemWatcher::fileChanged → drift check → externalFileChanged

private:
  void refreshFileWatch();
  void parseAndStore(QString text, bool modified, QVector<qsizetype> demoteAtOffsets = {}, bool async = false);
  // GUI-thread completion of an async parseAndStore (async=true). Discards the result if superseded.
  void finishAsyncParse();
  bool tryApplyTopLevelLocalEdit(
      qsizetype sourceStart,
      qsizetype sourceEnd,
      const QString& replacementText,
      bool modified,
      const QVector<LocalEditNodeHint>& nodeHints);

  MarkdownDocument document_;
  CmarkGfmParser parser_;
  ParseOptions parseOptions_;
  QString filePath_;
  TextFileFormat fileFormat_;
  qint64 lastParseElapsedMs_ = 0;
  bool fullParsePerformanceMetricsEnabled_ = false;
  ParsePerformanceMetrics lastFullParsePerformanceMetrics_;
  bool lastParseWasLocalEdit_ = false;
  bool lastLocalEditChangedTopLevelStructure_ = false;
  TopLevelRangeChange lastLocalTopLevelRangeChange_;
  // Async open-parse state. Only the FileController::open path passes async=true; everything else
  // runs the synchronous code path unchanged.
  QFutureWatcher<std::shared_ptr<ParseResult>>* parseWatcher_ = nullptr;
  // Covers the complete async-open transaction, including the interval after the worker finishes
  // but before its queued `finished` slot commits pendingText_ on the GUI thread. QFutureWatcher's
  // isRunning() does not cover that interval, so it cannot safely gate editing or saving.
  bool asyncParsePending_ = false;
  quint64 parseGeneration_ = 0;   // bumped on every parseAndStore + local edit; supersedes in-flight workers
  quint64 launchGeneration_ = 0;  // generation captured when the current worker was launched
  QString pendingText_;           // text held for the GUI-thread finish step
  bool pendingModified_ = false;
  QVector<qsizetype> pendingDemoteAtOffsets_;
  QVector<qsizetype> pendingMarkerOffsets_;

  // External-modification detection. The watcher tracks filePath_; the baseline is the mtime+size
  // at open/last-save. suppressExternalChange_ is set by SelfWriteGuard during our own writes.
  QDateTime baselineMtime_;
  qint64 baselineSize_ = -1;
  QFileSystemWatcher* fileWatcher_ = nullptr;
  bool suppressExternalChange_ = false;
  QDateTime lastNotifiedMtime_;
  qint64 lastNotifiedSize_ = -2;
};

}  // namespace muffin
