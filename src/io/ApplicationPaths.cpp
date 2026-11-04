#include "io/ApplicationPaths.h"
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace {
QString portableRoot;
}

bool muffin::ApplicationPaths::initialize(const QString& executablePath, QString* error) {
  const QDir application(QFileInfo(executablePath).absolutePath());
  const QFileInfo marker(application.filePath(QStringLiteral("Muffin.portable")));
  if (!marker.exists())
    return true;
  const QString root = application.filePath(QStringLiteral("Data"));
  QTemporaryFile probe(QDir(root).filePath(QStringLiteral(".write-check-XXXXXX")));
  if (!marker.isFile() || !QDir().mkpath(root) || !probe.open()) {
    if (error)
      *error = QDir::toNativeSeparators(root) + QLatin1Char('\n') + probe.errorString();
    return false; // Never fall back to the registry if the portable drive is read-only.
  }
  portableRoot = QDir(root).canonicalPath();
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, portableRoot + QStringLiteral("/settings"));
  // Prevent fallback reads from a machine-wide INI profile as well.
  QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, portableRoot + QStringLiteral("/settings"));
  return true;
}

bool muffin::ApplicationPaths::isPortable() {
  return !portableRoot.isEmpty();
}

QString muffin::ApplicationPaths::dataDirectory() {
  return isPortable() ? portableRoot : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString muffin::ApplicationPaths::localDataDirectory() {
  return isPortable() ? portableRoot : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

QString muffin::ApplicationPaths::temporaryDirectory() {
  if (!isPortable())
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  const auto path = portableRoot + QStringLiteral("/temp");
  QDir().mkpath(path);
  return path;
}
