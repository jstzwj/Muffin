#include "image/CustomCommandUploader.h"

#include <QCoreApplication>
#include <QDebug>
#include <QSettings>
#include <QString>

#include <cstdlib>

// Exercises the testable slice of CustomCommandUploader: the settings-resolution
// policy (resolvedCommand / isAvailable), mirroring PandocRunnerTest. The upload()
// path drives a real external process behind a modal progress dialog and is
// covered by manual verification, not a unit test.
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

void testUnsetCommandMeansUnavailable() {
  QSettings().remove(QStringLiteral("image/uploadCommand"));
  require(muffin::CustomCommandUploader::resolvedCommand().isEmpty(),
          QStringLiteral("Unset uploadCommand should resolve to an empty command"));
  require(!muffin::CustomCommandUploader::isAvailable(),
          QStringLiteral("With no command configured, isAvailable() should be false"));
}

void testConfiguredCommandIsHonored() {
  QSettings().setValue(QStringLiteral("image/uploadCommand"), QStringLiteral("  picgo upload  "));
  require(muffin::CustomCommandUploader::resolvedCommand() == QStringLiteral("picgo upload"),
          QStringLiteral("resolvedCommand should return the configured command, trimmed"));
  require(muffin::CustomCommandUploader::isAvailable(),
          QStringLiteral("A configured command should make isAvailable() true"));
  QSettings().remove(QStringLiteral("image/uploadCommand"));
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("Muffin"));
  QCoreApplication::setApplicationName(QStringLiteral("MuffinTests"));
  testUnsetCommandMeansUnavailable();
  testConfiguredCommandIsHonored();
  return 0;
}
