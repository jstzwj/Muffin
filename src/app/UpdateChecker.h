#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

namespace muffin {

/// Async update checker that queries the GitHub Releases API.
/// Compares the latest release version with the running application version
/// and emits signals with the result.
class UpdateChecker : public QObject {
  Q_OBJECT

public:
  static UpdateChecker& instance();

  /// Start an asynchronous check against the GitHub releases API.
  /// `requester` identifies the UI surface that should present a manual result;
  /// null is reserved for a silent automatic check. Returns false when another
  /// request is already in flight.
  bool checkForUpdates(QObject* requester = nullptr);

  /// Check if auto-check is enabled in QSettings and enough time has
  /// elapsed since the last check (24 hours). If so, perform a silent check.
  void maybeAutoCheck();

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

  QNetworkAccessManager network_;
  bool checking_ = false;
};

}  // namespace muffin
