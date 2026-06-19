#include "image/ImageInsertionPolicy.h"

#include "image/CustomCommandUploader.h"
#include "io/ImageFileOps.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>
#include <QUrl>

namespace muffin {

namespace {

bool isNetworkSource(const QString& src) {
  return src.startsWith(QStringLiteral("http:"), Qt::CaseInsensitive) ||
         src.startsWith(QStringLiteral("https:"), Qt::CaseInsensitive);
}

// Read the chosen insert action, then honour a per-document upload directive: when
// `image/allowYamlUpload` is on and the document's frontmatter contains
// `muffin-image-upload: true`, force Upload regardless of the dropdown.
ImageInsertAction effectiveAction(QSettings& settings, const ImageInsertRequest& req) {
  const int raw = qBound(0, settings.value(QStringLiteral("image/insertAction"), 0).toInt(),
                         static_cast<int>(ImageInsertAction::CopyToCustomFolder));
  ImageInsertAction action = static_cast<ImageInsertAction>(raw);
  if (action == ImageInsertAction::Upload) {
    return action;  // already uploading
  }
  const bool allowYaml = settings.value(QStringLiteral("image/allowYamlUpload"), false).toBool();
  if (!allowYaml || req.documentText.isEmpty()) {
    return action;
  }
  // Only scan the leading frontmatter block, never the whole document.
  const QString head = req.documentText.left(qMin(req.documentText.size(), qsizetype(4096)));
  if (!head.startsWith(QStringLiteral("---"))) {
    return action;
  }
  const QStringList lines = head.split(QChar('\n'));
  for (int i = 1; i < lines.size(); ++i) {  // skip the opening "---"
    const QString line = lines[i].trimmed();
    if (line == QStringLiteral("---") || line == QStringLiteral("...")) {
      break;  // end of frontmatter
    }
    if (line.startsWith(QStringLiteral("muffin-image-upload"), Qt::CaseInsensitive) &&
        line.contains(QStringLiteral("true"), Qt::CaseInsensitive)) {
      return ImageInsertAction::Upload;
    }
  }
  return action;
}

// The destination directory for a copy action, relative to the document dir.
QString copyDestination(ImageInsertAction action, const QString& documentPath, QSettings& settings) {
  const QString docDir = QFileInfo(documentPath).absolutePath();
  switch (action) {
    case ImageInsertAction::CopyToCurrentFolder:
      return docDir;
    case ImageInsertAction::CopyToAssets:
      return docDir.isEmpty() ? QString() : QDir(docDir).filePath(QStringLiteral("assets"));
    case ImageInsertAction::CopyToFilenameAssets: {
      if (docDir.isEmpty()) {
        return QString();
      }
      // $(filename) = the document's base name (no extension).
      const QString base = QFileInfo(documentPath).completeBaseName();
      const QString folder = base.isEmpty() ? QStringLiteral("assets") : (base + QStringLiteral(".assets"));
      return QDir(docDir).filePath(folder);
    }
    case ImageInsertAction::CopyToCustomFolder:
      return settings.value(QStringLiteral("image/customFolder")).toString();
    default:
      return QString();
  }
}

// Reformat a (possibly absolute) local path into the href, honouring
// preferRelativePath / addLeadingSlash / escapeImageUrl.
QString formatLocalHref(const QString& path, const QString& docDir, QSettings& settings) {
  QString p = path;
  const bool preferRelative = settings.value(QStringLiteral("image/preferRelativePath"), false).toBool();
  if (QFileInfo(p).isAbsolute() && !docDir.isEmpty()) {
    const QString rel = QDir(docDir).relativeFilePath(p);
    // Default: use a relative path only when it stays at/under the document dir;
    // preferRelativePath forces it even when it escapes ("../…").
    if (preferRelative || !rel.startsWith(QStringLiteral(".."))) {
      p = rel;
    }
  }
  if (settings.value(QStringLiteral("image/addLeadingSlash"), false).toBool()) {
    // Only relative paths get "./"; leave absolute (has a drive/scheme) and rooted alone.
    if (!p.isEmpty() && !p.startsWith(QChar('/')) && !p.startsWith(QStringLiteral("./")) && !p.contains(QChar(':'))) {
      p = QStringLiteral("./") + p;
    }
  }
  if (settings.value(QStringLiteral("image/escapeImageUrl"), false).toBool()) {
    // Encode spaces and other unsafe chars, preserving the chars that are valid in a path/URL.
    p = QUrl::toPercentEncoding(p, QByteArrayLiteral("/:#?&=+@%,._-"));
  }
  return p;
}

// Synchronous GET of a network image to a temp file. Used when applyToNetwork
// re-hosts a remote image. Returns empty on failure/timeout.
QString downloadNetworkImage(const QString& url) {
  QNetworkAccessManager nam;
  QNetworkReply* reply = nam.get(QNetworkRequest(QUrl(url)));
  QEventLoop loop;
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  QTimer::singleShot(15000, &loop, &QEventLoop::quit);
  loop.exec(QEventLoop::ExcludeUserInputEvents);
  if (!reply->isFinished()) {
    reply->abort();
  }
  if (reply->error() != QNetworkReply::NoError) {
    reply->deleteLater();
    return {};
  }
  const QByteArray data = reply->readAll();
  reply->deleteLater();

  QImage image;
  if (!image.loadFromData(data)) {
    return {};
  }
  QString suffix = QFileInfo(QUrl(url).path()).suffix();
  if (suffix.isEmpty()) {
    suffix = QStringLiteral("png");
  }
  const QString tempPath = QDir::tempPath() + QStringLiteral("/muffin_rehost_%1.%2")
                                                   .arg(QString::number(qint64(image.cacheKey())), suffix);
  if (!image.save(tempPath, suffix.toUtf8().constData())) {
    return {};
  }
  return tempPath;
}

}  // namespace

ImageInsertResult ImageInsertionPolicy::resolveHref(const ImageInsertRequest& req, QSettings& settings, QWidget* parent) {
  ImageInsertResult result;
  result.alt = req.alt;

  const QString docDir = QFileInfo(req.documentPath).absolutePath();

  // 1. Materialize a local file path for the source.
  QString sourcePath = req.sourcePath;
  if (sourcePath.isEmpty() && !req.pastedImage.isNull()) {
    if (docDir.isEmpty()) {
      result.error = QStringLiteral("no document folder to save the pasted image into");
      return result;
    }
    sourcePath = ImageFileOps::savePastedImage(req.pastedImage, QDir(docDir));
    if (sourcePath.isEmpty()) {
      result.error = QStringLiteral("could not save the pasted image");
      return result;
    }
  }
  if (sourcePath.isEmpty()) {
    result.error = QStringLiteral("no image source");
    return result;
  }

  // 2. Effective action (frontmatter override), then the applyToLocal/Network gate.
  ImageInsertAction action = effectiveAction(settings, req);
  const bool network = isNetworkSource(sourcePath);
  if (network && !settings.value(QStringLiteral("image/applyToNetwork"), false).toBool()) {
    action = ImageInsertAction::None;
  } else if (!network && !settings.value(QStringLiteral("image/applyToLocal"), true).toBool()) {
    action = ImageInsertAction::None;
  }

  // 3. Re-host network sources (download to temp) when an action other than None applies.
  if (network && action != ImageInsertAction::None) {
    const QString downloaded = downloadNetworkImage(sourcePath);
    if (downloaded.isEmpty()) {
      action = ImageInsertAction::None;  // couldn't fetch; keep the original URL
      result.error = QStringLiteral("could not download the network image; left the URL as-is");
    } else {
      sourcePath = downloaded;
    }
  }

  // 4. Dispatch on the action.
  if (action == ImageInsertAction::None) {
    result.href = network ? sourcePath : formatLocalHref(sourcePath, docDir, settings);
    result.ok = true;
    return result;
  }

  if (action == ImageInsertAction::Upload) {
    const CustomCommandResult uploaded = CustomCommandUploader::upload(parent, {sourcePath});
    if (uploaded.canceled) {
      result.error = QStringLiteral("upload canceled");
      return result;  // ok stays false → caller inserts nothing
    }
    if (uploaded.ran && !uploaded.urls.isEmpty()) {
      result.href = uploaded.urls.first();
      result.uploaded = true;
      result.ok = true;
      return result;
    }
    // Upload failed — fall back to a local href so the user still sees the image,
    // and surface the reason as a non-fatal warning.
    result.error = uploaded.error.isEmpty() ? QStringLiteral("upload failed; inserted the local path instead")
                                            : uploaded.error;
    result.href = formatLocalHref(sourcePath, docDir, settings);
    result.ok = true;
    return result;
  }

  // Copy actions: CurrentFolder / Assets / FilenameAssets / CustomFolder.
  const QString destDirPath = copyDestination(action, req.documentPath, settings);
  if (destDirPath.isEmpty()) {
    result.error = QStringLiteral("no destination folder configured for this copy action");
    result.href = formatLocalHref(sourcePath, docDir, settings);
    result.ok = true;
    return result;
  }
  QDir destDir(destDirPath);
  if (!destDir.exists() && !QDir::root().mkpath(destDirPath)) {
    result.error = QStringLiteral("could not create the destination folder");
    result.href = formatLocalHref(sourcePath, docDir, settings);
    result.ok = true;
    return result;
  }
  QString newPath;
  if (ImageFileOps::copyImageTo(sourcePath, destDir, &newPath) && !newPath.isEmpty()) {
    result.href = formatLocalHref(newPath, docDir, settings);
  } else {
    result.error = QStringLiteral("could not copy the image; left the original path");
    result.href = formatLocalHref(sourcePath, docDir, settings);
  }
  result.ok = true;
  return result;
}

}  // namespace muffin
