#include "io/ImageFileOps.h"

#include "document/InlineNode.h"
#include "document/ImageSyntaxOps.h"
#include "document/MarkdownDocument.h"
#include "document/MarkdownNode.h"
#include "document/LinkSyntaxOps.h"
#include "editor/ResourceUrl.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QHash>
#include <QSet>

#include <algorithm>

namespace {

bool moveFileWithFallback(const QString& source, const QString& destination) {
  if (source == destination) { return true; }
  if (QFile::rename(source, destination)) { return true; }
  if (!QFile::copy(source, destination)) { return false; }
  if (QFile::remove(source)) { return true; }
  QFile::remove(destination);
  return false;
}

QString plannedDestination(const QString& source, const QDir& destination,
                           QSet<QString>& reserved) {
  const QFileInfo info(source);
  QString candidate = destination.filePath(info.fileName());
  const QString candidateAbs = QFileInfo(candidate).absoluteFilePath();
  if (candidateAbs == info.absoluteFilePath()) { return candidateAbs; }
  int counter = 1;
  while (QFileInfo::exists(candidate) || reserved.contains(QFileInfo(candidate).absoluteFilePath())) {
    const QString name = info.suffix().isEmpty()
        ? QStringLiteral("%1_%2").arg(info.completeBaseName()).arg(counter++)
        : QStringLiteral("%1_%2.%3").arg(info.completeBaseName()).arg(counter++).arg(info.suffix());
    candidate = destination.filePath(name);
  }
  candidate = QFileInfo(candidate).absoluteFilePath();
  reserved.insert(candidate);
  return candidate;
}

void collectInlineImageRefs(const muffin::InlineNode& inlineNode, qsizetype absoluteDelta, QVector<muffin::ImageFileOps::ImageRef>& refs) {
  if (inlineNode.type() == muffin::InlineType::Image) {
    // Inlines are stored in the subtree's frame (block-relative post-relativize); the delta
    // resolves to ABSOLUTE document offsets (ImageRef is spliced into the full markdown).
    refs.append({
        inlineNode.sourceStart() + absoluteDelta,
        inlineNode.sourceEnd() + absoluteDelta,
        inlineNode.href(),
    });
  }
  for (const auto& child : inlineNode.children()) {
    collectInlineImageRefs(child, absoluteDelta, refs);
  }
}

void collectImageRefsRecursive(const muffin::MarkdownNode& node, QVector<muffin::ImageFileOps::ImageRef>& refs) {
  // All descendants share the same inline storage frame; inlineAbsoluteDelta() resolves it
  // (guard included). Recomputed per call (O(depth)) — image collection is not a hot path.
  const qsizetype absoluteDelta = node.inlineAbsoluteDelta();
  for (const auto& inlineNode : node.inlines()) {
    collectInlineImageRefs(inlineNode, absoluteDelta, refs);
  }
  for (const auto& child : node.children()) {
    collectImageRefsRecursive(*child, refs);
  }
}

}  // namespace

QString muffin::ImageFileOps::resolveImagePath(const QString& src, const QString& documentDir) {
  if (src.isEmpty()) { return {}; }
  const QUrl url = resolvedUrlForDirectoryResource(src, documentDir);
  if (!url.isValid() || !url.isLocalFile()) { return {}; }
  const QFileInfo info(url.toLocalFile());
  return info.isFile() ? info.absoluteFilePath() : QString();
}

bool muffin::ImageFileOps::isLocalImageSrc(const QString& src) {
  if (src.isEmpty()) {
    return false;
  }
  const QUrl url = resolvedUrlForDirectoryResource(src, {});
  return url.isValid() && url.isLocalFile();
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

QString muffin::ImageFileOps::rewriteImageSources(
    const MarkdownDocument& document, const QString& markdown, const QString& documentDir,
    const QHash<QString, QString>& replacements, int* rewrittenCount) {
  auto refs = collectImageRefs(document);
  std::sort(refs.begin(), refs.end(), [](const ImageRef& left, const ImageRef& right) {
    return left.sourceStart < right.sourceStart;
  });
  QString rewritten = markdown;
  int count = 0;
  for (auto it = refs.crbegin(); it != refs.crend(); ++it) {
    const auto replacement = replacements.constFind(resolveImagePath(it->href, documentDir));
    if (replacement == replacements.cend() || it->sourceStart < 0 ||
        it->sourceEnd <= it->sourceStart || it->sourceEnd > markdown.size()) { continue; }
    const QString snippet = rewritten.mid(it->sourceStart, it->sourceEnd - it->sourceStart);
    if (!image_syntax::findSource(snippet).found) { continue; }
    const QString replaced = image_syntax::replaceSource(snippet, replacement.value());
    rewritten.replace(it->sourceStart, it->sourceEnd - it->sourceStart, replaced);
    ++count;
  }
  if (rewrittenCount) { *rewrittenCount = count; }
  return rewritten;
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
  if (!moveFileWithFallback(QFileInfo(srcPath).absoluteFilePath(), QFileInfo(destPath).absoluteFilePath())) {
    return false;
  }
  if (outNewPath) {
    *outNewPath = destPath;
  }
  return true;
}

muffin::ImageFileOps::MoveAllResult muffin::ImageFileOps::moveAllImages(
    const MarkdownDocument& document, const QString& markdown,
    const QString& documentDir, const QDir& destDir) {
  MoveAllResult result;
  if (documentDir.isEmpty() || !destDir.exists()) {
    result.error = QCoreApplication::translate("muffin::ImageFileOps", "source or destination directory is unavailable");
    return result;
  }

  QVector<ImageRef> refs = collectImageRefs(document);
  std::sort(refs.begin(), refs.end(), [](const ImageRef& left, const ImageRef& right) {
    return left.sourceStart < right.sourceStart;
  });
  QHash<QString, QString> destinations;
  QSet<QString> reserved;
  for (const ImageRef& ref : refs) {
    if (!isLocalImageSrc(ref.href)) { continue; }
    const QString source = resolveImagePath(ref.href, documentDir);
    if (source.isEmpty() || destinations.contains(source)) { continue; }
    destinations.insert(source, plannedDestination(source, destDir, reserved));
  }

  QString rewritten = markdown;
  for (auto it = refs.crbegin(); it != refs.crend(); ++it) {
    if (!isLocalImageSrc(it->href)) { continue; }
    const QString source = resolveImagePath(it->href, documentDir);
    const auto destination = destinations.constFind(source);
    if (destination == destinations.cend()) { continue; }
    const QString snippet = rewritten.mid(it->sourceStart, it->sourceEnd - it->sourceStart);
    const QString relative = QDir::fromNativeSeparators(
        QDir(documentDir).relativeFilePath(destination.value()));
    const QString replaced = image_syntax::replaceSource(snippet, link_syntax::localPathHref(relative));
    if (replaced == snippet && relative != it->href) {
      result.error = QCoreApplication::translate("muffin::ImageFileOps", "could not locate an image destination in the document source");
      return result;
    }
    rewritten.replace(it->sourceStart, it->sourceEnd - it->sourceStart, replaced);
  }

  QVector<QPair<QString, QString>> completed;
  for (auto it = destinations.cbegin(); it != destinations.cend(); ++it) {
    if (it.key() == it.value()) { continue; }
    if (!moveFileWithFallback(it.key(), it.value())) {
      QStringList rollbackFailures;
      for (auto rollback = completed.crbegin(); rollback != completed.crend(); ++rollback) {
        if (!moveFileWithFallback(rollback->second, rollback->first)) {
          rollbackFailures.append(rollback->first);
        }
      }
      result.error = QCoreApplication::translate("muffin::ImageFileOps", "could not move %1").arg(it.key());
      if (!rollbackFailures.isEmpty()) {
        result.error += QCoreApplication::translate("muffin::ImageFileOps", "; rollback failed for %1")
                            .arg(rollbackFailures.join(QStringLiteral(", ")));
      }
      return result;
    }
    completed.append({it.key(), it.value()});
  }

  result.success = true;
  result.movedCount = completed.size();
  result.markdown = std::move(rewritten);
  return result;
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
  // Typora-style name: image-<YYYYMMDDHHmmssmmm>.png (e.g. image-20260624040509881.png).
  const QString fileName = QStringLiteral("image-%1.png").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddHHmmsszzz")));
  const QString filePath = destDir.filePath(fileName);
  if (!image.save(filePath, "PNG")) {
    return {};
  }
  return filePath;
}
