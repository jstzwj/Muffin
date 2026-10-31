#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include "app/UpdatePackage.h"

namespace muffin {

/// Async update checker using static CNB release metadata, with a GitHub asset fallback.
/// Compares the latest release version with the running application version
/// and emits signals with the result.
class UpdateChecker : public QObject {
  Q_OBJECT
  friend struct UpdateCheckerTestAccess;

public:
  static UpdateChecker& instance();

  /// Start an asynchronous check of the published update catalog.
  /// `requester` identifies the UI surface that should present a manual result;
  /// null is reserved for a silent automatic check. Returns false when another
  /// request is already in flight.
  bool checkForUpdates(QObject* requester = nullptr);

  /// Check if auto-check is enabled in QSettings and enough time has
  /// elapsed since the last check (24 hours). If so, perform a silent check.
  void maybeAutoCheck();
  const std::optional<UpdateRelease>& latestPackage() const { return latestPackage_; }

signals:
  /// Emitted when a newer version is available.
  /// version = "x.y.z" (without "v" prefix), url = release page URL.
  void updateAvailable(QString version, QString url, QObject* requester);

  /// Emitted when the current version matches the latest release.
  void upToDate(QObject* requester);

  /// Emitted when the network request or JSON parsing fails.
  void checkFailed(QString errorMessage, QObject* requester);

private:
  explicit UpdateChecker(QObject* parent = nullptr);
  void requestCatalog(QPointer<QObject> owner, bool manual, int source);
  bool acceptCatalog(const QByteArray& data, QObject* requester);
  QString cacheKey(int source) const;

  QNetworkAccessManager network_;
  bool checking_ = false;
  std::optional<UpdateRelease> latestPackage_;
  QList<QUrl> sources_;
};

} // namespace muffin
