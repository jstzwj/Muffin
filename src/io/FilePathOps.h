#pragma once

#include <QString>

class QDir;

namespace muffin {

// Filesystem primitives for arbitrary paths (create / rename / copy / delete /
// reveal). The sidebar file-tree context menu drives these; MainWindow maps the
// technical error strings to translated user messages, so this class has no tr()
// and is intentionally NOT a translatable source. Mirrors the ImageFileOps shape.
class FilePathOps final {
public:
  /// Create an empty file (and any missing parent directories). Fails if the
  /// file already exists.
  static bool createFile(const QString& path, QString* error);

  /// Create a directory, including parents. Fails if it already exists.
  static bool createFolder(const QString& path, QString* error);

  /// Rename/move a file or directory. Fails if the target exists or the move
  /// crosses volumes (the common sidebar case is a same-directory rename).
  static bool renamePath(const QString& oldPath, const QString& newPath, QString* error);

  /// Copy a file. Fails if the destination already exists (QFile::copy semantics).
  static bool copyFile(const QString& srcPath, const QString& destPath, QString* error);

  /// Move to the OS trash. Returns false (with a generic error) if trashing is
  /// unavailable; the caller decides whether to offer a permanent delete.
  static bool moveToTrash(const QString& path, QString* error);

  /// Permanent delete: files via QFile::remove, directories recursively.
  static bool removePermanently(const QString& path, QString* error);

  /// Reveal the item in the platform file manager, selecting it where supported
  /// (explorer /select on Windows, open -R on macOS, open parent elsewhere).
  static bool revealPathInManager(const QString& path, QString* error);

  /// Return a non-existent sibling path for a copy: "notes.md" -> "notes (1).md".
  static QString uniqueDuplicatePath(const QString& srcPath);

  /// Append ".md" when a filename has no extension; otherwise return it unchanged.
  static QString normalizeMarkdownFileName(const QString& name);

private:
  FilePathOps() = delete;
};

}  // namespace muffin
