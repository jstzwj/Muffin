#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

class QDir;

namespace muffin {

class MarkdownDocument;

class ImageFileOps final {
public:
  /// Resolve a possibly-relative image src to an absolute path using the document directory.
  static QString resolveImagePath(const QString& src, const QString& documentDir);

  /// Check whether a src URL refers to a local file (not http/https/data).
  static bool isLocalImageSrc(const QString& src);

  /// Collect all local image file paths referenced in the document.
  static QStringList collectLocalImagePaths(const MarkdownDocument& document, const QString& documentDir);

  /// Collect all image InlineNodes' source ranges and hrefs from the document.
  struct ImageRef {
    qsizetype sourceStart = 0;
    qsizetype sourceEnd = 0;
    QString href;
  };
  static QVector<ImageRef> collectImageRefs(const MarkdownDocument& document);

  /// Copy an image file to a destination directory, returning the new path.
  static bool copyImageTo(const QString& srcPath, const QDir& destDir, QString* outNewPath);

  /// Move an image file to a destination directory, returning the new path.
  static bool moveImageTo(const QString& srcPath, const QDir& destDir, QString* outNewPath);

  /// Delete an image file.
  static bool deleteImageFile(const QString& path);

  /// Save a pasted QImage to a file in the given directory.
  static QString savePastedImage(const QImage& image, const QDir& destDir);

private:
  ImageFileOps() = delete;
};

}  // namespace muffin
