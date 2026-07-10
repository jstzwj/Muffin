#pragma once

#include <QMetaType>
#include <QString>
#include <QVector>

namespace muffin {

// Crash-recovery snapshots for unsaved documents. The active document's markdown
// text (plus its source path) is written to a per-application drafts directory
// on a debounce timer; on the next launch pending snapshots are offered for
// restoration. Pure data I/O — it knows nothing about DocumentSession, taking
// raw text + path, which keeps it unit-testable with a temp directory.
class DraftRecovery {
public:
  struct PendingDraft {
    QString key;          // stable per-document id (legacy drafts may use a path hash or "untitled")
    QString sourcePath;   // original file path; empty for untitled documents
    qint64 timestamp = 0; // msecs since epoch of the last snapshot
    qsizetype charCount = 0;
  };

  // Production leaves `directory` empty to use AppDataLocation/drafts; tests pass
  // a temp dir.
  explicit DraftRecovery(QString directory = QString());

  // Create a filesystem-safe identity for one logical document instance. The
  // identity is independent of sourcePath so several untitled windows (or two
  // windows editing the same file) never overwrite each other's recovery data.
  static QString createDraftKey();

  // Write a snapshot for the given source path (empty path = untitled). No-op if
  // the text is empty (nothing worth recovering).
  void snapshot(const QString& markdownText, const QString& sourceFilePath,
                const QString& draftKey = QString());

  // Remove the snapshot for this source path (called after a successful save).
  void markClean(const QString& sourceFilePath, const QString& draftKey = QString());

  QVector<PendingDraft> pendingDrafts() const;
  QString loadDraft(const PendingDraft& draft) const;
  void discard(const PendingDraft& draft);
  // Drop only half-written (.md/.meta) pairs left behind by a crash
  // mid-snapshot. A complete draft is retained even when its source file was
  // moved or deleted: that is recoverable user data, restored as untitled.
  void pruneOrphaned();

private:
  QString keyFor(const QString& sourceFilePath, const QString& draftKey) const;
  QString draftPath(const QString& key) const;
  QString metaPath(const QString& key) const;

  QString directory_;
};

}  // namespace muffin

Q_DECLARE_METATYPE(muffin::DraftRecovery::PendingDraft)
