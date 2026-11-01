#include "app/DraftRecovery.h"
#include "app/DraftSnapshotWriter.h"
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
#include <future>
#include <atomic>

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
  recovery.markClean(oldPath, oldKey); // user chose Discard before opening B
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
  require(recovery.loadDraft(drafts.first()) == QStringLiteral("edited B"),
          QStringLiteral("New draft must contain B's edits"));
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

void testBackgroundSnapshotsOwnTextCoalesceAndClean() {
  QTemporaryDir dir;
  DraftRecovery recovery(dir.path());
  std::promise<void> started, release;
  const auto gate = release.get_future().share();
  std::atomic<int> calls{0};
  QStringList seen;
  DraftSnapshotWriter writer(dir.path(), [&](const PieceTable& text, const QString& path, const QString& key) {
    if (++calls == 1) {
      started.set_value();
      gate.wait();
    }
    seen.append(text.toString());
    return recovery.snapshot(text.toString(), path, key);
  });
  int completions = 0;
  QObject::connect(&writer, &DraftSnapshotWriter::snapshotFinished, &writer, [&](const QString&, quint64, bool saved) {
    require(saved, "background write failed");
    ++completions;
  });
  const QString key = DraftRecovery::createDraftKey();
  {
    DocumentSession session;
    session.setMarkdownText(QStringLiteral("original **中文**"), true);
    require(writer.request(session, key), "first request accepted");
    require(started.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready,
            "background worker did not start");
    require(writer.queueStats().running == 1 && writer.queueStats().pending == 0 &&
                writer.queueStats().runningCodeUnits == session.markdownText().size(),
            "running snapshot accounting must not copy or omit its text");
    require(!writer.request(session, key), "same in-flight revision must not enqueue twice");
    require(session.applyTextDelta(0, 8, QStringLiteral("second"), true), "edit while snapshot is running");
    require(writer.request(session, key), "new revision accepted");
    require(writer.queueStats().pending == 1 && writer.queueStats().pendingCodeUnits == session.markdownText().size(),
            "pending snapshot accounting must track the coalesced revision");
    session.setMarkdownText(QString(), true);
    require(writer.request(session, key), "intentional empty revision replaces pending request");
    // Session destruction must not affect either detached request.
  }
  release.set_value();
  writer.waitForDone();
  require(writer.queueStats().running == 0 && writer.queueStats().pending == 0 &&
              writer.queueStats().runningCodeUnits == 0 && writer.queueStats().pendingCodeUnits == 0,
          "drained queue must retain no pending or running text accounting");
  QCoreApplication::processEvents();
  require(seen == QStringList{QStringLiteral("original **中文**"), QString()},
          "snapshot isolation or coalescing failed");
  require(completions == 2, "successful revisions must be acknowledged after commit");
  const auto drafts = recovery.pendingDrafts();
  require(drafts.size() == 1 && drafts.first().charCount == 0, "latest empty draft must win");
  writer.markClean({}, key);
  writer.waitForDone();
  require(recovery.pendingDrafts().isEmpty(), "cleanup barrier must remove committed draft");
}

void testCleanInvalidatesRunningAndQueuedCompletions() {
  QTemporaryDir dir;
  DraftRecovery recovery(dir.path());
  std::promise<void> started, release;
  const auto gate = release.get_future().share();
  std::atomic<int> calls{0};
  DocumentSession session;
  const auto key = DraftRecovery::createDraftKey();
  int completions = 0;
  {
    DraftSnapshotWriter writer(dir.path(), [&](const PieceTable& text, const QString& path, const QString& identity) {
      if (++calls == 1) {
        started.set_value();
        gate.wait();
      }
      return recovery.snapshot(text.toString(), path, identity);
    });
    QObject::connect(&writer, &DraftSnapshotWriter::snapshotFinished, &writer,
                     [&](const QString&, quint64, bool) { ++completions; });
    session.setMarkdownText(QStringLiteral("old"), true);
    writer.request(session, key);
    require(started.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready, "worker starts");
    session.setMarkdownText(QStringLiteral("queued stale"), true);
    writer.request(session, key);
    writer.markClean({}, key); // returns even while disk write is blocked
    release.set_value();
    writer.waitForDone();
    QCoreApplication::processEvents();
    require(calls == 1 && completions == 0 && recovery.pendingDrafts().isEmpty(),
            "clean must cancel pending revision and stale completion, then remove in-flight write");
    session.setMarkdownText(QStringLiteral("fresh edit"), true);
    writer.request(session, key);
    writer.waitForDone();
    // Invalidate a success already posted to the GUI queue.
    writer.markClean({}, key);
    QCoreApplication::processEvents();
    require(completions == 0, "a queued obsolete success must not acknowledge a new generation");
    session.setMarkdownText(QStringLiteral("newest"), true);
    writer.request(session, key);
    // Destructor must drain the cleanup before this final request, even without a GUI event loop.
  }
  const auto drafts = recovery.pendingDrafts();
  require(drafts.size() == 1 && recovery.loadDraft(drafts.first()) == "newest",
          "shutdown must drain latest request after clean barrier");
}

void testBackgroundFailureRetriesAndBusySessionsAreSkipped() {
  QTemporaryDir dir;
  std::atomic<int> calls{0};
  DocumentSession session;
  DraftSnapshotWriter writer(dir.path(),
                             [&](const PieceTable&, const QString&, const QString&) { return ++calls > 1; });
  const auto key = DraftRecovery::createDraftKey();
  QVector<bool> results;
  QObject::connect(&writer, &DraftSnapshotWriter::snapshotFinished, &writer,
                   [&](const QString&, quint64, bool saved) { results.append(saved); });
  require(!writer.request(session, key), "clean session skipped");
  session.setMarkdownText(QStringLiteral("dirty"), true);
  writer.request(session, key);
  writer.waitForDone();
  QCoreApplication::processEvents();
  require(results == QVector<bool>{false}, "failure must be reported, not acknowledged as success");
  require(writer.request(session, key), "failed revision can be retried");
  writer.waitForDone();
  QCoreApplication::processEvents();
  require(results == QVector<bool>({false, true}), "retry succeeds");
  session.openDocumentAsync(QStringLiteral("new document"));
  require(!writer.request(session, key), "transitional async-open state must not be captured");
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

void testLegacyContentWithoutMetadataAndAtomicMigration() {
  QTemporaryDir dir;
  DraftRecovery recovery(dir.path());
  const QString key = DraftRecovery::createDraftKey();
  const QString legacy = dir.filePath(key + ".md");
  QFile content(legacy);
  require(content.open(QIODevice::WriteOnly), "legacy fixture");
  content.write("unsaved notes");
  content.close();
  recovery.pruneOrphaned();
  auto pending = recovery.pendingDrafts();
  require(pending.size() == 1 && pending.first().sourcePath.isEmpty() &&
              recovery.loadDraft(pending.first()) == "unsaved notes",
          "orphaned content remains recoverable");
  // A blocked destination must not retire the old recoverable data.
  const auto snapshot = dir.filePath(key + ".draft");
  require(QDir().mkdir(snapshot), "block atomic destination");
  require(!recovery.snapshot("new notes", "note.md", key) && QFile::exists(legacy), "failed migration retains content");
  QDir().rmdir(snapshot);
  require(recovery.snapshot("new notes", "note.md", key), "atomic migration");
  pending = recovery.pendingDrafts();
  require(pending.size() == 1 && pending.first().atomicSnapshot && !QFile::exists(legacy) &&
              recovery.loadDraft(pending.first()) == "new notes",
          "one complete replacement survives");
  QFile broken(snapshot);
  require(broken.open(QIODevice::ReadWrite) && broken.resize(broken.size() - 1), "truncate snapshot fixture");
  broken.close();
  bool ok = true;
  recovery.loadDraft(pending.first(), &ok);
  require(!ok, "truncated payload cannot masquerade as a complete snapshot");
}

} // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  testLegacyContentWithoutMetadataAndAtomicMigration();
  testBackgroundSnapshotsOwnTextCoalesceAndClean();
  testCleanInvalidatesRunningAndQueuedCompletions();
  testBackgroundFailureRetriesAndBusySessionsAreSkipped();
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
