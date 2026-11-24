#include "diagnostics/PerfFileLogger.h"

#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

#include <cstdio>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace muffin::diag {
namespace {

constexpr char kPerfFamily[] = "muffin.perf";

QFile perfFile;
QMutex perfFileMutex;
QtMessageHandler previousHandler = nullptr;

// "muffin.perf" itself or one of its children, e.g. "muffin.perf.detail".
bool belongsToPerfFamily(const char* category) {
  if (qstrncmp(category, kPerfFamily, int(sizeof(kPerfFamily) - 1)) != 0)
    return false;
  const char next = category[sizeof(kPerfFamily) - 1];
  return next == '\0' || next == '.';
}

// What Qt's own handler would have done with the message: the configured
// pattern (QT_MESSAGE_PATTERN) on stderr, and the debugger when one is
// attached. Qt does not expose its default handler, so a custom handler that
// took its place has to reproduce this part.
void writeDefaultOutput(QtMsgType type, const QMessageLogContext& context, const QString& message) {
  const QString line = qFormatLogMessage(type, context, message);
  if (line.isEmpty())
    return;
#if defined(Q_OS_WIN)
  OutputDebugStringW(reinterpret_cast<const wchar_t*>(line.utf16()));
  OutputDebugStringW(L"\n");
#endif
  const QByteArray local = (line + QLatin1Char('\n')).toLocal8Bit();
  std::fwrite(local.constData(), 1, size_t(local.size()), stderr);
  std::fflush(stderr);
}

void perfFileMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
  if (perfFile.isOpen() && belongsToPerfFamily(context.category)) {
    QMutexLocker locker(&perfFileMutex);
    QTextStream stream(&perfFile);
    stream << QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz")) << ' ' << message << '\n';
    stream.flush();
    return;
  }
  if (previousHandler)
    previousHandler(type, context, message);
  else
    writeDefaultOutput(type, context, message);
}

} // namespace

void PerfFileLogger::install(const QString& filePath) {
  if (filePath.isEmpty())
    return;
  QMutexLocker locker(&perfFileMutex);
  if (perfFile.isOpen())
    return;
  perfFile.setFileName(filePath);
  if (!perfFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    return;
  // Ask for the default handler first and install this one last: a message that
  // arrives in between reaches the default handler instead of being dropped.
  previousHandler = qInstallMessageHandler(nullptr);
  qInstallMessageHandler(perfFileMessageHandler);
}

void PerfFileLogger::uninstall() {
  QMutexLocker locker(&perfFileMutex);
  if (!perfFile.isOpen())
    return;
  qInstallMessageHandler(previousHandler);
  previousHandler = nullptr;
  perfFile.close();
}

bool PerfFileLogger::isInstalled() {
  QMutexLocker locker(&perfFileMutex);
  return perfFile.isOpen();
}

} // namespace muffin::diag
