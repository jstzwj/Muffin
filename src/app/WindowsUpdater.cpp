#include "app/WindowsUpdater.h"
#include "app/MainWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkReply>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QVersionNumber>
#include <utility>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <msi.h>
#include <tlhelp32.h>
#endif

namespace {
QString installerPath(const QTemporaryDir& staging) {
  return staging.filePath(QStringLiteral("update.exe"));
}

void cancelHandoff(const QString& ready) {
  QFile decision(ready + QStringLiteral(".decision"));
  if (decision.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
    decision.write("cancel");
    decision.close();
  }
  QFile cancel(ready + QStringLiteral(".cancel"));
  if (cancel.open(QIODevice::WriteOnly))
    cancel.close();
  // No install decision was issued. Late helper startup can only abort, so
  // the staged files can be retired after their executable lock is released.
  QFile finished(QFileInfo(ready).dir().filePath(QStringLiteral("update.finished")));
  if (finished.open(QIODevice::WriteOnly))
    finished.close();
}

#ifdef Q_OS_WIN
void cleanupCompletedDownloads(QSettings& settings) {
  const QDir temporary(QStandardPaths::writableLocation(QStandardPaths::TempLocation));
  QStringList pending;
  for (const auto& path : settings.value(QStringLiteral("update/stagingFolders")).toStringList()) {
    const QFileInfo folder(path);
    if (!folder.exists())
      continue;
    // Only remove our known files from direct, non-linked children of Temp.
    // Never recursively delete a directory supplied by settings.
    if (folder.isSymLink() || !folder.isDir() || !folder.fileName().startsWith(QStringLiteral("Muffin-update-")) ||
        folder.dir().canonicalPath() != temporary.canonicalPath())
      continue;
    const QDir directory(path);
    if (!QFile::exists(directory.filePath(QStringLiteral("update.finished")))) {
      pending.append(path);
      continue;
    }
    QFile installer(directory.filePath(QStringLiteral("update.exe")));
    if (installer.exists() && !installer.remove()) {
      pending.append(path); // The bundle may still be exiting. Retry next launch.
      continue;
    }
    const QRegularExpression marker(QStringLiteral("^update(?:-[0-9a-f]{32})?\\.ready(?:\\.cancel|\\.decision)?$"));
    for (const auto& name : directory.entryList(QDir::Files))
      if (marker.match(name).hasMatch())
        QFile::remove(directory.filePath(name));
    QFile::remove(directory.filePath(QStringLiteral("update.finished")));
    temporary.rmdir(folder.fileName()); // Leave any unrecognized files alone.
  }
  settings.setValue(QStringLiteral("update/stagingFolders"), pending);
}
#endif

class UpdateDialog final : public QDialog {
public:
  explicit UpdateDialog(std::function<bool()> busy) : busy_(std::move(busy)) {}

  void reject() override {
    if (!busy_())
      QDialog::reject();
  }

protected:
  void closeEvent(QCloseEvent* event) override {
    if (busy_())
      event->ignore();
    else
      QDialog::closeEvent(event);
  }

private:
  std::function<bool()> busy_;
};
}

muffin::WindowsUpdater::WindowsUpdater() : QObject(qApp) {
}

muffin::WindowsUpdater::~WindowsUpdater() {
  if (reply_) {
    disconnect(reply_, nullptr, this, nullptr);
    reply_->abort();
  }
  delete dialog_.data();
}

muffin::WindowsUpdater& muffin::WindowsUpdater::instance() {
  static QPointer<WindowsUpdater> updater;
  if (!updater)
    updater = new WindowsUpdater;
  return *updater;
}

bool muffin::WindowsUpdater::installedCopy() {
#ifdef Q_OS_WIN
  wchar_t path[32768];
  DWORD length = DWORD(std::size(path));
  if (MsiLocateComponentW(L"{d2e3f4a5-b6c7-4d8e-9f0a-1b2c3d4e5f6a}", path, &length) != INSTALLSTATE_LOCAL)
    return false;
  const QString installed = QFileInfo(QString::fromWCharArray(path)).canonicalFilePath();
  return !installed.isEmpty() &&
         installed.compare(QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath(),
                           Qt::CaseInsensitive) == 0;
#else
  return false;
#endif
}

bool muffin::WindowsUpdater::anotherProcessRunning() {
#ifdef Q_OS_WIN
  const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE)
    return true;
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  bool found = true;
  if (Process32FirstW(snapshot, &entry)) {
    found = false;
    do {
      if (entry.th32ProcessID == GetCurrentProcessId() || _wcsicmp(entry.szExeFile, L"Muffin.exe") != 0)
        continue;
      const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
      if (!process) {
        found = true;
        break;
      }
      wchar_t path[32768];
      DWORD length = DWORD(std::size(path));
      if (!QueryFullProcessImageNameW(process, 0, path, &length) ||
          QFileInfo(QString::fromWCharArray(path))
                  .canonicalFilePath()
                  .compare(QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath(),
                           Qt::CaseInsensitive) == 0)
        found = true;
      CloseHandle(process);
    } while (!found && Process32NextW(snapshot, &entry));
    if (!found && GetLastError() != ERROR_NO_MORE_FILES)
      found = true;
  }
  CloseHandle(snapshot);
  return found;
#else
  return false;
#endif
}

void muffin::WindowsUpdater::showUpdate(const UpdateRelease& release, QWidget* parent) {
  if (dialog_) {
    showExisting();
    return;
  }
  if (state_ != State::Downloading && state_ != State::Ready && !restartPending()) {
    release_ = release;
    state_ = State::Available;
    error_.clear();
  }
  // Independent of a Preferences dialog / document window lifetime.
  dialog_ = new UpdateDialog([this] { return restartPending(); });
  if (parent)
    dialog_->setPalette(parent->palette());
  dialog_->setAttribute(Qt::WA_DeleteOnClose);
  dialog_->setWindowTitle(tr("Update Muffin"));
  dialog_->setMinimumWidth(440);
  auto* layout = new QVBoxLayout(dialog_);
  label_ = new QLabel(dialog_);
  label_->setWordWrap(true);
  layout->addWidget(label_);
  progress_ = new QProgressBar(dialog_);
  layout->addWidget(progress_);
  auto* buttons = new QHBoxLayout;
  buttons->addStretch();
  primary_ = new QPushButton(dialog_);
  cancel_ = new QPushButton(tr("Cancel"), dialog_);
  buttons->addWidget(primary_);
  buttons->addWidget(cancel_);
  layout->addLayout(buttons);
  connect(primary_, &QPushButton::clicked, this, [this] {
    if (state_ == State::Ready)
      restart();
    else
      download();
  });
  connect(cancel_, &QPushButton::clicked, this, &WindowsUpdater::cancel);
  connect(dialog_, &QObject::destroyed, this, [this] {
    label_ = nullptr;
    progress_ = nullptr;
    primary_ = nullptr;
    cancel_ = nullptr;
  });
  refresh();
  dialog_->show();
}

bool muffin::WindowsUpdater::showExisting() {
  if (state_ != State::Downloading && state_ != State::Ready && !restartPending() && !dialog_)
    return false;
  if (!dialog_)
    showUpdate(release_);
  dialog_->show();
  dialog_->raise();
  dialog_->activateWindow();
  return true;
}

void muffin::WindowsUpdater::refresh() {
  if (!dialog_)
    return;
  const bool downloading = state_ == State::Downloading;
  progress_->setVisible(downloading);
  if (downloading) {
    label_->setText(tr("Downloading Muffin %1… You can continue writing.").arg(release_.version));
    progress_->setRange(0, 1000);
    progress_->setValue(release_.size > 0 ? int(written_ * 1000 / release_.size) : 0);
  } else if (state_ == State::Ready) {
    label_->setText(
        error_.isEmpty()
            ? tr("Muffin %1 is ready. Your documents will be checked for unsaved changes before restarting. Windows may ask for administrator permission.")
                  .arg(release_.version)
            : error_);
  } else if (restartPending()) {
    label_->setText(tr("Preparing to restart…"));
  } else if (state_ == State::Failed) {
    label_->setText(error_);
  } else {
    label_->setText(tr("Muffin %1 is available. Download the update now?").arg(release_.version));
  }
  primary_->setText(state_ == State::Ready ? tr("Restart and Update") : tr("Download Update"));
  primary_->setEnabled(!downloading && !restartPending());
  cancel_->setEnabled(!restartPending());
  cancel_->setText(downloading ? tr("Cancel Download") : tr("Later"));
}

void muffin::WindowsUpdater::get(const QUrl& url, qint64 limit,
                                 const std::function<void(const QByteArray&)>& completed) {
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::UserAgentHeader,
                    QStringLiteral("Muffin/%1").arg(QCoreApplication::applicationVersion()));
  request.setTransferTimeout(60000);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  auto* reply = network_.get(request);
  reply->setReadBufferSize(128 * 1024);
  reply_ = reply;
  const auto buffer = std::make_shared<QByteArray>();
  const auto consume = [this, reply, buffer, limit] {
    if (reply_ != reply)
      return;
    const auto bytes = reply->readAll();
    if (file_) {
      if (written_ + bytes.size() > limit || file_->write(bytes) != bytes.size()) {
        fail(tr("The update download could not be saved or exceeded its expected size."));
        return;
      }
      hash_.addData(bytes);
      written_ += bytes.size();
      refresh();
    } else {
      if (buffer->size() + bytes.size() > limit) {
        fail(tr("The update manifest is too large."));
        return;
      }
      buffer->append(bytes);
    }
  };
  connect(reply, &QNetworkReply::readyRead, this, consume);
  connect(reply, &QNetworkReply::finished, this, [this, reply, buffer, completed, consume] {
    reply->deleteLater();
    if (reply_ != reply)
      return;
    if (reply->error() != QNetworkReply::NoError ||
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
      fail(tr("Could not download the update: %1").arg(reply->errorString()));
      return;
    }
    // Drain any final buffered bytes through the same size/write/hash path.
    consume();
    if (reply_ != reply)
      return;
    reply_ = nullptr;
    completed(*buffer);
  });
}

void muffin::WindowsUpdater::download() {
  if (state_ != State::Available && state_ != State::Failed)
    return;
  state_ = State::Downloading;
  error_.clear();
  file_.reset();
  written_ = 0;
  refresh();
  get(release_.manifest, 16384, [this](const QByteArray& bytes) {
    package_ = UpdatePackage::verify(bytes, release_, UpdatePackage::architectureName());
    if (!package_) {
      fail(tr(
          "The update signature is invalid or the package does not match this version of Windows. Nothing was installed."));
      return;
    }
    downloadInstaller();
  });
}

void muffin::WindowsUpdater::downloadInstaller() {
  staging_ = std::make_unique<QTemporaryDir>(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                                 .filePath(QStringLiteral("Muffin-update-XXXXXX")));
  if (!staging_->isValid()) {
    fail(tr("Could not create the update download folder."));
    return;
  }
  file_ = std::make_unique<QFile>(installerPath(*staging_));
  if (!file_->open(QIODevice::WriteOnly)) {
    fail(tr("Could not create the update download folder."));
    return;
  }
  hash_.reset();
  written_ = 0;
  get(release_.installer, package_->size, [this](const QByteArray&) {
    const bool flushed = file_->flush();
    file_.reset();
    if (!flushed || written_ != package_->size || hash_.result() != package_->sha256) {
      fail(tr("The downloaded update is incomplete or has been modified. Nothing was installed."));
      return;
    }
    state_ = State::Ready;
    refresh();
    if (!dialog_)
      showUpdate(release_);
  });
}

void muffin::WindowsUpdater::cancel() {
  if (restartPending())
    return;
  if (state_ == State::Downloading) {
    if (reply_) {
      auto* reply = reply_.data();
      reply_ = nullptr;
      reply->abort();
    }
    file_.reset();
    staging_.reset();
    state_ = State::Available;
    refresh();
  } else if (dialog_) {
    dialog_->close();
  }
}

void muffin::WindowsUpdater::fail(const QString& message) {
  if (reply_) {
    auto* reply = reply_.data();
    reply_ = nullptr;
    reply->abort();
  }
  file_.reset();
  state_ = State::Failed;
  error_ = message;
  refresh();
}

void muffin::WindowsUpdater::failRestart(const QString& message) {
  // The verified download is still usable. Retry the handoff, and recheck its
  // size/hash under the same file lock, without downloading it again.
  state_ = State::Ready;
  error_ = message;
  refresh();
}

void muffin::WindowsUpdater::interruptRestart() {
  if (restartPending())
    restartInterrupted_ = true;
}

void muffin::WindowsUpdater::restart() {
  if (state_ != State::Ready || !staging_ || !package_)
    return;
  if (auto* modal = QApplication::activeModalWidget(); modal && modal != dialog_) {
    modal->raise();
    modal->activateWindow();
    return;
  }
  // Save prompts run nested event loops. Hold the restart guard before the
  // first one so another click, close request or forwarded file cannot alter
  // the prepared set of windows.
  state_ = State::Preparing;
  error_.clear();
  restartInterrupted_ = false;
  emit preparingRestart();
  refresh();
  if (restartInterrupted_) {
    state_ = State::Ready;
    refresh();
    return;
  }
  if (!installedCopy() || anotherProcessRunning()) {
    QMessageBox::information(
        dialog_, tr("Update Muffin"),
        tr("Close other running Muffin instances before updating. Only installed copies can be updated in the app."));
    state_ = State::Ready;
    refresh();
    return;
  }
  std::shared_ptr<void> packageLock;
#ifdef Q_OS_WIN
  // Keep the verified bytes immutable through CreateProcess and the helper
  // handshake, including while save dialogs run nested event loops.
  const auto path = QDir::toNativeSeparators(installerPath(*staging_));
  const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    fail(tr("The downloaded update is incomplete or has been modified. Nothing was installed."));
    return;
  }
  packageLock = std::shared_ptr<void>(handle, [](void* file) { CloseHandle(file); });
#endif
  QFile downloaded(installerPath(*staging_));
  QCryptographicHash currentHash(QCryptographicHash::Sha256);
  if (!downloaded.open(QIODevice::ReadOnly) || downloaded.size() != package_->size ||
      !currentHash.addData(&downloaded) || currentHash.result() != package_->sha256) {
    fail(tr("The downloaded update is incomplete or has been modified. Nothing was installed."));
    return;
  }
  downloaded.close();
  QList<QPointer<MainWindow>> windows;
  for (auto* widget : QApplication::topLevelWidgets())
    if (auto* window = qobject_cast<MainWindow*>(widget))
      windows.append(window);
  // Freeze prepared windows while later save prompts run. No window closes on Cancel.
  for (const auto& window : windows) {
    if (!window || !window->isEnabled() || !window->prepareUpdateClose() || restartInterrupted_) {
      for (const auto& candidate : windows)
        if (candidate)
          candidate->finishUpdateClose(false);
      state_ = State::Ready;
      refresh();
      return;
    }
    window->setEnabled(false);
  }
  // A forwarded open request may have arrived inside a save dialog's nested
  // event loop. It takes precedence over restarting and remains queued until
  // the restart guard is released.
  if (restartInterrupted_) {
    for (const auto& window : windows)
      if (window)
        window->finishUpdateClose(false);
    state_ = State::Ready;
    refresh();
    return;
  }
  QVariantList snapshots;
  for (const auto& window : windows) {
    if (!window)
      continue;
    snapshots.append(window->sessionState());
  }
  QSettings settings;
  settings.setValue(QStringLiteral("update/resumeWindows"), snapshots);
  auto downloads = settings.value(QStringLiteral("update/stagingFolders")).toStringList();
  if (!downloads.contains(staging_->path()))
    downloads.append(staging_->path());
  settings.setValue(QStringLiteral("update/stagingFolders"), downloads);
  settings.sync();
  if (settings.status() != QSettings::NoError) {
    settings.remove(QStringLiteral("update/resumeWindows"));
    for (const auto& window : windows)
      if (window)
        window->finishUpdateClose(false);
    failRestart(tr("Could not save the session before updating."));
    return;
  }
  // Never reuse an attempt's decisions: a late helper from a canceled attempt
  // must not acknowledge or authorize a later retry of the same download.
  const QString ready =
      staging_->filePath(QStringLiteral("update-%1.ready").arg(QUuid::createUuid().toString(QUuid::Id128)));
  const QString token = QStringLiteral("%1:%2:%3")
                            .arg(QCoreApplication::applicationPid())
                            .arg(QString::fromLatin1(QCoreApplication::applicationFilePath().toUtf8().toBase64()),
                                 QString::fromLatin1(ready.toUtf8().toBase64()));
  QProcess process;
  process.setProgram(installerPath(*staging_));
  process.setArguments({QStringLiteral("/install"), QStringLiteral("/passive"), QStringLiteral("/norestart"),
                        QStringLiteral("MuffinUpdate=") + token});
  if (!process.startDetached()) {
    settings.remove(QStringLiteral("update/resumeWindows"));
    for (const auto& window : windows)
      if (window)
        window->finishUpdateClose(false);
    failRestart(tr("Could not start the update installer. Your current version is unchanged."));
    return;
  }
  // The helper marks completion; a later startup removes only completed
  // downloads, after the bundle has released its executable.
  staging_->setAutoRemove(false);
  state_ = State::Installing;
  refresh();
  auto* timer = new QTimer(this);
  auto elapsed = std::make_shared<QElapsedTimer>();
  elapsed->start();
  connect(timer, &QTimer::timeout, this, [this, timer, elapsed, ready, windows, packageLock] {
    Q_UNUSED(packageLock);
    const bool canceled = restartInterrupted_ || QFile::exists(ready + QStringLiteral(".cancel"));
    if (!canceled && QFile::exists(ready)) {
      timer->stop();
      timer->deleteLater();
      // Readiness alone never authorizes installation: a crash or an ordinary
      // quit while waiting must leave the installed copy untouched.
      QFile commit(ready + QStringLiteral(".decision"));
      const bool created = commit.open(QIODevice::WriteOnly | QIODevice::NewOnly);
      if (!created || commit.write("install") != 7 || !commit.flush()) {
        if (created) {
          commit.close();
          commit.remove();
        }
        cancelHandoff(ready);
        QSettings().remove(QStringLiteral("update/resumeWindows"));
        for (const auto& window : windows)
          if (window)
            window->finishUpdateClose(false);
        failRestart(tr("Could not save the session before updating."));
        return;
      }
      commit.close();
      for (const auto& window : windows)
        if (window) {
          window->finishUpdateClose(true);
          window->close();
        }
      QCoreApplication::quit();
    } else if (canceled || elapsed->elapsed() > 30000) {
      timer->stop();
      timer->deleteLater();
      cancelHandoff(ready);
      QSettings().remove(QStringLiteral("update/resumeWindows"));
      for (const auto& window : windows)
        if (window)
          window->finishUpdateClose(false);
      failRestart(tr("The installer did not become ready. Muffin will stay open; please try again."));
    }
  });
  timer->start(100);
}

muffin::MainWindow* muffin::WindowsUpdater::restoreAfterUpdate() {
#ifndef Q_OS_WIN
  return nullptr;
#else
  QSettings settings;
  cleanupCompletedDownloads(settings);
  const auto snapshots = settings.value(QStringLiteral("update/resumeWindows")).toList();
  if (snapshots.isEmpty())
    return nullptr;
  // Consume once, even if a failed upgrade is followed by a manual launch.
  settings.remove(QStringLiteral("update/resumeWindows"));
  settings.sync();
  MainWindow* first = nullptr;
  for (const auto& snapshot : snapshots) {
    auto* window = new MainWindow;
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->restoreSessionState(snapshot.toMap());
    window->show();
    if (!first)
      first = window;
  }
  return first;
#endif
}
