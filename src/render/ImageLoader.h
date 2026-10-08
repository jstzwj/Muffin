#pragma once

#include <QHash>
#include <QImage>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QString>
#include <QFileSystemWatcher>

#include <memory>

#include "Export.h"

namespace muffin {

/// Shared image cache with asynchronous remote loading and local file watching.
/// Publishes paint/geometry versions and notifies via imageReady().
class MUFFIN_UI_EXPORT ImageLoader : public QObject {
  Q_OBJECT

public:
  static ImageLoader& instance();

  /// Return a cached image for the given URL, or a null QImage if not available.
  QImage cached(const QString& url) const;
  // Shared local/data/remote lookup. Local files are decoded once and watched;
  // remote requests keep their existing asynchronous behavior.
  QImage image(const QString& url);
  void store(const QString& url, QImage image);

  /// Return true if the URL has been requested and is still downloading.
  bool isPending(const QString& url) const;

  /// Start an async download for a remote URL. Does nothing if already cached or pending.
  void request(const QString& url);

signals:
 /// Emitted when decoded image content changes, including failed downloads.
 void imageReady(QString url);

private:
  explicit ImageLoader(QObject* parent = nullptr);

  QNetworkAccessManager* network_ = nullptr;  // Owned; torn down on aboutToQuit (see .cpp).
  QHash<QString, QImage> cache_;
  QSet<QString> pending_;
  QFileSystemWatcher files_;
  QHash<QString, QSet<QString>> localFiles_;
  QHash<QString, QByteArray> fileIdentities_;
};

}  // namespace muffin
