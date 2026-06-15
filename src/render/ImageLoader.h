#pragma once

#include <QHash>
#include <QImage>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QString>

#include <memory>

namespace muffin {

/// Async image loader with in-memory cache. Loads remote (HTTP/HTTPS) images
/// in the background and notifies via the imageReady() signal.
class ImageLoader : public QObject {
  Q_OBJECT

public:
  static ImageLoader& instance();

  /// Return a cached image for the given URL, or a null QImage if not available.
  QImage cached(const QString& url) const;

  /// Return true if the URL has been requested and is still downloading.
  bool isPending(const QString& url) const;

  /// Start an async download for a remote URL. Does nothing if already cached or pending.
  void request(const QString& url);

signals:
  /// Emitted when a remote image has been downloaded and cached.
  void imageReady(QString url);

private:
  explicit ImageLoader(QObject* parent = nullptr);

  QNetworkAccessManager* network_ = nullptr;  // Owned; torn down on aboutToQuit (see .cpp).
  QHash<QString, QImage> cache_;
  QSet<QString> pending_;
};

}  // namespace muffin
