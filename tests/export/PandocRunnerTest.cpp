#include "export/PandocRunner.h"

#include <QCoreApplication>
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

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("Muffin"));
  QCoreApplication::setApplicationName(QStringLiteral("MuffinTests"));
  testEmptySettingFallsBackToSystemSearchOrBarePandoc();
  testNonExistentConfiguredPathFallsBack();
  testRealExecutablePathIsHonored();
  testFindFirstExistingReturnsFirstExecutable();
  testFindFirstExistingReturnsEmptyForGarbage();
  testFindFirstExistingSkipsNonExecutables();
  return 0;
}
