#include "render/ImageLoader.h"
#include "render/ImageDecoder.h"
#include "render/LayoutResources.h"

#include <QCoreApplication>
#include <QNetworkReply>
#include <QUrl>
#include <QFileInfo>
#include <QDateTime>
#include <QTimer>
#include <algorithm>
#include <limits>

namespace muffin {
struct ImageLoader::Usage {
  ImageLoader& loader;
  QString url;
  ~Usage() { loader.scheduleRelease(); }
};

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
        for (const auto& url : urls)
          image(url);
      }
  });
  // QNetworkAccessManager and its pending replies must be released while a
  // QCoreApplication still exists (see instance() above). Note that aboutToQuit
  // only fires after an event loop has run — applications (and tests) that
  // never call exec() rely on the instance being leaked instead.
  if (auto* app = QCoreApplication::instance()) {
    connect(app, &QCoreApplication::aboutToQuit, this, &ImageLoader::shutdown);
  }
}

QImage ImageLoader::cached(const QString& url) const {
  LayoutResources::instance().read(LayoutResources::imageKey(url));
  if (cache_.contains(url))
    recency_[url] = ++clock_;
  return cache_.value(url);
}

void ImageLoader::store(const QString& url, QImage image) {
  // Explicit preloading after the previous reader left starts a new warm-cache
  // lifetime; an older queued release must not discard this new publication.
  if (users_.contains(url) && users_.value(url).expired())
    users_.remove(url);
  cacheBytes_ -= cache_.value(url).sizeInBytes();
  cacheBytes_ += image.sizeInBytes();
  cache_.insert(url, image);
  recency_[url] = ++clock_;
  const auto logical = image.deviceIndependentSize();
  const auto metrics =
      QByteArray::number(logical.width(), 'g', 17) + ':' + QByteArray::number(logical.height(), 'g', 17);
  LayoutResources::instance().publish(LayoutResources::imageKey(url), metrics);
  emit imageReady(url);
  trim();
}

std::shared_ptr<void> ImageLoader::retain(const QString& url) {
  auto usage = users_.value(url).lock();
  if (!usage) {
    usage = std::make_shared<Usage>(*this, url);
    users_.insert(url, usage);
  }
  return usage;
}

void ImageLoader::scheduleRelease() {
  if (releaseQueued_)
    return;
  releaseQueued_ = true;
  // Measurement and final layout can briefly have no overlapping readers.
  // Retain across that synchronous gap, just like QObject::deleteLater().
  QTimer::singleShot(0, this, [this] {
    releaseQueued_ = false;
    for (const auto& url : users_.keys()) {
      if (!users_.value(url).expired())
        continue;
      if (auto* reply = pending_.take(url)) {
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
      }
      remove(url);
    }
    trim();
  });
}

void ImageLoader::remove(const QString& url) {
  if (users_.value(url).expired())
    users_.remove(url);
  cacheBytes_ -= cache_.take(url).sizeInBytes();
  recency_.remove(url);
  fileIdentities_.remove(url);
  const auto paths = localFiles_.keys();
  for (const auto& path : paths) {
    auto it = localFiles_.find(path);
    it->remove(url);
    if (!it->isEmpty())
      continue;
    localFiles_.erase(it);
    files_.removePath(path);
    const auto directory = QFileInfo(path).absolutePath();
    const bool stillUsed = std::any_of(localFiles_.keyBegin(), localFiles_.keyEnd(), [&](const QString& other) {
      return QFileInfo(other).absolutePath() == directory;
    });
    if (!stillUsed)
      files_.removePath(directory);
  }
  LayoutResources::instance().release(LayoutResources::imageKey(url), false);
}

void ImageLoader::trim() {
  while (cacheBytes_ > byteLimit_ || cache_.size() > entryLimit_) {
    QString oldest;
    quint64 age = std::numeric_limits<quint64>::max();
    for (auto it = recency_.cbegin(); it != recency_.cend(); ++it)
      if (users_.value(it.key()).expired() && it.value() < age) {
        oldest = it.key();
        age = it.value();
      }
    if (oldest.isEmpty())
      break; // Live layouts already own these pixels; never evict their watches.
    remove(oldest);
  }
}

void ImageLoader::setCacheLimits(qint64 bytes, qsizetype entries) {
  byteLimit_ = qMax<qint64>(0, bytes);
  entryLimit_ = qMax<qsizetype>(0, entries);
  trim();
}

ImageLoader::CacheStats ImageLoader::cacheStats() const {
  return {cacheBytes_,    cache_.size(), users_.size(), files_.files().size(), files_.directories().size(),
          pending_.size()};
}

void ImageLoader::shutdown() {
  if (network_) {
    // Prevent abort/destroy signals from repopulating the cache during teardown.
    for (auto* reply : pending_)
      disconnect(reply, nullptr, this, nullptr);
    delete network_;
    network_ = nullptr;
  }
  pending_.clear();
  const auto urls = cache_.keys();
  for (const auto& url : urls)
    remove(url);
}

QImage ImageLoader::image(const QString& url) {
  const QUrl resource(url);
  if (resource.scheme() == "http" || resource.scheme() == "https") {
    if (!cache_.contains(url))
      request(url);
  } else if (resource.scheme() == "data") {
    if (!cache_.contains(url)) {
      auto decoded = image_decoder::decodeDataUri(url);
      store(url, decoded);
      if (!cache_.contains(url))
        return decoded;
    }
  } else {
    const auto path = resource.isLocalFile()       ? resource.toLocalFile()
                      : resource.scheme() == "qrc" ? ':' + resource.path()
                                                   : url;
    const QFileInfo info(path);
    const auto identity = QByteArray::number(info.exists()) + ':' + QByteArray::number(info.size()) + ':' +
                          QByteArray::number(info.lastModified().toMSecsSinceEpoch()) + ':' +
                          QByteArray::number(info.metadataChangeTime().toMSecsSinceEpoch());
    if (!fileIdentities_.contains(url) || fileIdentities_.value(url) != identity) {
      fileIdentities_.insert(url, identity);
      auto decoded = image_decoder::decodeFileFallback(path);
      if (decoded.isNull())
        decoded.load(path);
      store(url, decoded);
      // An oversized, unleased one-shot image need not fit in the warm cache.
      if (!cache_.contains(url))
        return decoded;
    }
    if (!path.startsWith(":/")) {
      localFiles_[info.absoluteFilePath()].insert(url);
      if (info.exists() && !files_.files().contains(info.absoluteFilePath()))
        files_.addPath(info.absoluteFilePath());
      if (!files_.directories().contains(info.absolutePath()))
        files_.addPath(info.absolutePath());
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
  const QUrl requestUrl(url);
  QNetworkRequest request(requestUrl);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  QNetworkReply* reply = network_->get(request);
  pending_.insert(url, reply);

  connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
    reply->deleteLater();
    pending_.remove(url);
    if (users_.contains(url) && users_.value(url).expired()) {
      remove(url); // Reader closed before the deferred cancellation ran.
      return;
    }

    if (reply->error() != QNetworkReply::NoError) {
      store(url, {});
      return;
    }
    const QByteArray data = reply->readAll();
    if (data.isEmpty()) {
      store(url, {});
      return;
    }
    QImage image = image_decoder::decodeFallback(data); // png/jpeg/webp/avif/svg via bundled libs
    if (image.isNull()) {
      image.loadFromData(data); // last resort for formats we don't ship
      if (image.isNull()) {
        store(url, {});
        return;
      }
    }
    store(url, std::move(image));
  });
}

} // namespace muffin
