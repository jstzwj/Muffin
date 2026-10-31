#pragma once

#include "app/UpdatePackage.h"
#include <QCryptographicHash>
#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTemporaryDir>
#include <memory>
#include <functional>

class QDialog;
class QLabel;
class QPushButton;
class QProgressBar;
class QNetworkReply;
class QWidget;

namespace muffin {

class MainWindow;

// Windows installed copies only. All other distributions keep their download-page flow.
class WindowsUpdater final : public QObject {
  Q_OBJECT
  friend struct WindowsUpdaterTestAccess;

public:
  enum class State { Available, Downloading, Ready, Preparing, Installing, Failed };
  static WindowsUpdater& instance();
  static bool installedCopy();
  static bool anotherProcessRunning();
  static MainWindow* restoreAfterUpdate();
  bool restartPending() const { return state_ == State::Preparing || state_ == State::Installing; }
  void interruptRestart();
  void showUpdate(const UpdateRelease& release, QWidget* parent = nullptr);
  bool showExisting();

signals:
  void preparingRestart();

private:
  WindowsUpdater();
  ~WindowsUpdater() override;
  void refresh();
  void download();
  void downloadInstaller();
  void cancel();
  void fail(const QString& message);
  void failRestart(const QString& message);
  void restart();
  void get(const QUrl& url, qint64 limit, const std::function<void(const QByteArray&)>& completed,
           bool allowFallback = true);
  QNetworkAccessManager network_;
  QPointer<QNetworkReply> reply_;
  UpdateRelease release_;
  std::optional<UpdatePackage> package_;
  std::unique_ptr<QTemporaryDir> staging_;
  std::unique_ptr<QFile> file_;
  QCryptographicHash hash_{QCryptographicHash::Sha256};
  qint64 written_ = 0;
  State state_ = State::Available;
  bool restartInterrupted_ = false;
  QString error_;
  QPointer<QDialog> dialog_;
  QLabel* label_ = nullptr;
  QProgressBar* progress_ = nullptr;
  QPushButton* primary_ = nullptr;
  QPushButton* cancel_ = nullptr;
};
} // namespace muffin
