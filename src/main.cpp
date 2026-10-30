#include "app/MainWindow.h"

#include "app/ApplicationInstance.h"
#include "app/DwmPopupBorderFilter.h"
#include "app/FileOpenDispatcher.h"
#include "app/LanguageManager.h"
#include "app/WindowsIntegration.h"
#include "app/WindowsUpdater.h"
#include "editor/EditorAccessibility.h"
#include "theme/FontRendering.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QMessageBox>
#include <QTimer>
#include <QStandardPaths>
#include <QTextStream>

namespace {

QFile perfLogFile;
QMutex perfLogMutex;

void muffinMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
  Q_UNUSED(type);

  if (!perfLogFile.isOpen() || QStringView(QString::fromUtf8(context.category)) != QStringLiteral("muffin.perf")) {
    return;
  }

  QMutexLocker locker(&perfLogMutex);
  QTextStream stream(&perfLogFile);
  stream << QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz")) << ' ' << message << '\n';
  stream.flush();
}

void installPerfFileLogger() {
  const QByteArray logPath = qgetenv("MUFFIN_PERF_LOG");
  if (logPath.isEmpty()) {
    return;
  }

  QString path = QString::fromLocal8Bit(logPath);
  // A relative path (e.g. MUFFIN_PERF_LOG=perf.log) resolves against the process working
  // directory, which during development is typically the repo root — silently littering
  // perf traces into the source tree. Anchor relative paths under the OS temp dir instead.
  if (QFileInfo(path).isRelative()) {
    path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
               .filePath(QStringLiteral("muffin/%1").arg(path));
    QDir().mkpath(QFileInfo(path).absolutePath());
  }

  perfLogFile.setFileName(path);
  if (!perfLogFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
    return;
  }

  qInstallMessageHandler(muffinMessageHandler);
}

} // namespace

int main(int argc, char* argv[]) {
  installPerfFileLogger();

  // Qt 6 defaults to PassThrough (fractional) scaling and Per-Monitor V2 DPI
  // awareness. Set it explicitly so a stray env var or qt.conf can't silently
  // change it and reintroduce blurry chrome.
  QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

  QApplication app(argc, argv);
  QApplication::setApplicationName("Muffin");
  QApplication::setOrganizationName("Muffin");
  QApplication::setApplicationVersion(QStringLiteral(MUFFIN_VERSION));
  // Application window/taskbar icon for every platform. This is the primary
  // mechanism on Linux; on Windows/macOS the .exe/.app icon is also embedded,
  // and this makes the decoration appear immediately at launch.
  QApplication::setWindowIcon(QIcon(QStringLiteral(":/app/muffin.png")));

  // Keep application chrome on the same screen-rasterization policy as the
  // document, source editor, and inline HTML text.
  QFont uiFont = QApplication::font();
  muffin::font_rendering::configureForScreen(uiFont);
  QApplication::setFont(uiFont);

  // Windows 11 strokes a dark DWM border around Qt::Popup top-levels (menus,
  // combo dropdowns) on top of the theme's own QSS hairline; clear it so
  // popups show only the themed border. No-op on other platforms.
  muffin::DwmPopupBorderFilter::install(app);

  muffin::LanguageManager::instance().initialize();
  muffin::WindowsIntegration::refreshLocalizedLabels();
  QObject::connect(&muffin::LanguageManager::instance(), &muffin::LanguageManager::languageChanged, &app,
                   [] { muffin::WindowsIntegration::refreshLocalizedLabels(); });

  // Screen-reader adapters for the two editor canvases (rendered + source mode). Installed
  // before any editor widget exists; inert until something queries accessibility.
  muffin::installEditorAccessibility();

  // Must be installed before the window exists: the odoc event for a
  // double-clicked file can arrive as early as the first event-loop iteration.
  muffin::FileOpenDispatcher fileOpenDispatcher(&app);

  QCommandLineParser parser;
  parser.setApplicationDescription(
      QCoreApplication::translate("main", "A fast, lightweight, native Markdown editor built with C++ and Qt 6."));
  parser.addHelpOption();
  parser.addVersionOption();
  // --folder <path> is the form used by the Explorer "Open with Muffin" verb
  // for directories and folder backgrounds (it passes %1 / %V). A positional
  // directory argument is treated the same way for ergonomic command-line use.
  const QCommandLineOption folderOption(
      QStringLiteral("folder"), QCoreApplication::translate("main", "Open <folder> in the sidebar file browser."),
      QStringLiteral("folder"));
  parser.addOption(folderOption);
  const QCommandLineOption newWindowOption(QStringLiteral("new-window"),
                                           QCoreApplication::translate("muffin::MainWindow", "Open in New Window"));
  parser.addOption(newWindowOption);
  const QCommandLineOption updateResultOption(QStringLiteral("update-result"),
                                              QStringLiteral("Internal update result."), QStringLiteral("code"));
  parser.addOption(updateResultOption);
  parser.addPositionalArgument(QStringLiteral("file"),
                               QCoreApplication::translate("main", "Markdown or text file (or folder) to open."));
  parser.process(app);

  const QStringList positionalArguments = parser.positionalArguments();
  const QString folderArg = parser.value(folderOption);
  QStringList files;
  bool hasFolder = !folderArg.isEmpty();
  for (const QString& argument : positionalArguments) {
    const QFileInfo info(argument);
    files.append(info.absoluteFilePath());
    hasFolder = hasFolder || info.isDir();
  }
  muffin::ApplicationInstance instance(muffin::ApplicationInstance::profileIdentity(), &app);
  auto& updater = muffin::WindowsUpdater::instance();
  QObject::connect(&instance, &muffin::ApplicationInstance::acceptingFileRequest, &updater,
                   &muffin::WindowsUpdater::interruptRestart);
  QObject::connect(&updater, &muffin::WindowsUpdater::preparingRestart, &instance, [&instance, &updater] {
    if (instance.hasPendingFileRequests())
      updater.interruptRestart();
  });
  // Folder launches still create an independent workspace. Starting without
  // arguments also keeps the familiar ability to create another blank window.
  const bool reuseWindow =
      muffin::file_open_preferences::openInTabs() && !parser.isSet(newWindowOption) && !hasFolder && !files.isEmpty();
  if (!instance.listen() && reuseWindow) {
    if (instance.forwardFiles(files))
      return 0;
    // The previous process may have exited during the handshake. Become the
    // listener if possible; otherwise open locally rather than lose the files.
    instance.listen();
  }
  QObject::connect(&instance, &muffin::ApplicationInstance::filesRequested, &fileOpenDispatcher,
                   &muffin::FileOpenDispatcher::openFiles);

  if (parser.isSet(updateResultOption)) {
    bool valid = false;
    const auto code = parser.value(updateResultOption).toULongLong(&valid);
    if (valid && code != 0 && code != 3010 && code != 1641) {
      QTimer::singleShot(0, &app, [code] {
        QMessageBox::warning(
            QApplication::activeWindow(), QCoreApplication::translate("muffin::WindowsUpdater", "Update Muffin"),
            QCoreApplication::translate(
                "muffin::WindowsUpdater",
                "The update was canceled or could not be installed (code %1). Your session has been restored. You can try again from Help > Check for Updates.")
                .arg(code));
      });
    }
  }
  if (positionalArguments.isEmpty() && folderArg.isEmpty()) {
    if (auto* restored = muffin::WindowsUpdater::restoreAfterUpdate()) {
      fileOpenDispatcher.setInitialWindow(restored);
      return QApplication::exec();
    }
  }

  muffin::MainWindow window;
  bool openedSomething = fileOpenDispatcher.setInitialWindow(&window);
  if (!folderArg.isEmpty()) {
    window.openFolderAtPath(QFileInfo(folderArg).absoluteFilePath());
    openedSomething = true;
  }
  for (const QString& argument : positionalArguments) {
    const QFileInfo info(argument);
    if (info.isDir()) {
      window.openFolderAtPath(info.absoluteFilePath());
      openedSomething = true;
    } else {
      openedSomething = window.openFile(info.absoluteFilePath()) || openedSomething;
    }
  }
  if (!openedSomething && !window.offerDraftRecovery()) {
    // No draft was restored (none existed, all discarded, or deferred) — fall
    // back to the configured startup behavior.
    window.restoreStartupFile();
  }
  window.show();

  return QApplication::exec();
}
