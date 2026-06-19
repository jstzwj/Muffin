#include "export/PandocRunner.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QSettings>
#include <QString>

#include <cstdlib>

// Verifies PandocRunner::resolvedExecutable()'s resolution policy (the one piece
// of logic that is unit-testable without an actual Pandoc install + GUI loop):
// unset/empty → bare "pandoc"; a non-existent configured path → fallback; a real
// executable path → honored. isAvailable()/run() need a live Pandoc + GUI event
// loop and are covered by manual verification instead.
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

void testEmptySettingFallsBackToBarePandoc() {
  QSettings().remove(QStringLiteral("export/pandocPath"));
  require(muffin::PandocRunner::resolvedExecutable() == QStringLiteral("pandoc"),
          QStringLiteral("Empty/unset pandocPath should fall back to bare 'pandoc'"));
}

void testNonExistentConfiguredPathFallsBack() {
  QSettings().setValue(QStringLiteral("export/pandocPath"), QStringLiteral("/no/such/pandoc-binary"));
  require(muffin::PandocRunner::resolvedExecutable() == QStringLiteral("pandoc"),
          QStringLiteral("A non-existent configured path should fall back to 'pandoc'"));
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

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("Muffin"));
  QCoreApplication::setApplicationName(QStringLiteral("MuffinTests"));
  testEmptySettingFallsBackToBarePandoc();
  testNonExistentConfiguredPathFallsBack();
  testRealExecutablePathIsHonored();
  return 0;
}
