#include "app/DraftRecovery.h"
#include "document/DocumentSession.h"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QThread>
#include <QDir>
#include <QString>
#include <QTemporaryDir>

#include <cstdlib>
#include <utility>

// Exercises DraftRecovery's pure data I/O against a temp directory: snapshot →
// pendingDrafts → loadDraft → markClean/discard, plus the untitled-key and
// overwrite-same-key behaviors the crash-recovery flow relies on.
//
// Follows the project's test convention (no QTest): require() asserts that exit
// on failure, plain test functions, and a main() that runs them under QCoreApplication.

using namespace muffin;

namespace {

void require(bool condition, const QString& message) {
  if (!condition) {
    qCritical().noquote() << message;
    std::exit(1);
  }
}

DraftRecovery makeRecovery(const QTemporaryDir& dir) {
  // Each test starts from a clean drafts directory.
  QDir(dir.path()).removeRecursively();
  QDir().mkpath(dir.path());
  return DraftRecovery(dir.path());
}

void testAsyncOpenNeverSnapshotsPreviousDocument() {
  QTemporaryDir dir;
  DraftRecovery recovery = makeRecovery(dir);
  DocumentSession session;
  const QString oldPath = dir.filePath(QStringLiteral("A.md"));
  const QString newPath = dir.filePath(QStringLiteral("B.md"));
  const QString oldKey = DraftRecovery::createDraftKey();
  const QString newKey = DraftRecovery::createDraftKey();
  session.setFilePath(oldPath);
  session.setMarkdownText(QStringLiteral("unsaved A"), true);
  require(recovery.snapshotSession(session, oldKey), QStringLiteral("Stable dirty document must be recoverable"));
  require(recovery.pendingDrafts().size() == 1, QStringLiteral("A snapshot missing"));
  recovery.markClean(oldPath, oldKey);  // user chose Discard before opening B
  session.openDocumentAsync(QStringLiteral("disk B"));
  session.setFilePath(newPath);
  require(session.isAsyncParseInProgress(), QStringLiteral("Async fixture should be pending before event processing"));
  require(!recovery.snapshotSession(session, newKey), QStringLiteral("Timer must not snapshot old buffer under B"));
  require(!recovery.snapshotSession(session, newKey), QStringLiteral("Close-time snapshot must also be blocked"));
  require(recovery.pendingDrafts().isEmpty(), QStringLiteral("Discarded A must not reappear as B draft"));
  QElapsedTimer wait;
  wait.start();
  while (session.isAsyncParseInProgress() && wait.elapsed() < 5000) {
    QCoreApplication::processEvents();
    QThread::msleep(1);
  }
  require(!session.isAsyncParseInProgress(), QStringLiteral("Async parse did not finish"));
  require(!recovery.snapshotSession(session, newKey), QStringLiteral("Clean B needs no recovery draft"));
  session.setMarkdownText(QStringLiteral("edited B"), true);
  require(recovery.snapshotSession(session, newKey), QStringLiteral("Real edits after open must remain recoverable"));
  const auto drafts = recovery.pendingDrafts();
  require(drafts.size() == 1 && drafts.first().sourcePath == newPath && drafts.first().key == newKey,
          QStringLiteral("New draft must have B's identity"));
  require(recovery.loadDraft(drafts.first()) == QStringLiteral("edited B"), QStringLiteral("New draft must contain B's edits"));
}

void testEmptyDirectoryHasNoDrafts() {
  QTemporaryDir dir;
  require(dir.isValid(), QStringLiteral("Temp dir invalid"));
  DraftRecovery recovery = makeRecovery(dir);
  require(recovery.pendingDrafts().isEmpty(), QStringLiteral("Empty dir should have no drafts"));
}

void testSnapshotRoundTrips() {
  QTemporaryDir dir;
  DraftRecovery recovery = makeRecovery(dir);
  const QString content = QStringLiteral("# Hello\nworld");
  recovery.snapshot(content, QStringLiteral("/tmp/notes.md"));
  const QVector<DraftRecovery::PendingDraft> drafts = recovery.pendingDrafts();
  require(drafts.size() == 1, QStringLiteral("Expected 1 draft"));
  require(drafts.first().sourcePath == QStringLiteral("/tmp/notes.md"), QStringLiteral("sourcePath mismatch"));
  require(drafts.first().charCount == content.size(), QStringLiteral("charCount mismatch"));
  require(recovery.loadDraft(drafts.first()) == content, QStringLiteral("loadDraft mismatch"));
}

void testUntitledSnapshotHasEmptySource() {
  QTemporaryDir dir;
  DraftRecovery recovery = makeRecovery(dir);
  recovery.snapshot(QStringLiteral("scratch"), QString());
  const auto drafts = recovery.pendingDrafts();
  require(drafts.size() == 1, QStringLiteral("Expected 1 untitled draft"));
  require(drafts.first().sourcePath.isEmpty(), QStringLiteral("Untitled sourcePath should be empty"));
}

void testExplicitKeysIsolateUntitledDocuments() {
  QTemporaryDir dir;
  DraftRecovery recovery = makeRecovery(dir);
  const QString firstKey = DraftRecovery::createDraftKey();
  const QString secondKey = DraftRecovery::createDraftKey();
  require(firstKey != secondKey, QStringLiteral("Draft keys must be unique"));

  recovery.snapshot(QStringLiteral("first"), QString(), firstKey);
  recovery.snapshot(QStringLiteral("second"), QString(), secondKey);
  const auto drafts = recovery.pendingDrafts();
  require(drafts.size() == 2, QStringLiteral("Two untitled documents need independent drafts"));

  recovery.markClean(QString(), firstKey);
  const auto remaining = recovery.pendingDrafts();
  require(remaining.size() == 1, QStringLiteral("Cleaning one untitled draft must retain the other"));
  require(recovery.loadDraft(remaining.first()) == QStringLiteral("second"),
          QStringLiteral("Wrong untitled draft survived cleanup"));
}

void testEmptyTextIsRecoverable() {
  QTemporaryDir dir;
  DraftRecovery recovery = makeRecovery(dir);
  require(recovery.snapshot(QString(), QStringLiteral("/tmp/empty.md")), QStringLiteral("Empty snapshot must commit"));
  const auto pending = recovery.pendingDrafts();
  require(pending.size() == 1 && pending.first().charCount == 0, QStringLiteral("Empty draft must be listed"));
  bool loaded = false;
  require(recovery.loadDraft(pending.first(), &loaded).isEmpty() && loaded,
          QStringLiteral("Reading an empty draft must succeed"));
}

void testClearedDocumentReplacesPreviousSnapshot() {
  QTemporaryDir dir;
  DraftRecovery recovery = makeRecovery(dir);
  DocumentSession session;
  const QString key = DraftRecovery::createDraftKey();
  require(!recovery.snapshotSession(session, key), QStringLiteral("An unmodified empty document needs no draft"));
  session.setMarkdownText(QStringLiteral("before deletion"), true);
  require(recovery.snapshotSession(session, key), QStringLiteral("Initial dirty snapshot must commit"));
  require(session.applyTextDelta(0, session.markdownText().size(), QString(), true),
          QStringLiteral("Deleting all document text must succeed"));
  require(recovery.snapshotSession(session, key), QStringLiteral("Cleared dirty document must replace the snapshot"));
  recovery.pruneOrphaned();
  const auto pending = recovery.pendingDrafts();
  require(pending.size() == 1 && pending.first().key == key && pending.first().charCount == 0,
          QStringLiteral("Cleared snapshot must retain its identity and survive pruning"));
  bool loaded = false;
  const QString restored = recovery.loadDraft(pending.first(), &loaded);
  require(loaded && restored.isEmpty(), QStringLiteral("Recover the empty state, not the earlier text"));
  recovery.discard(pending.first());
  loaded = true;
  recovery.loadDraft(pending.first(), &loaded);
  require(!loaded, QStringLiteral("A missing draft must report a read failure"));
}

void testFailedSnapshotIsReported() {
  QTemporaryDir dir;
  const QString blocked = dir.filePath(QStringLiteral("not-a-directory"));
  QFile file(blocked);
  require(file.open(QIODevice::WriteOnly), QStringLiteral("Could not create blocking file"));
  file.close();
  DraftRecovery recovery(blocked);
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("dirty"), true);
  require(!recovery.snapshotSession(session, DraftRecovery::createDraftKey()),
          QStringLiteral("Unwritable snapshots must not be reported as saved"));
}

void testMarkCleanRemovesDraft() {
  QTemporaryDir dir;
  DraftRecovery recovery = makeRecovery(dir);
  recovery.snapshot(QStringLiteral("data"), QStringLiteral("/tmp/clean.md"));
  require(recovery.pendingDrafts().size() == 1, QStringLiteral("Expected draft after snapshot"));
  recovery.markClean(QStringLiteral("/tmp/clean.md"));
  require(recovery.pendingDrafts().isEmpty(), QStringLiteral("Draft should be gone after markClean"));
}

void testDiscardRemovesDraft() {
  QTemporaryDir dir;
  DraftRecovery recovery = makeRecovery(dir);
  recovery.snapshot(QStringLiteral("data"), QStringLiteral("/tmp/discard.md"));
  QVector<DraftRecovery::PendingDraft> drafts = recovery.pendingDrafts();
  require(drafts.size() == 1, QStringLiteral("Expected draft after snapshot"));
  recovery.discard(drafts.first());
  require(recovery.pendingDrafts().isEmpty(), QStringLiteral("Draft should be gone after discard"));
}

void testSnapshotOverwritesSameKey() {
  QTemporaryDir dir;
  DraftRecovery recovery = makeRecovery(dir);
  recovery.snapshot(QStringLiteral("v1"), QStringLiteral("/tmp/overwrite.md"));
  recovery.snapshot(QStringLiteral("v2"), QStringLiteral("/tmp/overwrite.md"));
  const auto drafts = recovery.pendingDrafts();
  require(drafts.size() == 1, QStringLiteral("Overwrite should keep a single draft"));
  require(recovery.loadDraft(drafts.first()) == QStringLiteral("v2"), QStringLiteral("Latest snapshot should win"));
}

void testMultipleDraftsListNewestFirst() {
  QTemporaryDir dir;
  DraftRecovery recovery = makeRecovery(dir);
  recovery.snapshot(QStringLiteral("older"), QStringLiteral("/tmp/a.md"));
  recovery.snapshot(QStringLiteral("newer"), QStringLiteral("/tmp/b.md"));
  const auto drafts = recovery.pendingDrafts();
  require(drafts.size() == 2, QStringLiteral("Expected 2 drafts"));
  require(drafts.first().timestamp >= drafts.last().timestamp, QStringLiteral("Drafts should be newest-first"));
}

void writeSourceFile(const QString& path) {
  QFile f(path);
  require(f.open(QIODevice::WriteOnly), QStringLiteral("Could not write source file"));
  f.write("x");
  f.close();
}

void testPruneKeepsDraftsForDeletedSource() {
  QTemporaryDir dir;
  DraftRecovery recovery = makeRecovery(dir);
  // A real on-disk source: its draft survives pruning.
  const QString alivePath = dir.filePath(QStringLiteral("alive.md"));
  writeSourceFile(alivePath);
  recovery.snapshot(QStringLiteral("alive"), alivePath);
  // A source that was deleted before next launch remains recoverable as untitled.
  recovery.snapshot(QStringLiteral("ghost"), QStringLiteral("/tmp/muffin-no-such-file.md"));
  require(recovery.pendingDrafts().size() == 2, QStringLiteral("Both drafts listed before prune"));
  recovery.pruneOrphaned();
  const auto after = recovery.pendingDrafts();
  require(after.size() == 2, QStringLiteral("Complete drafts must survive regardless of source existence"));
  bool foundMissingSource = false;
  for (const auto& draft : after) {
    if (draft.sourcePath == QStringLiteral("/tmp/muffin-no-such-file.md")) {
      foundMissingSource = recovery.loadDraft(draft) == QStringLiteral("ghost");
    }
  }
  require(foundMissingSource, QStringLiteral("Missing-source draft should remain loadable"));
}

void testPruneRemovesHalfWrittenPairs() {
  QTemporaryDir dir;
  DraftRecovery recovery = makeRecovery(dir);
  // Simulate a crash that left a .meta with no matching .md (a 16-hex draft key,
  // so pruneOrphaned recognizes it and removes the orphan sidecar).
  const QString orphanMeta = QDir(dir.path()).absoluteFilePath(QStringLiteral("deadbeefdeadbeef.meta"));
  {
    QFile f(orphanMeta);
    require(f.open(QIODevice::WriteOnly), QStringLiteral("Could not write orphan meta"));
    f.write(R"({"sourcePath":"","timestamp":0,"charCount":0})");
    f.close();
  }
  require(QFile::exists(orphanMeta), QStringLiteral("Orphan meta present before prune"));
  recovery.pruneOrphaned();
  require(!QFile::exists(orphanMeta), QStringLiteral("Orphan meta removed by prune"));
}

void testPruneLeavesUntitledAndStrayFiles() {
  QTemporaryDir dir;
  DraftRecovery recovery = makeRecovery(dir);
  // Untitled drafts are kept even though their source is empty.
  recovery.snapshot(QStringLiteral("scratch"), QString());
  // A stray non-draft file in the directory must not be touched.
  const QString stray = QDir(dir.path()).absoluteFilePath(QStringLiteral("readme.md"));
  writeSourceFile(stray);
  recovery.pruneOrphaned();
  require(recovery.pendingDrafts().size() == 1, QStringLiteral("Untitled draft kept after prune"));
  require(QFile::exists(stray), QStringLiteral("Stray file left untouched by prune"));
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  testAsyncOpenNeverSnapshotsPreviousDocument();
  testEmptyDirectoryHasNoDrafts();
  testSnapshotRoundTrips();
  testUntitledSnapshotHasEmptySource();
  testExplicitKeysIsolateUntitledDocuments();
  testEmptyTextIsRecoverable();
  testClearedDocumentReplacesPreviousSnapshot();
  testFailedSnapshotIsReported();
  testMarkCleanRemovesDraft();
  testDiscardRemovesDraft();
  testSnapshotOverwritesSameKey();
  testMultipleDraftsListNewestFirst();
  testPruneKeepsDraftsForDeletedSource();
  testPruneRemovesHalfWrittenPairs();
  testPruneLeavesUntitledAndStrayFiles();
  return 0;
}
