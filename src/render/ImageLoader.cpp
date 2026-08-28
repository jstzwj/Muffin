#include "render/ImageLoader.h"
#include "render/ImageDecoder.h"

#include <QCoreApplication>
#include <QNetworkReply>
#include <QUrl>

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
  return cache_.value(url);
}

bool ImageLoader::isPending(const QString& url) const {
  return pending_.contains(url);
}

void ImageLoader::request(const QString& url) {
  if (cache_.contains(url) || pending_.contains(url)) {
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
      return;
    }
    const QByteArray data = reply->readAll();
    if (data.isEmpty()) {
      return;
    }
    QImage image = image_decoder::decodeFallback(data);  // png/jpeg/webp/avif/svg via bundled libs
    if (image.isNull()) {
      image.loadFromData(data);  // last resort for formats we don't ship
      if (image.isNull()) {
        return;
      }
    }
    cache_.insert(url, image);
    emit imageReady(url);
  });
}

}  // namespace muffin
