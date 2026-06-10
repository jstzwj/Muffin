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
  /// If a check is already in progress, this is a no-op.
  void checkForUpdates();

  /// Check if auto-check is enabled in QSettings and enough time has
  /// elapsed since the last check (24 hours). If so, perform a silent check.
  void maybeAutoCheck();

  /// Whether the current (or most recent) check was initiated by the user
  /// (as opposed to the automatic startup check).
  bool isUserInitiated() const;

signals:
  /// Emitted when a newer version is available.
  /// version = "x.y.z" (without "v" prefix), url = release page URL.
  void updateAvailable(QString version, QString url);

  /// Emitted when the current version matches the latest release.
  void upToDate();

  /// Emitted when the network request or JSON parsing fails.
  void checkFailed(QString errorMessage);

private:
  explicit UpdateChecker(QObject* parent = nullptr);

  QNetworkAccessManager network_;
  bool checking_ = false;
  bool userInitiated_ = true;
};

}  // namespace muffin
