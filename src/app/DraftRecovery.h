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
    QString key;          // stable id: hash of the absolute source path, or "untitled"
    QString sourcePath;   // original file path; empty for untitled documents
    qint64 timestamp = 0; // msecs since epoch of the last snapshot
    qsizetype charCount = 0;
  };

  // Production leaves `directory` empty to use AppDataLocation/drafts; tests pass
  // a temp dir.
  explicit DraftRecovery(QString directory = QString());

  // Write a snapshot for the given source path (empty path = untitled). No-op if
  // the text is empty (nothing worth recovering).
  void snapshot(const QString& markdownText, const QString& sourceFilePath);

  // Remove the snapshot for this source path (called after a successful save).
  void markClean(const QString& sourceFilePath);

  QVector<PendingDraft> pendingDrafts() const;
  QString loadDraft(const PendingDraft& draft) const;
  void discard(const PendingDraft& draft);
  // Drop drafts whose source file no longer exists, plus half-written
  // (.md/.meta) pairs left behind by a crash mid-snapshot. Untitled drafts are
  // always kept. Call before pendingDrafts() at the recovery entry point.
  void pruneOrphaned();

private:
  QString keyFor(const QString& sourceFilePath) const;
  QString draftPath(const QString& key) const;
  QString metaPath(const QString& key) const;

  QString directory_;
};

}  // namespace muffin

Q_DECLARE_METATYPE(muffin::DraftRecovery::PendingDraft)
