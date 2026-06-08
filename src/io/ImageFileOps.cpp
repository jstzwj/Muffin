#include "io/ImageFileOps.h"

#include "document/InlineNode.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>

namespace {

void collectInlineImageRefs(const muffin::InlineNode& inlineNode, QVector<muffin::ImageFileOps::ImageRef>& refs) {
  if (inlineNode.type() == muffin::InlineType::Image) {
    refs.append({
        inlineNode.sourceStart(),
        inlineNode.sourceEnd(),
        inlineNode.href(),
    });
  }
  for (const auto& child : inlineNode.children()) {
    collectInlineImageRefs(child, refs);
  }
}

void collectImageRefsRecursive(const muffin::MarkdownNode& node, QVector<muffin::ImageFileOps::ImageRef>& refs) {
  for (const auto& inlineNode : node.inlines()) {
    collectInlineImageRefs(inlineNode, refs);
  }
  for (const auto& child : node.children()) {
    collectImageRefsRecursive(*child, refs);
  }
}

}  // namespace

QString muffin::ImageFileOps::resolveImagePath(const QString& src, const QString& documentDir) {
  if (src.startsWith(QStringLiteral("http:")) || src.startsWith(QStringLiteral("https:")) ||
      src.startsWith(QStringLiteral("data:"))) {
    return {};
  }
  if (QFileInfo::exists(src)) {
    return QFileInfo(src).absoluteFilePath();
  }
  if (!documentDir.isEmpty()) {
    const QString absolute = QDir(documentDir).absoluteFilePath(src);
    if (QFileInfo::exists(absolute)) {
      return QFileInfo(absolute).absoluteFilePath();
    }
  }
  return {};
}

bool muffin::ImageFileOps::isLocalImageSrc(const QString& src) {
  if (src.isEmpty()) {
    return false;
  }
  return !src.startsWith(QStringLiteral("http:")) &&
         !src.startsWith(QStringLiteral("https:")) &&
         !src.startsWith(QStringLiteral("data:"));
}

QStringList muffin::ImageFileOps::collectLocalImagePaths(const MarkdownDocument& document, const QString& documentDir) {
  QStringList paths;
  const auto refs = collectImageRefs(document);
  for (const auto& ref : refs) {
    if (!isLocalImageSrc(ref.href)) {
      continue;
    }
    const QString resolved = resolveImagePath(ref.href, documentDir);
    if (!resolved.isEmpty()) {
      paths.append(resolved);
    }
  }
  paths.removeDuplicates();
  return paths;
}

QVector<muffin::ImageFileOps::ImageRef> muffin::ImageFileOps::collectImageRefs(const MarkdownDocument& document) {
  QVector<ImageRef> refs;
  collectImageRefsRecursive(document.root(), refs);
  return refs;
}

bool muffin::ImageFileOps::copyImageTo(const QString& srcPath, const QDir& destDir, QString* outNewPath) {
  if (!QFileInfo::exists(srcPath) || !destDir.exists()) {
    return false;
  }
  const QString fileName = QFileInfo(srcPath).fileName();
  QString destPath = destDir.filePath(fileName);
  // Avoid overwriting: append a number if file exists
  if (QFileInfo::exists(destPath) && QFileInfo(srcPath).absoluteFilePath() != QFileInfo(destPath).absoluteFilePath()) {
    const QString baseName = QFileInfo(srcPath).completeBaseName();
    const QString suffix = QFileInfo(srcPath).suffix();
    int counter = 1;
    do {
      destPath = destDir.filePath(QStringLiteral("%1_%2.%3").arg(baseName).arg(counter).arg(suffix));
      ++counter;
    } while (QFileInfo::exists(destPath));
  }
  if (!QFile::copy(srcPath, destPath)) {
    return false;
  }
  if (outNewPath) {
    *outNewPath = destPath;
  }
  return true;
}

bool muffin::ImageFileOps::moveImageTo(const QString& srcPath, const QDir& destDir, QString* outNewPath) {
  if (!QFileInfo::exists(srcPath) || !destDir.exists()) {
    return false;
  }
  const QString fileName = QFileInfo(srcPath).fileName();
  QString destPath = destDir.filePath(fileName);
  if (QFileInfo::exists(destPath) && QFileInfo(srcPath).absoluteFilePath() != QFileInfo(destPath).absoluteFilePath()) {
    const QString baseName = QFileInfo(srcPath).completeBaseName();
    const QString suffix = QFileInfo(srcPath).suffix();
    int counter = 1;
    do {
      destPath = destDir.filePath(QStringLiteral("%1_%2.%3").arg(baseName).arg(counter).arg(suffix));
      ++counter;
    } while (QFileInfo::exists(destPath));
  }
  if (!QFile::rename(srcPath, destPath)) {
    return false;
  }
  if (outNewPath) {
    *outNewPath = destPath;
  }
  return true;
}

bool muffin::ImageFileOps::deleteImageFile(const QString& path) {
  if (!QFileInfo::exists(path)) {
    return false;
  }
  return QFile::remove(path);
}

QString muffin::ImageFileOps::savePastedImage(const QImage& image, const QDir& destDir) {
  if (image.isNull() || !destDir.exists()) {
    return {};
  }
  const QString fileName = QStringLiteral("pasted_%1.png").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")));
  const QString filePath = destDir.filePath(fileName);
  if (!image.save(filePath, "PNG")) {
    return {};
  }
  return filePath;
}
