#include "render/ImageLoader.h"
#include "render/ImageDecoder.h"
#include "render/LayoutResources.h"

#include <QCoreApplication>
#include <QNetworkReply>
#include <QUrl>
#include <QFileInfo>
#include <QDateTime>

namespace muffin {

ImageLoader& ImageLoader::instance() {
  // Intentionally leaked, never destroyed. A destroyed-at-exit() static would
  // run during MuffinUi.dll's DLL_PROCESS_DETACH in SHARED builds: tearing down
  // QNetworkAccessManager there waits on Qt's internal threads and touches
  // already-freed socket notifiers under the loader lock — a deterministic
  // exit-time access violation on Windows ARM64 (confirmed by WER dump:
  // ~QNetworkAccessManager -> QThread::wait -> doUnregisterSocketNotifier),
  // and a glibc heap abort on Linux when it outlives QApplication. The
  // aboutToQuit hook below still releases the network resources early in the
  // real app; the shell object itself is left for the OS to reclaim.
  static ImageLoader* loader = new ImageLoader();
  return *loader;
}

ImageLoader::ImageLoader(QObject* parent) : QObject(parent), network_(new QNetworkAccessManager(this)) {
  connect(&files_, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
    const auto urls = localFiles_.value(path);
    for (const auto& url : urls) {
      fileIdentities_.remove(url);
      image(url);
    }
  });
  connect(&files_, &QFileSystemWatcher::directoryChanged, this, [this](const QString& directory) {
    const auto paths = localFiles_.keys();
    for (const auto& path : paths)
      if (QFileInfo(path).absolutePath() == directory) {
        const auto urls = localFiles_.value(path);
        for (const auto& url : urls) image(url);
      }
  });
  // QNetworkAccessManager and its pending replies must be released while a
  // QCoreApplication still exists (see instance() above). Note that aboutToQuit
  // only fires after an event loop has run — applications (and tests) that
  // never call exec() rely on the instance being leaked instead.
  if (auto* app = QCoreApplication::instance()) {
    connect(app, &QCoreApplication::aboutToQuit, this, [this] {
      if (network_) {
        delete network_;
        network_ = nullptr;
      }
      pending_.clear();
    });
  }
}

QImage ImageLoader::cached(const QString& url) const {
  LayoutResources::instance().read(LayoutResources::imageKey(url));
  return cache_.value(url);
}

void ImageLoader::store(const QString& url, QImage image) {
  cache_.insert(url, image);
  const auto metrics = QByteArray::number(image.width()) + ':' + QByteArray::number(image.height());
  LayoutResources::instance().publish(LayoutResources::imageKey(url), metrics);
  emit imageReady(url);
}

QImage ImageLoader::image(const QString& url) {
  const QUrl resource(url);
  if (resource.scheme() == "http" || resource.scheme() == "https") {
    if (!cache_.contains(url)) request(url);
  } else if (resource.scheme() == "data") {
    if (!cache_.contains(url)) store(url, image_decoder::decodeDataUri(url));
  } else {
    const auto path = resource.isLocalFile() ? resource.toLocalFile() : resource.scheme() == "qrc" ? ':' + resource.path() : url;
    const QFileInfo info(path);
    const auto identity = QByteArray::number(info.exists()) + ':' + QByteArray::number(info.size()) + ':' +
                          QByteArray::number(info.lastModified().toMSecsSinceEpoch()) + ':' +
                          QByteArray::number(info.metadataChangeTime().toMSecsSinceEpoch());
    if (!fileIdentities_.contains(url) || fileIdentities_.value(url) != identity) {
      fileIdentities_.insert(url, identity);
      auto decoded = image_decoder::decodeFileFallback(path);
      if (decoded.isNull()) decoded.load(path);
      store(url, std::move(decoded));
    }
    if (!path.startsWith(":/")) {
      localFiles_[info.absoluteFilePath()].insert(url);
      if (info.exists() && !files_.files().contains(info.absoluteFilePath())) files_.addPath(info.absoluteFilePath());
      if (!files_.directories().contains(info.absolutePath())) files_.addPath(info.absolutePath());
    }
  }
  return cached(url);
}

bool ImageLoader::isPending(const QString& url) const {
  return pending_.contains(url);
}

void ImageLoader::request(const QString& url) {
  if (!network_ || cache_.contains(url) || pending_.contains(url)) {
    return;
  }
  if (!url.startsWith(QStringLiteral("http:")) && !url.startsWith(QStringLiteral("https:"))) {
    return;
  }
  pending_.insert(url);

  const QUrl requestUrl(url);
  QNetworkRequest request(requestUrl);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  QNetworkReply* reply = network_->get(request);

  connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
    reply->deleteLater();
    pending_.remove(url);

    if (reply->error() != QNetworkReply::NoError) {
      store(url, {});
      return;
    }
    const QByteArray data = reply->readAll();
    if (data.isEmpty()) {
      store(url, {});
      return;
    }
    QImage image = image_decoder::decodeFallback(data);  // png/jpeg/webp/avif/svg via bundled libs
    if (image.isNull()) {
      image.loadFromData(data);  // last resort for formats we don't ship
      if (image.isNull()) {
        store(url, {});
        return;
      }
    }
    store(url, std::move(image));
  });
}

}  // namespace muffin
