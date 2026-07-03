#include "io/FilePathOps.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>
#include <QDesktopServices>

#if defined(Q_OS_WIN)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <shellapi.h>
#endif

namespace {

// Generic error used when the platform API gives no detail (e.g. moveToTrash).
const QString kUnsupported = QStringLiteral("operation failed");

}  // namespace

bool muffin::FilePathOps::createFile(const QString& path, QString* error) {
  if (QFileInfo::exists(path)) {
    if (error) {
      *error = QStringLiteral("file already exists");
    }
    return false;
  }
  const QDir parentDir = QFileInfo(path).dir();
  if (!parentDir.exists() && !parentDir.mkpath(QStringLiteral("."))) {
    if (error) {
      *error = QStringLiteral("could not create parent directory");
    }
    return false;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    if (error) {
      *error = file.errorString();
    }
    return false;
  }
  file.close();
  return true;
}

bool muffin::FilePathOps::createFolder(const QString& path, QString* error) {
  if (QFileInfo::exists(path)) {
    if (error) {
      *error = QStringLiteral("path already exists");
    }
    return false;
  }
  if (!QDir().mkpath(path)) {
    if (error) {
      *error = QStringLiteral("could not create folder");
    }
    return false;
  }
  return true;
}

bool muffin::FilePathOps::renamePath(const QString& oldPath, const QString& newPath, QString* error) {
  if (!QFile::rename(oldPath, newPath)) {
    if (error) {
      *error = QStringLiteral("could not rename (target may exist or the move crosses volumes)");
    }
    return false;
  }
  return true;
}

bool muffin::FilePathOps::copyFile(const QString& srcPath, const QString& destPath, QString* error) {
  const QDir destDir = QFileInfo(destPath).dir();
  if (!destDir.exists() && !destDir.mkpath(QStringLiteral("."))) {
    if (error) {
      *error = QStringLiteral("could not create destination directory");
    }
    return false;
  }
  if (!QFile::copy(srcPath, destPath)) {
    if (error) {
      *error = QStringLiteral("could not copy (destination may already exist)");
    }
    return false;
  }
  return true;
}

bool muffin::FilePathOps::moveToTrash(const QString& path, QString* error) {
  if (!QFile::moveToTrash(path)) {
    if (error) {
      *error = kUnsupported;
    }
    return false;
  }
  return true;
}

bool muffin::FilePathOps::removePermanently(const QString& path, QString* error) {
  if (QFileInfo(path).isDir()) {
    if (!QDir(path).removeRecursively()) {
      if (error) {
        *error = kUnsupported;
      }
      return false;
    }
    return true;
  }
  if (!QFile::remove(path)) {
    if (error) {
      *error = kUnsupported;
    }
    return false;
  }
  return true;
}

bool muffin::FilePathOps::revealPathInManager(const QString& path, QString* error) {
  if (path.isEmpty() || !QFileInfo::exists(path)) {
    if (error) {
      *error = QStringLiteral("path does not exist");
    }
    return false;
  }
#if defined(Q_OS_WIN)
  // explorer /select,<path> opens the parent folder with the item selected.
  // ShellExecuteW avoids QProcess's argument-quoting, which mangles the /select,
  // form on paths containing spaces.
  const std::wstring params = (L"/select,\"" + QDir::toNativeSeparators(path).toStdWString() + L"\"");
  const HINSTANCE res = ShellExecuteW(nullptr, L"open", L"explorer.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
  const bool ok = reinterpret_cast<INT_PTR>(res) > 32;
  if (!ok && error) {
    *error = kUnsupported;
  }
  return ok;
#elif defined(Q_OS_MAC)
  // `open -R <path>` reveals the item in Finder with it selected.
  const bool ok = QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), path});
  if (!ok && error) {
    *error = kUnsupported;
  }
  return ok;
#else
  // Linux / other: no portable "reveal with selection" — open the parent folder
  // (or the folder itself when a directory was passed).
  const QFileInfo info(path);
  const QString target = info.isDir() ? path : info.absolutePath();
  if (!QDesktopServices::openUrl(QUrl::fromLocalFile(target))) {
    if (error) {
      *error = kUnsupported;
    }
    return false;
  }
  return true;
#endif
}

QString muffin::FilePathOps::uniqueDuplicatePath(const QString& srcPath) {
  const QFileInfo info(srcPath);
  const QDir dir = info.dir();
  const QString base = info.completeBaseName();
  const QString suffix = info.suffix();
  for (int i = 1; i < 10000; ++i) {
    const QString candidateName = suffix.isEmpty()
        ? QStringLiteral("%1 (%2)").arg(base).arg(i)
        : QStringLiteral("%1 (%2).%3").arg(base).arg(i).arg(suffix);
    const QString candidate = dir.filePath(candidateName);
    if (!QFileInfo::exists(candidate)) {
      return candidate;
    }
  }
  return srcPath;
}

QString muffin::FilePathOps::normalizeMarkdownFileName(const QString& name) {
  // Only the file-segment is inspected, so a name with a path is handled too.
  const int lastSlash = qMax(name.lastIndexOf(QLatin1Char('/')), name.lastIndexOf(QLatin1Char('\\')));
  const QString fileSegment = name.mid(lastSlash + 1);
  if (fileSegment.isEmpty() || fileSegment.contains(QLatin1Char('.'))) {
    return name;
  }
  return name + QStringLiteral(".md");
}
