#include "export/PandocRunner.h"

#include <QApplication>
#include <QDir>
#include <QProgressDialog>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QString>

#include <cstdlib>

// Verifies PandocRunner's resolution policy: an empty/invalid configured path falls
// through to the system search of well-known install locations, then the bare
// "pandoc"; a real executable path is honored verbatim. findFirstExistingExecutable
// is the deterministic core and is exercised directly. A self-executing child also
// verifies run()'s working directory and progress UI without an installed Pandoc;
// this checks process invocation, not Pandoc's document conversion.
//
// Note: the fallback is NOT a hard-coded bare "pandoc" anymore — it is
// searchSystem() (which finds real installs) and only THEN bare "pandoc". So
// fallback tests compare against `searchSystem() or "pandoc"`, never the literal
// "pandoc", or they would fail on any machine that actually has Pandoc installed.
//
// Follows the project test convention (no QTest). Org/app names are set in
// main() before any QSettings use so default-constructed QSettings() resolves
// consistently within the process.

namespace {

void require(bool condition, const QString& message) {
  if (!condition) {
    qCritical().noquote() << message;
    std::exit(1);
  }
}

// Expected auto-fallback result on this machine: a discovered install path, or
// the bare "pandoc" if nothing was found.
QString expectedAutoResolution() {
  const QString searched = muffin::PandocRunner::searchSystem();
  return searched.isEmpty() ? QStringLiteral("pandoc") : searched;
}

void testEmptySettingFallsBackToSystemSearchOrBarePandoc() {
  QSettings().remove(QStringLiteral("export/pandocPath"));
  require(muffin::PandocRunner::resolvedExecutable() == expectedAutoResolution(),
          QStringLiteral("Empty/unset pandocPath should fall back to system search then bare 'pandoc'"));
}

void testNonExistentConfiguredPathFallsBack() {
  QSettings().setValue(QStringLiteral("export/pandocPath"), QStringLiteral("/no/such/pandoc-binary"));
  require(muffin::PandocRunner::resolvedExecutable() == expectedAutoResolution(),
          QStringLiteral("A non-existent configured path should fall back to system search then bare 'pandoc'"));
  QSettings().remove(QStringLiteral("export/pandocPath"));
}

void testRealExecutablePathIsHonored() {
  const QString self = QCoreApplication::applicationFilePath();
  require(QFileInfo(self).isExecutable(), QStringLiteral("The test binary itself must be executable"));
  QSettings().setValue(QStringLiteral("export/pandocPath"), self);
  require(muffin::PandocRunner::resolvedExecutable() == self,
          QStringLiteral("A real executable path should be honored verbatim"));
  QSettings().remove(QStringLiteral("export/pandocPath"));
}

// findFirstExistingExecutable is the pure, deterministic core of the system
// search; the per-platform candidate list is environment-dependent and not
// asserted here. The test binary stands in for a real executable.
void testFindFirstExistingReturnsFirstExecutable() {
  const QString self = QCoreApplication::applicationFilePath();
  const QString got =
      muffin::PandocRunner::findFirstExistingExecutable({QStringLiteral("/no/such/a"), self});
  require(got == self, QStringLiteral("findFirstExistingExecutable should return the first real executable"));
}

void testFindFirstExistingReturnsEmptyForGarbage() {
  const QString got = muffin::PandocRunner::findFirstExistingExecutable(
      {QStringLiteral("/no/such/pandoc"), QStringLiteral("C:/definitely/not/here/pandoc.exe")});
  require(got.isEmpty(), QStringLiteral("findFirstExistingExecutable should return empty when nothing matches"));
}

void testFindFirstExistingSkipsNonExecutables() {
  const QString self = QCoreApplication::applicationFilePath();
  // Empty entries and nonexistent paths are skipped; the real executable wins.
  const QString got = muffin::PandocRunner::findFirstExistingExecutable(
      {QString(), QStringLiteral("/no/such/b"), self});
  require(got == self, QStringLiteral("findFirstExistingExecutable should skip bad entries and return the executable"));
}

bool sameExistingDirectory(const QString& actualPath, const QString& expectedPath) {
  const QFileInfo actual(actualPath);
  const QFileInfo expected(expectedPath);
  if (!actual.isDir() || !expected.isDir()) return false;
  const QString canonicalActual = actual.canonicalFilePath();
  return !canonicalActual.isEmpty() && canonicalActual == expected.canonicalFilePath();
}

void testDirectoryIdentityRejectsWrongTargets() {
  QTemporaryDir expected;
  QTemporaryDir other;
  require(expected.isValid() && other.isValid(), QStringLiteral("Fixture directories missing"));
  require(!sameExistingDirectory(other.path(), expected.path()),
          QStringLiteral("A genuinely different working directory must be rejected"));
  const QString missing = expected.filePath(QStringLiteral("missing"));
  require(!sameExistingDirectory(missing, missing),
          QStringLiteral("Two missing paths must not compare equal via empty canonical paths"));
  require(!sameExistingDirectory(QString(), QString()), QStringLiteral("Empty directory paths must be rejected"));
  QFile file(expected.filePath(QStringLiteral("file")));
  require(file.open(QIODevice::WriteOnly), QStringLiteral("Could not create regular-file fixture"));
  file.close();
  require(!sameExistingDirectory(file.fileName(), file.fileName()),
          QStringLiteral("An existing regular file must not count as a working directory"));
}

void checkWorkingDirectoryAndProgressLabel(const QString& workDir) {
  QSettings().setValue(QStringLiteral("export/pandocPath"), QCoreApplication::applicationFilePath());
  const QString label = QStringLiteral("Importing document…");
  bool sawLabel = false;
  QTimer::singleShot(0, [&] {
    for (QWidget* widget : QApplication::topLevelWidgets()) {
      if (auto* progress = qobject_cast<QProgressDialog*>(widget)) {
        sawLabel = progress->labelText() == label;
      }
    }
  });
  const auto result = muffin::PandocRunner::run(nullptr, {QStringLiteral("--fake-pandoc")},
                                               {.workDir = workDir, .progressLabel = label});
  require(result.ran && result.exitCode == 0, QStringLiteral("Import process should start"));
  const QString actualDirectory = QString::fromUtf8(result.out);
  require(sameExistingDirectory(actualDirectory, workDir),
          QStringLiteral("Wrong import working directory: actual=%1 expected=%2").arg(actualDirectory, workDir));
  require(sawLabel, QStringLiteral("Import label should be shown as progress text"));
  QSettings().remove(QStringLiteral("export/pandocPath"));
}

void testWorkingDirectoryAndProgressLabelAreIndependent() {
  QTemporaryDir dir;
  require(dir.isValid(), QStringLiteral("Fixture directory missing"));
  const QString actualDirectory = dir.filePath(QStringLiteral("actual"));
  require(QDir().mkdir(actualDirectory), QStringLiteral("Could not create working directory"));
  checkWorkingDirectoryAndProgressLabel(actualDirectory);
#ifdef Q_OS_UNIX
  // An OS-reported cwd may use the physical path even when QProcess was given an alias
  // (for example /var versus /private/var on macOS). Exercise that distinction explicitly.
  const QString alias = dir.filePath(QStringLiteral("alias"));
  require(QFile::link(actualDirectory, alias), QStringLiteral("Could not create directory alias"));
  checkWorkingDirectoryAndProgressLabel(alias);
#endif
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && QByteArray(argv[1]) == "--fake-pandoc") {
    const QByteArray cwd = QDir::currentPath().toUtf8();
    std::fwrite(cwd.constData(), 1, cwd.size(), stdout);
    QThread::msleep(300);  // let the parent exercise its responsive progress loop
    return 0;
  }
  if (qgetenv("QT_QPA_PLATFORM").isEmpty()) qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("Muffin"));
  QCoreApplication::setApplicationName(QStringLiteral("MuffinTests"));
  testDirectoryIdentityRejectsWrongTargets();
  testWorkingDirectoryAndProgressLabelAreIndependent();
  testEmptySettingFallsBackToSystemSearchOrBarePandoc();
  testNonExistentConfiguredPathFallsBack();
  testRealExecutablePathIsHonored();
  testFindFirstExistingReturnsFirstExecutable();
  testFindFirstExistingReturnsEmptyForGarbage();
  testFindFirstExistingSkipsNonExecutables();
  return 0;
}
