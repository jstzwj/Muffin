#include "document/DocumentSession.h"
#include "io/ImageFileOps.h"

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

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
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
