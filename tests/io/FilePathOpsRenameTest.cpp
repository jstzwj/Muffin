#include "io/FilePathOps.h"

#include "../TestUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>

using namespace muffin;

// Builds a unique temp working directory for a test and tears it down on scope exit. The
// collision/rename helpers resolve canonicalFilePath, so they need real entries on disk.
class WorkDir {
 public:
  WorkDir() {
    path_ = QDir(QDir::tempPath()).filePath(QStringLiteral("muffin-rename-test-%1").arg(counter_++));
    QDir().mkpath(path_);
  }
  ~WorkDir() { QDir(path_).removeRecursively(); }
  QString path() const { return path_; }
  QString file(const QString& name) const { return QDir(path_).filePath(name); }
  bool makeFile(const QString& name) const {
    QFile f(file(name));
    if (!f.open(QIODevice::WriteOnly)) {
      return false;
    }
    f.write("x");
    f.close();
    return true;
  }
 private:
  QString path_;
  static qint64 counter_;
};
qint64 WorkDir::counter_ = 0;

void requireEq(const QString& actual, const QString& expected, const QString& message) {
  require(actual == expected,
          QStringLiteral("%1\n   expected: %2\n   actual:   %3").arg(message, expected, actual));
}

// ---- targetNameCollides -----------------------------------------------------

void testCollidesWithDifferentFile() {
  WorkDir wd;
  wd.makeFile(QStringLiteral("a.md"));
  wd.makeFile(QStringLiteral("b.md"));
  require(FilePathOps::targetNameCollides(wd.file(QStringLiteral("a.md")), wd.path(), QStringLiteral("b.md")),
          QStringLiteral("a.md vs sibling b.md → collision"));
}

void testNoCollisionForDistinctName() {
  WorkDir wd;
  wd.makeFile(QStringLiteral("a.md"));
  wd.makeFile(QStringLiteral("b.md"));
  require(!FilePathOps::targetNameCollides(wd.file(QStringLiteral("a.md")), wd.path(), QStringLiteral("c.md")),
          QStringLiteral("a.md vs absent c.md → no collision"));
}

void testSelfIsNotACollision() {
  WorkDir wd;
  wd.makeFile(QStringLiteral("a.md"));
  // Renaming a.md to "a.md" (exact) or "A.md" (case-only) is the SAME file, never a collision.
  require(!FilePathOps::targetNameCollides(wd.file(QStringLiteral("a.md")), wd.path(), QStringLiteral("a.md")),
          QStringLiteral("renaming a.md to a.md → not a collision (same file)"));
  require(!FilePathOps::targetNameCollides(wd.file(QStringLiteral("a.md")), wd.path(), QStringLiteral("A.md")),
          QStringLiteral("case-only a.md→A.md → not a collision (same file)"));
}

// ---- uniqueDuplicatePath ----------------------------------------------------

void testUniqueDuplicatePath() {
  WorkDir wd;
  const QString src = wd.file(QStringLiteral("notes.md"));
  requireEq(FilePathOps::uniqueDuplicatePath(src), wd.file(QStringLiteral("notes (1).md")),
            QStringLiteral("first duplicate of notes.md → notes (1).md"));
}

// ---- renamePath -------------------------------------------------------------

void testRenameToNewNameSucceeds() {
  WorkDir wd;
  wd.makeFile(QStringLiteral("c.md"));
  QString error;
  require(FilePathOps::renamePath(wd.file(QStringLiteral("c.md")), wd.file(QStringLiteral("d.md")), &error),
          QStringLiteral("rename c.md → d.md succeeds"));
  require(QFileInfo::exists(wd.file(QStringLiteral("d.md"))), QStringLiteral("d.md exists after rename"));
  require(!QFileInfo::exists(wd.file(QStringLiteral("c.md"))), QStringLiteral("c.md gone after rename"));
}

void testRenameOntoExistingFails() {
  WorkDir wd;
  wd.makeFile(QStringLiteral("x.md"));
  wd.makeFile(QStringLiteral("y.md"));
  QString error;
  require(!FilePathOps::renamePath(wd.file(QStringLiteral("x.md")), wd.file(QStringLiteral("y.md")), &error),
          QStringLiteral("rename x.md → existing y.md fails"));
  require(QFileInfo::exists(wd.file(QStringLiteral("x.md"))), QStringLiteral("x.md still exists (reverted)"));
}

// A case-only rename must land as the new case on every platform: direct rename on a
// case-sensitive FS, the two-step path on a case-insensitive one.
void testCaseOnlyRenameLandsAsNewCase() {
  WorkDir wd;
  wd.makeFile(QStringLiteral("low.md"));
  QString error;
  require(FilePathOps::renamePath(wd.file(QStringLiteral("low.md")), wd.file(QStringLiteral("LOW.md")), &error),
          QStringLiteral("case-only rename low.md → LOW.md succeeds"));
  const QStringList entries = QDir(wd.path()).entryList(QDir::Files | QDir::NoDotAndDotDot);
  require(entries.contains(QStringLiteral("LOW.md")),
          QStringLiteral("directory lists the new case LOW.md"));
  require(!entries.contains(QStringLiteral("low.md")),
          QStringLiteral("directory no longer lists the old case low.md"));
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  testCollidesWithDifferentFile();
  testNoCollisionForDistinctName();
  testSelfIsNotACollision();
  testUniqueDuplicatePath();
  testRenameToNewNameSucceeds();
  testRenameOntoExistingFails();
  testCaseOnlyRenameLandsAsNewCase();
  return 0;
}
