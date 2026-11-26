#include "io/AtomicWrite.h"

#include <QDebug>
#include <QDir>
#include <QFileDevice>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QThread>

#if defined(Q_OS_WIN)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

Q_LOGGING_CATEGORY(atomicWriteLog, "muffin.io", QtWarningMsg)

namespace {

// Long enough for whatever holds the file to be done with it, short enough
// that a budget is many attempts.
constexpr qint64 kPauseMs = 10;

#if defined(Q_OS_WIN)
// Whether a temporary file that QSaveFile could not open beside `path` is the
// system refusing the folder for a moment, as it does while another process
// looks at it, and not a file it will never take. QSaveFile says only that it
// could not, so a file of the same name shape is asked for. Taken, the folder
// is open and the refusal has passed or will. Refused as a busy folder is
// refused, with access denied or a sharing violation, it is one to wait out.
// Refused any other way, a bad name, a folder that is not there, a full disk,
// waiting will not help.
bool isPassingRefusal(const QString& path) {
  // An alternate data stream is not refused; QSaveFile does not write one.
  // Asking for a probe beside it would create the file the stream is on.
  if (path.indexOf(u':', 2) > 1)
    return false;
  const auto probe = QDir::toNativeSeparators(path) + QStringLiteral(".muffin-probe");
  const HANDLE file = CreateFile(reinterpret_cast<const wchar_t*>(probe.utf16()), DELETE, 0, nullptr, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    CloseHandle(file); // with the handle goes the file
    return true;
  }
  const DWORD error = GetLastError();
  return error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION;
}
#endif

// Whether `file` failed because the system refused it: its temporary file
// could not be opened, or could not be renamed over the target (`renaming`).
// Only Windows refuses a file for a moment.
bool isRefusal(const QSaveFile& file, bool renaming, const QString& path) {
#if defined(Q_OS_WIN)
  if (renaming)
    return file.error() == QFileDevice::RenameError;
  return file.error() == QFileDevice::OpenError && isPassingRefusal(path);
#else
  Q_UNUSED(file)
  Q_UNUSED(renaming)
  Q_UNUSED(path)
  return false;
#endif
}

} // namespace

bool muffin::AtomicWrite::Budget::waitOut() {
  if (!clock_.isValid())
    clock_.start();
  const qint64 remaining = total_.count() - clock_.elapsed();
  if (remaining <= 0)
    return false;
  QThread::msleep(static_cast<unsigned long>(qMin<qint64>(kPauseMs, remaining)));
  return true;
}

muffin::AtomicWrite::Result muffin::AtomicWrite::write(const QString& path,
                                                       const std::function<bool(QSaveFile&)>& writer, QString* error,
                                                       Budget* budget, const std::function<bool()>& stillCurrent) {
  Budget own;
  QElapsedTimer waited;
  for (;;) {
    Result result = Result::Written;
    bool refused = false;
    if (error)
      error->clear(); // what an earlier attempt left no longer says why this one ended
    {
      QSaveFile file(path);
      if (!file.open(QIODevice::WriteOnly)) {
        if (error)
          *error = file.errorString();
        refused = isRefusal(file, false, path);
        if (!refused)
          result = Result::Failed;
      } else if (writer(file) && (!stillCurrent || stillCurrent())) {
        if (!file.commit()) {
          if (error)
            *error = file.errorString();
          refused = isRefusal(file, true, path);
          if (!refused)
            result = Result::Failed;
        }
      } else {
        result = Result::Declined; // the temporary file goes with `file`
      }
    }
    if (!refused) {
      if (waited.isValid() && result == Result::Written)
        qCWarning(atomicWriteLog) << "waited out the system's refusal of" << path << "for" << waited.elapsed() << "ms";
      return result;
    }
    if (!waited.isValid())
      waited.start();
    if (!(budget ? *budget : own).waitOut())
      return Result::Refused;
  }
}

muffin::AtomicWrite::Result muffin::AtomicWrite::write(const QString& path, const QByteArray& bytes, QString* error,
                                                       Budget* budget, const std::function<bool()>& stillCurrent) {
  bool unwritten = false;
  const Result result = write(path, [&bytes, error, &unwritten](QSaveFile& file) {
    if (file.write(bytes) == bytes.size())
      return true;
    unwritten = true;
    if (error)
      *error = file.errorString();
    return false;
  }, error, budget, stillCurrent);
  return unwritten ? Result::Failed : result;
}
