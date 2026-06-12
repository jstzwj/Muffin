#include "app/MainWindow.h"

#include "app/LanguageManager.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
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

  perfLogFile.setFileName(QString::fromLocal8Bit(logPath));
  if (!perfLogFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
    return;
  }

  qInstallMessageHandler(muffinMessageHandler);
}

}  // namespace

int main(int argc, char *argv[]) {
  installPerfFileLogger();

  QApplication app(argc, argv);
  QApplication::setApplicationName("Muffin");
  QApplication::setOrganizationName("Muffin");
  QApplication::setApplicationVersion(QStringLiteral(MUFFIN_VERSION));
  // Application window/taskbar icon for every platform. This is the primary
  // mechanism on Linux; on Windows/macOS the .exe/.app icon is also embedded,
  // and this makes the decoration appear immediately at launch.
  QApplication::setWindowIcon(QIcon(QStringLiteral(":/app/muffin.png")));
  muffin::LanguageManager::instance().initialize();

  QCommandLineParser parser;
  parser.setApplicationDescription(QCoreApplication::translate(
      "main",
      "A fast native Markdown editor built with C++ and Qt 6 Widgets."));
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
  }
  window.show();

  return QApplication::exec();
}
