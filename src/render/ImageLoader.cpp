#include "render/ImageLoader.h"
#include "render/ImageDecoder.h"

#include <QCoreApplication>
#include <QNetworkReply>
#include <QUrl>

namespace muffin {

ImageLoader& ImageLoader::instance() {
  static ImageLoader loader;
  return loader;
}

ImageLoader::ImageLoader(QObject* parent) : QObject(parent), network_(new QNetworkAccessManager(this)) {
  // ImageLoader is a function-local static, so it is destroyed during exit(),
  // AFTER the QApplication (a stack local in main) is gone. QNetworkAccessManager
  // and its pending replies must be destroyed while a QCoreApplication still
  // exists — otherwise Qt's network teardown corrupts the heap (on Linux glibc
  // aborts with "malloc_consolidate(): unaligned fastbin chunk"). Tear down
  // networking on aboutToQuit, while the application object still lives, and
  // null out the pointer so the member does not destruct again at exit().
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
    QImage image;
    if (!image.loadFromData(data)) {
      image = image_decoder::decodeFallback(data);
      if (image.isNull()) {
        return;
      }
    }
    cache_.insert(url, image);
    emit imageReady(url);
  });
}

}  // namespace muffin
