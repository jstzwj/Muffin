#include "app/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

namespace {

QFile* perfLogFile = nullptr;
QMutex perfLogMutex;

void muffinMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
  Q_UNUSED(type);

  if (!perfLogFile || !perfLogFile->isOpen() || QStringView(QString::fromUtf8(context.category)) != QStringLiteral("muffin.perf")) {
    return;
  }

  QMutexLocker locker(&perfLogMutex);
  QTextStream stream(perfLogFile);
  stream << QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz")) << ' ' << message << '\n';
  stream.flush();
}

void installPerfFileLogger() {
  const QByteArray logPath = qgetenv("MUFFIN_PERF_LOG");
  if (logPath.isEmpty()) {
    return;
  }

  perfLogFile = new QFile(QString::fromLocal8Bit(logPath));
  if (!perfLogFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
    delete perfLogFile;
    perfLogFile = nullptr;
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

  QCommandLineParser parser;
  parser.setApplicationDescription("A fast native Markdown editor built with C++ and Qt 6 Widgets.");
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addPositionalArgument("file", "Markdown or text file to open.");
  parser.process(app);

  muffin::MainWindow window;
  const QStringList positionalArguments = parser.positionalArguments();
  if (!positionalArguments.isEmpty()) {
    window.openFile(QFileInfo(positionalArguments.first()).absoluteFilePath());
  }
  window.show();

  return QApplication::exec();
}
