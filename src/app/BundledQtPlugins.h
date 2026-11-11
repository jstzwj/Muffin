#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace muffin {

// Call before constructing Q(Gui)Application, which loads input methods.
// qt.conf relocates Qt's default path but still admits QT_PLUGIN_PATH. A
// self-contained Linux bundle must not mix in the desktop's private Qt ABI.
// Leave development builds and ordinary system installations untouched.
inline void initializeBundledQtPlugins() {
#if defined(Q_OS_LINUX)
  const auto executable = QFileInfo(QStringLiteral("/proc/self/exe")).canonicalFilePath();
  const QDir directory(QFileInfo(executable).absolutePath());
  if (executable.isEmpty() || !directory.exists(QStringLiteral("qt.conf")))
    return;
  const QSettings configuration(directory.filePath(QStringLiteral("qt.conf")), QSettings::IniFormat);
  if (configuration.value(QStringLiteral("Muffin/SelfContainedPlugins")).toBool()) {
    QCoreApplication::setLibraryPaths({directory.absolutePath()});
  }
#endif
}

} // namespace muffin
