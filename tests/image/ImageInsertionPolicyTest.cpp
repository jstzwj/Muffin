#include "image/ImageInsertionPolicy.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>

#include <cstdlib>

// Exercises ImageInsertionPolicy::resolveHref through the copy + path-formatting
// paths using real temp files (no GUI parent needed — only the Upload path drives
// a modal progress dialog, and that is left to manual verification). Follows the
// project test convention (no QTest).

namespace {

void require(bool condition, const QString& message) {
  if (!condition) {
    qCritical().noquote() << message;
    std::exit(1);
  }
}

// Make a 1x1 PNG inside `dir` named `name` and return its absolute path.
QString makeImage(const QDir& dir, const QString& name) {
  QImage img(1, 1, QImage::Format_RGB32);
  img.fill(Qt::black);
  const QString path = dir.filePath(name);
  require(img.save(path, "PNG"), QStringLiteral("could not write test image %1").arg(path));
  return path;
}

muffin::ImageInsertResult resolve(const muffin::ImageInsertRequest& req) {
  QSettings settings;
  return muffin::ImageInsertionPolicy::resolveHref(req, settings, nullptr);
}

void testCopyToAssets() {
  QTemporaryDir dir;
  require(dir.isValid(), QStringLiteral("temp dir"));
  const QDir docDir(dir.path());
  const QString img = makeImage(docDir, "pic.png");
  const QString docPath = docDir.filePath("note.md");

  QSettings().setValue(QStringLiteral("image/insertAction"), static_cast<int>(muffin::ImageInsertAction::CopyToAssets));

  muffin::ImageInsertRequest req;
  req.sourcePath = img;
  req.documentPath = docPath;
  const auto res = resolve(req);
  require(res.ok, QStringLiteral("CopyToAssets should succeed"));
  require(res.href == QStringLiteral("assets/pic.png"),
          QStringLiteral("CopyToAssets href should be 'assets/pic.png', got '%1'").arg(res.href));
  require(QFile::exists(docDir.filePath("assets/pic.png")),
          QStringLiteral("the image should have been copied into ./assets"));
}

void testCopyToFilenameAssets() {
  QTemporaryDir dir;
  const QDir docDir(dir.path());
  const QString img = makeImage(docDir, "shot.png");
  const QString docPath = docDir.filePath("My Note.md");  // completeBaseName == "My Note"

  QSettings().setValue(QStringLiteral("image/insertAction"), static_cast<int>(muffin::ImageInsertAction::CopyToFilenameAssets));

  muffin::ImageInsertRequest req;
  req.sourcePath = img;
  req.documentPath = docPath;
  const auto res = resolve(req);
  require(res.ok, QStringLiteral("CopyToFilenameAssets should succeed"));
  // $(filename) expands to the document's complete base name.
  require(res.href == QStringLiteral("My Note.assets/shot.png"),
          QStringLiteral("CopyToFilenameAssets href should expand $(filename), got '%1'").arg(res.href));
  require(QFile::exists(docDir.filePath("My Note.assets/shot.png")),
          QStringLiteral("the image should have been copied into ./<filename>.assets"));
}

void testNonePreferRelative() {
  QTemporaryDir dir;
  const QDir docDir(dir.path());
  const QString img = makeImage(docDir, "inline.png");
  const QString docPath = docDir.filePath("doc.md");

  QSettings().setValue(QStringLiteral("image/insertAction"), static_cast<int>(muffin::ImageInsertAction::None));
  QSettings().setValue(QStringLiteral("image/preferRelativePath"), true);

  muffin::ImageInsertRequest req;
  req.sourcePath = img;
  req.documentPath = docPath;
  const auto res = resolve(req);
  require(res.ok && res.href == QStringLiteral("inline.png"),
          QStringLiteral("None+preferRelativePath should produce a bare relative href, got '%1'").arg(res.href));
}

void testAddLeadingSlash() {
  QTemporaryDir dir;
  const QDir docDir(dir.path());
  const QString img = makeImage(docDir, "lead.png");
  const QString docPath = docDir.filePath("doc.md");

  QSettings().setValue(QStringLiteral("image/insertAction"), static_cast<int>(muffin::ImageInsertAction::CopyToAssets));
  QSettings().setValue(QStringLiteral("image/addLeadingSlash"), true);

  muffin::ImageInsertRequest req;
  req.sourcePath = img;
  req.documentPath = docPath;
  const auto res = resolve(req);
  require(res.ok && res.href == QStringLiteral("./assets/lead.png"),
          QStringLiteral("addLeadingSlash should prefix the relative href with ./, got '%1'").arg(res.href));
}

void testEscapeImageUrl() {
  QTemporaryDir dir;
  const QDir docDir(dir.path());
  // Filename with a space exercises URL escaping.
  const QString img = makeImage(docDir, "with space.png");
  const QString docPath = docDir.filePath("doc.md");

  QSettings().setValue(QStringLiteral("image/insertAction"), static_cast<int>(muffin::ImageInsertAction::CopyToAssets));
  QSettings().setValue(QStringLiteral("image/escapeImageUrl"), true);

  muffin::ImageInsertRequest req;
  req.sourcePath = img;
  req.documentPath = docPath;
  const auto res = resolve(req);
  require(res.ok && res.href.contains(QStringLiteral("%20")),
          QStringLiteral("escapeImageUrl should encode the space, got '%1'").arg(res.href));
}

void testYamlDirectiveWithoutSettingDoesNotForceUpload() {
  QTemporaryDir dir;
  const QDir docDir(dir.path());
  const QString img = makeImage(docDir, "y.png");
  const QString docPath = docDir.filePath("doc.md");

  // allowYamlUpload OFF even though the frontmatter asks for upload → the
  // dropdown action (CopyToAssets) must win.
  QSettings().setValue(QStringLiteral("image/insertAction"), static_cast<int>(muffin::ImageInsertAction::CopyToAssets));
  QSettings().setValue(QStringLiteral("image/allowYamlUpload"), false);

  muffin::ImageInsertRequest req;
  req.sourcePath = img;
  req.documentPath = docPath;
  req.documentText = QStringLiteral("---\nmuffin-image-upload: true\n---\nbody");
  const auto res = resolve(req);
  require(res.ok && res.href == QStringLiteral("assets/y.png"),
          QStringLiteral("Without allowYamlUpload the directive must be ignored, got '%1'").arg(res.href));
}

void testPastedImageIntoUntitledDocument() {
  // Regression: an untitled document (empty documentPath → no folder) used to make
  // resolveHref return ok=false, so ClipboardController::paste() silently dropped the
  // image. It must now fall back to a writable cache dir and still produce an href.
  QSettings().setValue(QStringLiteral("image/insertAction"), static_cast<int>(muffin::ImageInsertAction::CopyToCurrentFolder));

  QImage pasted(1, 1, QImage::Format_RGB32);
  pasted.fill(Qt::blue);

  muffin::ImageInsertRequest req;
  req.pastedImage = pasted;
  req.documentPath = QString();  // untitled document
  const auto res = resolve(req);
  require(res.ok, QStringLiteral("pasting an image into an untitled document should succeed (error: %1)").arg(res.error));
  require(!res.href.isEmpty(), QStringLiteral("the pasted-image href must not be empty"));
  // Untitled → parked under the app's "muffin-user-images" folder, Typora-style name.
  require(res.href.contains(QStringLiteral("muffin-user-images")),
          QStringLiteral("untitled-doc image should live under muffin-user-images, href='%1'").arg(res.href));
  require(QFileInfo(res.href).fileName().startsWith(QStringLiteral("image-")),
          QStringLiteral("pasted image should use the image-<timestamp> name, got '%1'").arg(QFileInfo(res.href).fileName()));
  require(QFile::exists(res.href),
          QStringLiteral("the pasted image should have been saved and referenced by an existing file, href='%1'").arg(res.href));
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("Muffin"));
  QCoreApplication::setApplicationName(QStringLiteral("MuffinTests"));

  // Start each case from a clean image-settings baseline.
  const QStringList imageKeys = {
      QStringLiteral("image/insertAction"),      QStringLiteral("image/customFolder"),
      QStringLiteral("image/uploadCommand"),     QStringLiteral("image/uploadService"),
      QStringLiteral("image/applyToLocal"),      QStringLiteral("image/applyToNetwork"),
      QStringLiteral("image/allowYamlUpload"),   QStringLiteral("image/preferRelativePath"),
      QStringLiteral("image/addLeadingSlash"),   QStringLiteral("image/escapeImageUrl"),
  };

  testCopyToAssets();
  for (const QString& k : imageKeys) { QSettings().remove(k); }
  testCopyToFilenameAssets();
  for (const QString& k : imageKeys) { QSettings().remove(k); }
  testNonePreferRelative();
  for (const QString& k : imageKeys) { QSettings().remove(k); }
  testAddLeadingSlash();
  for (const QString& k : imageKeys) { QSettings().remove(k); }
  testEscapeImageUrl();
  for (const QString& k : imageKeys) { QSettings().remove(k); }
  testYamlDirectiveWithoutSettingDoesNotForceUpload();
  for (const QString& k : imageKeys) { QSettings().remove(k); }
  testPastedImageIntoUntitledDocument();
  for (const QString& k : imageKeys) { QSettings().remove(k); }
  return 0;
}
