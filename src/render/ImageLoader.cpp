#include "render/ImageLoader.h"

#include <QNetworkReply>
#include <QUrl>

namespace muffin {

ImageLoader& ImageLoader::instance() {
  static ImageLoader loader;
  return loader;
}

ImageLoader::ImageLoader(QObject* parent) : QObject(parent) {}

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
  QNetworkReply* reply = network_.get(request);

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
      return;
    }
    cache_.insert(url, image);
    emit imageReady(url);
  });
}

}  // namespace muffin
