#include "app/MainWindow.h"

#include "app/LanguageManager.h"
#include "theme/FontRendering.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
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

  muffin::LanguageManager::instance().initialize();

  QCommandLineParser parser;
  parser.setApplicationDescription(QCoreApplication::translate(
      "main",
      "A fast, lightweight, native Markdown editor built with C++ and Qt 6."));
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addPositionalArgument(
      QStringLiteral("file"),
      QCoreApplication::translate("main", "Markdown or text file to open."));
  parser.process(app);

  muffin::MainWindow window;
  const QStringList positionalArguments = parser.positionalArguments();
  if (!positionalArguments.isEmpty()) {
    window.openFile(QFileInfo(positionalArguments.first()).absoluteFilePath());
  } else if (!window.offerDraftRecovery()) {
    // No draft was restored (none existed, all discarded, or deferred) — fall
    // back to the configured startup behavior.
    window.restoreStartupFile();
  }
  window.show();

  return QApplication::exec();
}
