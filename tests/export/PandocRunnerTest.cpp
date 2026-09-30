#include "export/PandocRunner.h"

#include <QApplication>
#include <QDir>
#include <QProgressDialog>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <QDebug>
#include <QFileInfo>
#include <QSettings>
#include <QString>

#include <cstdlib>

// Verifies PandocRunner's resolution policy (the piece that is unit-testable
// without a live Pandoc + GUI loop): an empty/invalid configured path falls
// through to the system search of well-known install locations, then the bare
// "pandoc"; a real executable path is honored verbatim. findFirstExistingExecutable
// is the deterministic core and is exercised directly. isAvailable()/run() need
// a live Pandoc + GUI event loop and are covered by manual verification instead.
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

void testWorkingDirectoryAndProgressLabelAreIndependent() {
  QTemporaryDir dir;
  require(dir.isValid(), QStringLiteral("Fixture directory missing"));
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
                                               {.workDir = dir.path(), .progressLabel = label});
  require(result.ran && result.exitCode == 0, QStringLiteral("Import process should start"));
  require(QString::fromUtf8(result.out) == dir.path(), QStringLiteral("Wrong import working directory"));
  require(sawLabel, QStringLiteral("Import label should be shown as progress text"));
  QSettings().remove(QStringLiteral("export/pandocPath"));
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
  testWorkingDirectoryAndProgressLabelAreIndependent();
  testEmptySettingFallsBackToSystemSearchOrBarePandoc();
  testNonExistentConfiguredPathFallsBack();
  testRealExecutablePathIsHonored();
  testFindFirstExistingReturnsFirstExecutable();
  testFindFirstExistingReturnsEmptyForGarbage();
  testFindFirstExistingSkipsNonExecutables();
  return 0;
}
