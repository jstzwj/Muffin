#include "io/ApplicationPaths.h"
#include "app/DraftRecovery.h"
#include "../TestUtils.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace muffin;

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  app.setOrganizationName("MuffinStorageTest");
  app.setApplicationName("Portable");
  QStandardPaths::setTestModeEnabled(true);
  QTemporaryDir directory;
  require(directory.isValid(), "storage fixture directory");
  const auto executable = directory.filePath("Muffin.exe");
  QString error;
  if (app.arguments().contains("--portable")) {
    QFile marker(directory.filePath("Muffin.portable"));
    require(marker.open(QIODevice::WriteOnly), "portable marker");
    marker.close();
    if (app.arguments().contains("--unwritable")) {
      QFile data(directory.filePath("Data"));
      require(data.open(QIODevice::WriteOnly), "obstruct portable profile creation");
      require(!ApplicationPaths::initialize(executable, &error) && !error.isEmpty(),
              "unwritable portable profile fails instead of falling back to registry");
      require(!ApplicationPaths::isPortable() && QSettings::defaultFormat() == QSettings::NativeFormat,
              "failed initialization does not change global profile");
      return 0;
    }
    require(ApplicationPaths::initialize(executable, &error) && ApplicationPaths::isPortable(), "portable profile");
    const auto root = QDir(directory.filePath("Data")).canonicalPath();
    require(ApplicationPaths::dataDirectory() == root && ApplicationPaths::localDataDirectory() == root &&
                ApplicationPaths::temporaryDirectory() == root + "/temp",
            "all application-owned data uses the portable directory");
    QSettings settings;
    settings.setValue("theme", "night");
    settings.sync();
    require(settings.format() == QSettings::IniFormat && settings.fileName().startsWith(root + "/") &&
                settings.status() == QSettings::NoError && QFile::exists(settings.fileName()),
            "portable settings persist to an INI file beside the app");
    require(DraftRecovery().snapshot("unsaved text", {}) && QFileInfo::exists(root + "/drafts"),
            "portable recovery drafts stay with profile");
    return 0;
  }
  require(ApplicationPaths::initialize(executable, &error) && !ApplicationPaths::isPortable(),
          "unmarked installed copy keeps its existing settings and directories");
  require(!QFile::exists(directory.filePath("Data")), "normal startup creates no portable data");
  for (const QStringList args : {QStringList{"--portable"}, QStringList{"--portable", "--unwritable"}}) {
    QProcess child;
    child.start(app.applicationFilePath(), args);
    require(child.waitForFinished(15000) && child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
            QString::fromUtf8(child.readAllStandardError()));
  }
  return 0;
}
