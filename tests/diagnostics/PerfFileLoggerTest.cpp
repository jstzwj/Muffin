// MUFFIN_PERF_LOG used to replace Qt's message handler with one that wrote muffin.perf
// messages to a file and dropped everything else, so the one situation in which someone
// turns the performance log on was the one in which warnings and errors disappeared.

#include "diagnostics/PerfFileLogger.h"

#include "../TestUtils.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>
#include <QTemporaryDir>

#include <cstdio>

#if defined(Q_OS_WIN)
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

Q_LOGGING_CATEGORY(perfCategory, "muffin.perf")
Q_LOGGING_CATEGORY(perfDetailCategory, "muffin.perf.detail")
Q_LOGGING_CATEGORY(plainCategory, "muffin.test.perflogger")

namespace {

QMutex captureMutex;
QStringList captured;

void capturingHandler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
  Q_UNUSED(type);
  QMutexLocker locker(&captureMutex);
  captured << QStringLiteral("%1|%2").arg(QLatin1String(context.category), message);
}

QStringList takeCaptured() {
  QMutexLocker locker(&captureMutex);
  QStringList result;
  result.swap(captured);
  return result;
}

QString readFile(const QString& path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly), QStringLiteral("should open %1").arg(path));
  return QString::fromUtf8(file.readAll());
}

// The C runtime descriptors behind `stderr`, which is where Qt's default handler writes.
namespace descriptors {

constexpr int kStderr = 2;

inline int duplicate(int descriptor) {
#if defined(Q_OS_WIN)
  return _dup(descriptor);
#else
  return ::dup(descriptor);
#endif
}

inline int openForWriting(const QString& path) {
#if defined(Q_OS_WIN)
  int descriptor = -1;
  if (_wsopen_s(&descriptor, reinterpret_cast<const wchar_t*>(path.utf16()),
                _O_WRONLY | _O_CREAT | _O_TRUNC | _O_TEXT, _SH_DENYNO, _S_IREAD | _S_IWRITE) != 0)
    return -1;
  return descriptor;
#else
  return ::open(QFile::encodeName(path).constData(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
#endif
}

inline void replace(int target, int source) {
#if defined(Q_OS_WIN)
  _dup2(source, target);
#else
  ::dup2(source, target);
#endif
}

inline void close(int descriptor) {
#if defined(Q_OS_WIN)
  _close(descriptor);
#else
  ::close(descriptor);
#endif
}

} // namespace descriptors

// Points the C runtime's stderr at a file for the lifetime of the object.
class StderrCapture {
public:
  explicit StderrCapture(const QString& path) : saved_(descriptors::duplicate(descriptors::kStderr)) {
    const int target = descriptors::openForWriting(path);
    require(target >= 0 && saved_ >= 0, QStringLiteral("stderr capture should have two descriptors"));
    descriptors::replace(descriptors::kStderr, target);
    descriptors::close(target);
  }

  ~StderrCapture() {
    std::fflush(stderr);
    descriptors::replace(descriptors::kStderr, saved_);
    descriptors::close(saved_);
  }

  StderrCapture(const StderrCapture&) = delete;
  StderrCapture& operator=(const StderrCapture&) = delete;

private:
  int saved_ = -1;
};

// With another handler installed before it, everything outside the perf family goes there.
void testNonPerfMessagesReachThePreviousHandler() {
  QTemporaryDir directory;
  require(directory.isValid(), QStringLiteral("temporary directory should exist"));
  const QString logPath = directory.filePath(QStringLiteral("perf.log"));

  qInstallMessageHandler(capturingHandler);
  muffin::diag::PerfFileLogger::install(logPath);
  require(muffin::diag::PerfFileLogger::isInstalled(), QStringLiteral("logger should install"));
  qCWarning(plainCategory) << "plain marker";
  qCWarning(perfCategory) << "perf marker";
  qCWarning(perfDetailCategory) << "detail marker";
  muffin::diag::PerfFileLogger::uninstall();
  qInstallMessageHandler(nullptr);

  const QStringList forwarded = takeCaptured();
  require(forwarded.size() == 1 && forwarded.first().contains(QStringLiteral("plain marker")),
          QStringLiteral("the plain warning should reach the previous handler exactly once (got %1)")
              .arg(forwarded.join(QStringLiteral(" / "))));
  const QString log = readFile(logPath);
  require(log.contains(QStringLiteral("perf marker")) && log.contains(QStringLiteral("detail marker")),
          QStringLiteral("the perf family should be appended to the file"));
  require(!log.contains(QStringLiteral("plain marker")), QStringLiteral("other messages never reach the file"));
}

// Without a custom handler the previous destination is Qt's default one: stderr.
void testNonPerfMessagesReachStderrWithoutACustomHandler() {
  QTemporaryDir directory;
  require(directory.isValid(), QStringLiteral("temporary directory should exist"));
  const QString logPath = directory.filePath(QStringLiteral("perf.log"));
  const QString stderrPath = directory.filePath(QStringLiteral("stderr.log"));

  qInstallMessageHandler(nullptr);
  muffin::diag::PerfFileLogger::install(logPath);
  {
    StderrCapture capture(stderrPath);
    qCWarning(plainCategory) << "stderr marker";
    qCWarning(perfCategory) << "perf marker";
  }
  muffin::diag::PerfFileLogger::uninstall();

  const QString err = readFile(stderrPath);
  require(err.contains(QStringLiteral("stderr marker")),
          QStringLiteral("a warning should still reach stderr (got '%1')").arg(err));
  require(!err.contains(QStringLiteral("perf marker")), QStringLiteral("perf messages stay out of stderr"));
  require(readFile(logPath).contains(QStringLiteral("perf marker")), QStringLiteral("perf messages reach the file"));
}

// uninstall() puts the earlier handler back in charge of every message.
void testUninstallRestoresThePreviousHandler() {
  QTemporaryDir directory;
  require(directory.isValid(), QStringLiteral("temporary directory should exist"));
  const QString logPath = directory.filePath(QStringLiteral("perf.log"));

  qInstallMessageHandler(capturingHandler);
  muffin::diag::PerfFileLogger::install(logPath);
  qCWarning(perfCategory) << "before uninstall";
  muffin::diag::PerfFileLogger::uninstall();
  require(!muffin::diag::PerfFileLogger::isInstalled(), QStringLiteral("logger should be uninstalled"));
  qCWarning(perfCategory) << "after uninstall";
  qInstallMessageHandler(nullptr);

  const QString log = readFile(logPath);
  require(log.contains(QStringLiteral("before uninstall")) && !log.contains(QStringLiteral("after uninstall")),
          QStringLiteral("the file should stop receiving messages after uninstall"));
  const QStringList forwarded = takeCaptured();
  require(forwarded.size() == 1 && forwarded.first().contains(QStringLiteral("after uninstall")),
          QStringLiteral("the previous handler should receive every message again"));
}

} // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
#define RUN_TEST(test) runTest(#test, test)
  RUN_TEST(testNonPerfMessagesReachThePreviousHandler);
  RUN_TEST(testNonPerfMessagesReachStderrWithoutACustomHandler);
  RUN_TEST(testUninstallRestoresThePreviousHandler);
#undef RUN_TEST
  return 0;
}
