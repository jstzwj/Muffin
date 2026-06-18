#include "app/DraftRecovery.h"

#include <QCoreApplication>
#include <QDebug>
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

void testEmptyTextIsIgnored() {
  QTemporaryDir dir;
  DraftRecovery recovery = makeRecovery(dir);
  recovery.snapshot(QString(), QStringLiteral("/tmp/empty.md"));
  require(recovery.pendingDrafts().isEmpty(), QStringLiteral("Empty text should not be snapshotted"));
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

void testPruneRemovesDraftsForDeletedSource() {
  QTemporaryDir dir;
  DraftRecovery recovery = makeRecovery(dir);
  // A real on-disk source: its draft survives pruning.
  const QString alivePath = dir.filePath(QStringLiteral("alive.md"));
  writeSourceFile(alivePath);
  recovery.snapshot(QStringLiteral("alive"), alivePath);
  // A source that was never on disk (deleted before next launch): pruned.
  recovery.snapshot(QStringLiteral("ghost"), QStringLiteral("/tmp/muffin-no-such-file.md"));
  require(recovery.pendingDrafts().size() == 2, QStringLiteral("Both drafts listed before prune"));
  recovery.pruneOrphaned();
  const auto after = recovery.pendingDrafts();
  require(after.size() == 1, QStringLiteral("Only the on-disk-source draft survives prune"));
  require(after.first().sourcePath == alivePath, QStringLiteral("Surviving draft should be the on-disk source"));
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
  testEmptyDirectoryHasNoDrafts();
  testSnapshotRoundTrips();
  testUntitledSnapshotHasEmptySource();
  testEmptyTextIsIgnored();
  testMarkCleanRemovesDraft();
  testDiscardRemovesDraft();
  testSnapshotOverwritesSameKey();
  testMultipleDraftsListNewestFirst();
  testPruneRemovesDraftsForDeletedSource();
  testPruneRemovesHalfWrittenPairs();
  testPruneLeavesUntitledAndStrayFiles();
  return 0;
}
