#include "document/DocumentSession.h"
#include "io/ImageFileOps.h"
#include "editor/ResourceUrl.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdlib>

using namespace muffin;

namespace {

void require(bool condition, const QString& message) {
  if (!condition) {
    qCritical().noquote() << message;
    std::exit(1);
  }
}

void writeFile(const QString& path, const QByteArray& data) {
  QFile file(path);
  require(file.open(QIODevice::WriteOnly), QStringLiteral("Could not create image fixture"));
  require(file.write(data) == data.size(), QStringLiteral("Could not write image fixture"));
}

void testDocumentDirectoryWinsOverWorkingDirectory() {
  QTemporaryDir root;
  QDir dir(root.path());
  require(dir.mkpath(QStringLiteral("cwd")) && dir.mkpath(QStringLiteral("doc")) &&
              dir.mkpath(QStringLiteral("dest")), QStringLiteral("Could not create fixture folders"));
  const QString wrong = dir.filePath(QStringLiteral("cwd/photo.png"));
  const QString correct = dir.filePath(QStringLiteral("doc/photo.png"));
  writeFile(wrong, QByteArrayLiteral("wrong"));
  writeFile(correct, QByteArrayLiteral("correct"));
  writeFile(dir.filePath(QStringLiteral("cwd/only-in-cwd.png")), QByteArrayLiteral("unrelated"));
  const QString previousCwd = QDir::currentPath();
  require(QDir::setCurrent(dir.filePath(QStringLiteral("cwd"))), QStringLiteral("Could not change working directory"));
  const QString docDir = dir.filePath(QStringLiteral("doc"));
  require(ImageFileOps::resolveImagePath(QStringLiteral("photo.png"), docDir) == correct,
          QStringLiteral("Relative image must resolve against its document, not the working directory"));
  require(ImageFileOps::resolveImagePath(QStringLiteral("only-in-cwd.png"), docDir).isEmpty(),
          QStringLiteral("A missing document image must not fall back to an unrelated working-directory file"));
  const QString md = QStringLiteral("![photo](photo.png)");
  DocumentSession session;
  session.setMarkdownText(md, false);
  const auto moved = ImageFileOps::moveAllImages(session.document(), md, docDir,
                                                QDir(dir.filePath(QStringLiteral("dest"))));
  require(QDir::setCurrent(previousCwd), QStringLiteral("Could not restore working directory"));
  require(moved.success && moved.movedCount == 1, QStringLiteral("Expected one image move"));
  require(QFileInfo::exists(wrong) && !QFileInfo::exists(correct),
          QStringLiteral("Move the document image and leave the unrelated file alone"));
  QFile destination(dir.filePath(QStringLiteral("dest/photo.png")));
  require(destination.open(QIODevice::ReadOnly) && destination.readAll() == QByteArrayLiteral("correct"),
          QStringLiteral("Destination must contain the document image"));
}

void testUrlEncodedPathsMatchRendering() {
  QTemporaryDir root;
  QDir dir(root.path());
  const QString docPath = dir.filePath(QStringLiteral("doc.md"));
  const QString local = dir.filePath(QStringLiteral("my image.png"));
  writeFile(local, QByteArrayLiteral("image"));
  const QStringList hrefs = {
      QStringLiteral("my%20image.png"), QStringLiteral("my%20image.png?cache=1#preview"),
      QUrl::fromLocalFile(local).toString(QUrl::FullyEncoded),
      QDir::fromNativeSeparators(local).replace(QStringLiteral("my image"), QStringLiteral("my%20image"))};
  for (const QString& href : hrefs) {
    require(ImageFileOps::isLocalImageSrc(href), QStringLiteral("Local URL must be classified as local"));
    const QString rendered = resolvedUrlForDocumentResource(href, docPath).toLocalFile();
    require(ImageFileOps::resolveImagePath(href, root.path()) == rendered && rendered == local,
            QStringLiteral("Image operations and rendering must resolve the same file: %1").arg(href));
  }
  require(!ImageFileOps::isLocalImageSrc(QStringLiteral("HTTPS://example.com/image.png")) &&
              !ImageFileOps::isLocalImageSrc(QStringLiteral("data:image/png;base64,a")) &&
              !ImageFileOps::isLocalImageSrc(QStringLiteral("ftp://example.com/image.png")),
          QStringLiteral("Non-file URLs must not be treated as local paths"));
  DocumentSession session;
  session.setMarkdownText(QStringLiteral("![space](my%20image.png)"), false);
  require(ImageFileOps::collectLocalImagePaths(session.document(), root.path()) == QStringList{local},
          QStringLiteral("Encoded images must be collected for upload and copy"));
}

void testBatchRewritePreservesImageSyntax() {
  QTemporaryDir root;
  QDir dir(root.path());
  const QString spaced = dir.filePath(QStringLiteral("my image.png"));
  const QString nested = dir.filePath(QStringLiteral("photo(1).png"));
  writeFile(spaced, QByteArrayLiteral("space"));
  writeFile(nested, QByteArrayLiteral("nested"));
  const QString md = QStringLiteral(
      "# Images\n\n"
      "![first](<my image.png> \"keep title\")\n\n"
      "> ![second](photo(1).png 'also keep')\n\n"
      "![duplicate](my%20image.png)\n\n"
      "![remote](https://example.com/original.png)\n");
  DocumentSession session;
  session.setMarkdownText(md, false);
  const QString url = QStringLiteral("https://cdn.example/new(1).png?a=1&b=2");
  int count = 0;
  const QString rewritten = ImageFileOps::rewriteImageSources(session.document(), md, root.path(),
      {{spaced, url}, {nested, QStringLiteral("https://cdn.example/nested.png")}}, &count);
  require(count == 3 && rewritten.contains(QStringLiteral("\"keep title\"")) &&
              rewritten.contains(QStringLiteral("'also keep'")),
          QStringLiteral("Rewrite all local uses while preserving titles"));
  DocumentSession after;
  after.setMarkdownText(rewritten, false);
  const auto refs = ImageFileOps::collectImageRefs(after.document());
  require(refs.size() == 4 && refs[0].href == url && refs[1].href == QStringLiteral("https://cdn.example/nested.png") &&
              refs[2].href == url && refs[3].href == QStringLiteral("https://example.com/original.png"),
          QStringLiteral("Every rewritten image must remain parseable with the exact uploaded URL"));
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  testDocumentDirectoryWinsOverWorkingDirectory();
  testUrlEncodedPathsMatchRendering();
  testBatchRewritePreservesImageSyntax();
  QTemporaryDir root;
  require(root.isValid(), QStringLiteral("Temp dir invalid"));
  QDir dir(root.path());
  require(dir.mkpath(QStringLiteral("assets")), QStringLiteral("Could not create assets"));
  require(dir.mkpath(QStringLiteral("moved")), QStringLiteral("Could not create destination"));
  writeFile(dir.filePath(QStringLiteral("assets/photo.png")), QByteArrayLiteral("one"));
  writeFile(dir.filePath(QStringLiteral("assets/photo (1).png")), QByteArrayLiteral("two"));

  const QString markdown = QStringLiteral(
      "![first](assets/photo.png \"title\")\n"
      "![same](assets/photo.png)\n"
      "![spaced](<assets/photo (1).png>)\n");
  DocumentSession before;
  before.setMarkdownText(markdown, false);

  const auto result = ImageFileOps::moveAllImages(
      before.document(), markdown, root.path(), QDir(dir.filePath(QStringLiteral("moved"))));
  require(result.success, QStringLiteral("Move-all failed: %1").arg(result.error));
  require(result.movedCount == 2, QStringLiteral("Duplicate references should move two unique files"));
  require(!QFileInfo::exists(dir.filePath(QStringLiteral("assets/photo.png"))),
          QStringLiteral("Original image was not moved"));
  require(QFileInfo::exists(dir.filePath(QStringLiteral("moved/photo.png"))),
          QStringLiteral("Moved image missing"));
  require(result.markdown.contains(QStringLiteral("![first](moved/photo.png \"title\")")),
          QStringLiteral("Title-bearing reference was not preserved"));
  require(result.markdown.count(QStringLiteral("moved/photo.png")) == 2,
          QStringLiteral("Every duplicate reference should be rewritten"));
  require(result.markdown.contains(QStringLiteral("![spaced](<moved/photo (1).png>)")),
          QStringLiteral("Spaced destination was not rewritten safely"));

  DocumentSession after;
  after.setMarkdownText(result.markdown, false);
  const QStringList resolved = ImageFileOps::collectLocalImagePaths(after.document(), root.path());
  require(resolved.size() == 2, QStringLiteral("Rewritten document should resolve both moved files"));
  return 0;
}
