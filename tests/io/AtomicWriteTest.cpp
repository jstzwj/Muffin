#include "io/AtomicWrite.h"
#include "../TestUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <thread>

namespace {

using muffin::AtomicWrite;
using namespace std::chrono_literals;

QByteArray readBytes(const QString& path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly), "read atomic write fixture");
  return file.readAll();
}

// Every file in the directory, a temporary one included.
QStringList filesIn(const QTemporaryDir& directory) {
  return QDir(directory.path()).entryList(QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
}

// What the write logs, to see that a refusal it waited out leaves a trace.
QStringList* logged = nullptr;
void collectLog(QtMsgType type, const QMessageLogContext& context, const QString& message) {
  if (logged && type == QtWarningMsg && QString::fromLatin1(context.category) == QStringLiteral("muffin.io"))
    logged->append(message);
}

void writesAndReplaces() {
  QTemporaryDir directory;
  require(directory.isValid(), "atomic write fixture directory");
  const auto name = QString::fromUtf8("中文 note.md");
  const auto path = directory.filePath(name);
  QString error = QStringLiteral("stale");
  require(AtomicWrite::write(path, "first", &error) == AtomicWrite::Result::Written && error.isEmpty(),
          "a new file is written, and a stale error is not left standing");
  require(AtomicWrite::write(path, "a longer replacement", nullptr) == AtomicWrite::Result::Written &&
              readBytes(path) == "a longer replacement",
          "an existing file is replaced whole");
  require(AtomicWrite::write(path, QByteArray(), nullptr) == AtomicWrite::Result::Written && readBytes(path).isEmpty(),
          "an empty write empties the file");
  require(AtomicWrite::write(path, [](QSaveFile& file) { return file.write("a") == 1 && file.write("b") == 1; }) ==
                  AtomicWrite::Result::Written &&
              readBytes(path) == "ab",
          "a writer fills the file in several writes");
  require(filesIn(directory) == QStringList{name}, "no temporary file is left behind");
}

// Only the system's refusal is waited for. What will not pass — a read-only
// file, a folder that is not there, a name the system does not take — is
// reported as it always was.
void failuresAreReportedAtOnce() {
  QTemporaryDir directory;
  require(directory.isValid(), "atomic write fixture directory");
  const auto path = directory.filePath(QStringLiteral("note.md"));
  const auto writable = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser |
                        QFileDevice::WriteUser | QFileDevice::ReadGroup | QFileDevice::ReadOther;
  require(AtomicWrite::write(path, "original", nullptr) == AtomicWrite::Result::Written, "write the fixture");
  require(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::ReadUser | QFileDevice::ReadGroup |
                                          QFileDevice::ReadOther),
          "make the fixture read-only");

  QString error;
  AtomicWrite::Budget budget(1000ms); // the default: long enough for a wait to show
  QElapsedTimer elapsed;
  elapsed.start();
  if (!QFileInfo(path).isWritable()) { // root writes read-only files
    require(AtomicWrite::write(path, "never written", &error, &budget) == AtomicWrite::Result::Failed &&
                error.contains(QStringLiteral("not writable")),
            "a read-only file is reported at once");
  }
  require(AtomicWrite::write(directory.filePath(QStringLiteral("gone/note.md")), "x", &error, &budget) ==
                  AtomicWrite::Result::Failed &&
              !error.isEmpty(),
          "a folder that is not there is reported at once");
  require(AtomicWrite::write(path + QStringLiteral("/inside.md"), "x", &error, &budget) == AtomicWrite::Result::Failed,
          "a file is not a folder, and that is reported at once");
#ifdef Q_OS_WIN
  require(AtomicWrite::write(directory.filePath(QStringLiteral("a<b.md")), "x", &error, &budget) ==
                  AtomicWrite::Result::Failed &&
              !error.isEmpty(),
          "a name the system does not take is reported at once");
#endif
  require(QFile::setPermissions(path, writable) && readBytes(path) == "original", "nothing was written");
  require(elapsed.elapsed() < 500, "what will not pass is not waited for");
  require(filesIn(directory) == QStringList{QStringLiteral("note.md")}, "nothing is left behind, not even a probe");
}

// A caller that turns a write down is obeyed at once, with its own reason.
void turnedDownWrites() {
  QTemporaryDir directory;
  require(directory.isValid(), "atomic write fixture directory");
  const auto path = directory.filePath(QStringLiteral("note.md"));
  require(AtomicWrite::write(path, "original", nullptr) == AtomicWrite::Result::Written, "write the fixture");
  QString error;
  AtomicWrite::Budget budget(1000ms);
  QElapsedTimer elapsed;
  elapsed.start();
  int checks = 0;
  require(AtomicWrite::write(path, "never installed", &error, &budget,
                             [&] {
                               ++checks;
                               return false;
                             }) == AtomicWrite::Result::Declined &&
              error.isEmpty() && checks == 1,
          "a file that is no longer current turns the write down, once");
  require(AtomicWrite::write(path,
                             [](QSaveFile& file) {
                               file.write("partial");
                               return false;
                             }) == AtomicWrite::Result::Declined &&
              error.isEmpty(),
          "a writer that turns the write down is obeyed");
  require(AtomicWrite::write(path,
                             [&error](QSaveFile&) {
                               error = QStringLiteral("turned down on purpose");
                               return false;
                             }) == AtomicWrite::Result::Declined &&
              error == QStringLiteral("turned down on purpose"),
          "and its reason is the one reported");
  require(readBytes(path) == "original" && elapsed.elapsed() < 500 &&
              filesIn(directory) == QStringList{QStringLiteral("note.md")},
          "a turned-down write neither changes the file, nor waits, nor leaves anything behind");
}

#if defined(Q_OS_WIN)
// A reader without delete sharing holds the file the way a scanner or an
// indexer does: the rename that would replace the file is denied until it
// lets go (TestUtils.h provides windows.h).
HANDLE holdTarget(const QString& path) {
  const HANDLE handle =
      CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()), GENERIC_READ,
                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  require(handle != INVALID_HANDLE_VALUE, "hold the target with a reader that does not share deletion");
  return handle;
}

void heldReplacements() {
  QTemporaryDir directory;
  require(directory.isValid(), "Windows sharing fixture directory");
  const auto name = QStringLiteral("contended.md");
  const auto path = directory.filePath(name);
  QString error;
  require(AtomicWrite::write(path, "original", &error) == AtomicWrite::Result::Written, error);

  QStringList messages;
  logged = &messages;
  const auto previous = qInstallMessageHandler(collectLog);

  HANDLE held = holdTarget(path);
  AtomicWrite::Budget none(0ms);
  require(AtomicWrite::write(path, "never installed", &error, &none) == AtomicWrite::Result::Refused &&
              !error.isEmpty() && readBytes(path) == "original",
          "a refused replacement is reported as refused, and leaves the file");
  require(messages.isEmpty(), "nothing was waited out, so nothing is logged");
  CloseHandle(held);

  held = holdTarget(path);
  std::thread release([held] {
    QThread::msleep(100);
    CloseHandle(held);
  });
  // A nested event loop here would run the document's autosave or reload
  // queue in the middle of a write.
  bool eventDispatched = false;
  QTimer::singleShot(0, [&eventDispatched] { eventDispatched = true; });
  const auto committed = AtomicWrite::write(path, "replacement after release", &error);
  release.join();
  require(committed == AtomicWrite::Result::Written && error.isEmpty() &&
              readBytes(path) == "replacement after release",
          "a briefly held file is replaced once it is let go");
  require(messages.size() == 1 && messages.first().contains(QStringLiteral("waited out")) &&
              messages.first().contains(path),
          "the wait is logged: " + messages.join(QStringLiteral(" | ")));
  require(!eventDispatched, "waiting for the file does not dispatch events");
  QCoreApplication::processEvents();
  require(eventDispatched, "the pending callback runs only after the write");

  // The caller's own check runs before every rename, the retried one too.
  held = holdTarget(path);
  int checks = 0;
  const auto twice = AtomicWrite::write(path, "after a second attempt", &error, nullptr, [&] {
    // The reader lets go while the second attempt is staged.
    if (++checks == 2)
      CloseHandle(held);
    return true;
  });
  require(twice == AtomicWrite::Result::Written && checks == 2 && readBytes(path) == "after a second attempt",
          "a retried write is checked again before it replaces the file");

  // A file held for the whole budget.
  held = holdTarget(path);
  QElapsedTimer elapsed;
  elapsed.start();
  AtomicWrite::Budget shortBudget(60ms);
  const auto refused = AtomicWrite::write(path, "must stay uncommitted", &error, &shortBudget);
  const qint64 duration = elapsed.elapsed();
  CloseHandle(held);
  require(refused == AtomicWrite::Result::Refused && !error.isEmpty() && duration >= 60,
          "a file held for the whole budget is reported refused once it is over");
  require(readBytes(path) == "after a second attempt" && filesIn(directory) == QStringList{name},
          "that leaves the file, and nothing else");

  // The writes of one operation share a budget, counted from the first refusal.
  const auto other = directory.filePath(QStringLiteral("other.md"));
  require(AtomicWrite::write(other, "other original", &error) == AtomicWrite::Result::Written, error);
  HANDLE firstHandle = holdTarget(path), secondHandle = holdTarget(other);
  AtomicWrite::Budget shared(200ms);
  elapsed.start();
  const auto first = AtomicWrite::write(path, "x", &error, &shared);
  const qint64 firstDuration = elapsed.elapsed();
  const auto second = AtomicWrite::write(other, "x", &error, &shared);
  const qint64 total = elapsed.elapsed();
  CloseHandle(firstHandle);
  CloseHandle(secondHandle);
  require(first == AtomicWrite::Result::Refused && second == AtomicWrite::Result::Refused && firstDuration >= 200,
          "the first refused write uses the shared budget");
  require(total < 380, "and the next one waits no further: " + QString::number(total));

  qInstallMessageHandler(previous);
  logged = nullptr;
}
#endif

} // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  runTest("writesAndReplaces", writesAndReplaces);
  runTest("failuresAreReportedAtOnce", failuresAreReportedAtOnce);
  runTest("turnedDownWrites", turnedDownWrites);
#ifdef Q_OS_WIN
  runTest("heldReplacements", heldReplacements);
#endif
  return 0;
}
