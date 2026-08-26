#include "app/MainWindow.h"

#include "app/DwmPopupBorderFilter.h"
#include "app/LanguageManager.h"
#include "app/WindowsIntegration.h"
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
#include <QFileOpenEvent>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>

namespace {

// macOS delivers files opened via Finder/LaunchServices (double-click, "Open
// With", drag-onto-icon) as AppleEvents, which Qt translates into QFileOpenEvent
// — they never appear in argv. Without this filter a double-clicked .md launches
// Muffin with a blank document: the positional-argument path below is the
// Windows/Linux mechanism only. The same event also covers a second file opened
// while Muffin is already running (LaunchServices routes the odoc to the
// existing instance instead of spawning a new process).
class FileOpenEventForwarder : public QObject {
public:
  explicit FileOpenEventForwarder(QObject* parent) : QObject(parent) {}

  // The window is constructed after QApplication (and after this filter is
  // installed); an event that beat it here is replayed from `pending_`.
  void setWindow(muffin::MainWindow* window) {
    window_ = window;
    if (window_ && !pending_.isEmpty()) {
      const QString path = pending_;
      pending_.clear();
      open(path);
    }
  }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    if (event->type() == QEvent::FileOpen) {
      const auto* openEvent = static_cast<QFileOpenEvent*>(event);
      // A custom URL scheme (e.g. muffin://) would land here too; only local
      // files/folders are meaningful to open today.
      QString path = openEvent->file();
      if (path.isEmpty()) {
        path = openEvent->url().toLocalFile();
      }
      if (!path.isEmpty()) {
        if (window_) {
          open(path);
        } else {
          pending_ = path;  // arrived before the window existed — replay later
        }
      }
      return true;
    }
    return QObject::eventFilter(watched, event);
  }

private:
  void open(const QString& path) {
    const QFileInfo info(path);
    if (info.isDir()) {
      window_->openFolderAtPath(info.absoluteFilePath());
    } else {
      window_->openFile(info.absoluteFilePath());
    }
  }

  muffin::MainWindow* window_ = nullptr;
  QString pending_;
};

}  // namespace

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

}  // namespace

int main(int argc, char *argv[]) {
  installPerfFileLogger();

  // Qt 6 defaults to PassThrough (fractional) scaling and Per-Monitor V2 DPI
  // awareness. Set it explicitly so a stray env var or qt.conf can't silently
  // change it and reintroduce blurry chrome.
  QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
      Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

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

  // Screen-reader adapters for the two editor canvases (rendered + source mode). Installed
  // before any editor widget exists; inert until something queries accessibility.
  muffin::installEditorAccessibility();

  // Must be installed before the window exists: the odoc event for a
  // double-clicked file can arrive as early as the first event-loop iteration.
  FileOpenEventForwarder fileOpenForwarder(&app);
  app.installEventFilter(&fileOpenForwarder);

  QCommandLineParser parser;
  parser.setApplicationDescription(QCoreApplication::translate(
      "main",
      "A fast, lightweight, native Markdown editor built with C++ and Qt 6."));
  parser.addHelpOption();
  parser.addVersionOption();
  // --folder <path> is the form used by the Explorer "Open with Muffin" verb
  // for directories and folder backgrounds (it passes %1 / %V). A positional
  // directory argument is treated the same way for ergonomic command-line use.
  const QCommandLineOption folderOption(
      QStringLiteral("folder"),
      QCoreApplication::translate("main", "Open <folder> in the sidebar file browser."), QStringLiteral("folder"));
  parser.addOption(folderOption);
  parser.addPositionalArgument(
      QStringLiteral("file"),
      QCoreApplication::translate("main", "Markdown or text file (or folder) to open."));
  parser.process(app);

  muffin::MainWindow window;
  fileOpenForwarder.setWindow(&window);
  const QStringList positionalArguments = parser.positionalArguments();
  const QString folderArg = parser.value(folderOption);
  bool openedSomething = false;
  if (!folderArg.isEmpty()) {
    window.openFolderAtPath(QFileInfo(folderArg).absoluteFilePath());
    openedSomething = true;
  } else if (!positionalArguments.isEmpty()) {
    const QFileInfo firstArg(positionalArguments.first());
    if (firstArg.isDir()) {
      window.openFolderAtPath(firstArg.absoluteFilePath());
    } else {
      window.openFile(firstArg.absoluteFilePath());
    }
    openedSomething = true;
  }
  if (!openedSomething && !window.offerDraftRecovery()) {
    // No draft was restored (none existed, all discarded, or deferred) — fall
    // back to the configured startup behavior.
    window.restoreStartupFile();
  }
  window.show();

#ifdef Q_OS_WIN
  // If the installer registered Muffin and the user opted into "set as
  // default", ask once whether to open the system Default Apps page —
  // jumping there unasked feels like the OS hijacking the session, so the
  // redirect only happens on consent (Windows 8+ forbids a silent default
  // change either way). Clear the flag regardless so it never repeats.
  if (muffin::WindowsIntegration::shouldPromptForDefault()) {
    muffin::WindowsIntegration::clearPromptForDefault();
    const auto answer = QMessageBox::question(
        &window, QCoreApplication::translate("main", "Default Markdown editor"),
        QCoreApplication::translate(
            "main",
            "Muffin is registered as an editor for Markdown files.\n"
            "Make it your default editor now?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer == QMessageBox::Yes) {
      muffin::WindowsIntegration::openDefaultAppsSettings();
    }
  }
#endif

  return QApplication::exec();
}
