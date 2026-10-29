#include "app/ApplicationInstance.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
constexpr qsizetype kMaximumRequestBytes = 1024 * 1024;
constexpr int kRequestTimeoutMs = 5000;

QString serverName(const QString& identity) {
  // Leave room for macOS's long per-user temporary directory in sockaddr_un.
  return QStringLiteral("muffin-open-%1")
      .arg(QString::fromLatin1(
          QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex().left(24)));
}
} // namespace

muffin::ApplicationInstance::ApplicationInstance(const QString& identity, QObject* parent)
    : QObject(parent), serverName_(serverName(identity)),
      lock_(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                .filePath(serverName_ + QStringLiteral(".lock"))) {
  // A responsive process can legitimately own this lock for weeks.
  lock_.setStaleLockTime(0);
  server_.setSocketOptions(QLocalServer::UserAccessOption);
  connect(&server_, &QLocalServer::newConnection, this, [this] {
    while (auto* socket = server_.nextPendingConnection()) {
      socket->setParent(this);
      socket->setReadBufferSize(kMaximumRequestBytes + 1);
      auto* timeout = new QTimer(socket);
      timeout->setSingleShot(true);
      connect(timeout, &QTimer::timeout, socket, &QLocalSocket::abort);
      connect(timeout, &QTimer::timeout, socket, &QObject::deleteLater);
      connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
      timeout->start(kRequestTimeoutMs);
      const auto readRequest = [this, socket, timeout] {
        if (socket->bytesAvailable() > kMaximumRequestBytes) {
          socket->abort();
          return;
        }
        if (!socket->canReadLine())
          return;
        timeout->stop();
        const auto document = QJsonDocument::fromJson(socket->readLine());
        const auto object = document.object();
        const auto paths = object.value(QStringLiteral("files"));
        if (object.value(QStringLiteral("version")).toInt() != 1 || !paths.isArray() || paths.toArray().isEmpty()) {
          socket->disconnectFromServer();
          return;
        }
        QStringList files;
        for (const auto& value : paths.toArray()) {
          const auto path = value.toString();
          if (path.isEmpty() || !QFileInfo(path).isAbsolute()) {
            socket->disconnectFromServer();
            return;
          }
          files.append(path);
        }
        // One request per connection. Queue delivery until after acknowledgement
        // so slow document loading or a confirmation cannot stall the launcher.
        QObject::disconnect(socket, &QLocalSocket::readyRead, this, nullptr);
        ++pendingFileRequests_;
        emit acceptingFileRequest();
        socket->write("OK\n");
        socket->disconnectFromServer();
        QTimer::singleShot(0, this, [this, files] {
          emit filesRequested(files);
          --pendingFileRequests_;
        });
      };
      connect(socket, &QLocalSocket::readyRead, this, readRequest);
      readRequest(); // The entire frame may have arrived before newConnection.
    }
  });
}

QString muffin::ApplicationInstance::profileIdentity() {
  return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QLatin1Char('\n') +
         QSettings().fileName();
}

bool muffin::ApplicationInstance::listen() {
  if (server_.isListening())
    return true;
  if (!lock_.tryLock())
    return false;
  QLocalServer::removeServer(serverName_);
  if (server_.listen(serverName_))
    return true;
  lock_.unlock();
  return false;
}

bool muffin::ApplicationInstance::forwardFiles(const QStringList& absolutePaths) const {
  if (absolutePaths.isEmpty())
    return false;
  QJsonArray files;
  for (const QString& path : absolutePaths) {
    if (!QFileInfo(path).isAbsolute())
      return false;
    files.append(path);
  }
  const auto frame = QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("files"), files}})
                         .toJson(QJsonDocument::Compact) +
                     '\n';
  if (frame.size() > kMaximumRequestBytes)
    return false;

  QLocalSocket socket;
  QElapsedTimer timer;
  timer.start();
  do {
    socket.connectToServer(serverName_, QIODevice::ReadWrite);
    if (socket.waitForConnected(kRequestTimeoutMs))
      break;
    // A competing launcher may own the lock but not yet have created its
    // socket. Give that small startup interval a chance instead of spawning
    // a second window. A vanished owner falls back to opening locally.
    qint64 processId = 0;
    QString host, application;
    if (timer.elapsed() >= kRequestTimeoutMs || !lock_.getLockInfo(&processId, &host, &application))
      return false;
    socket.abort();
    QThread::msleep(10);
  } while (true);
#ifdef Q_OS_WIN
  qint64 processId = 0;
  QString host, application;
  if (lock_.getLockInfo(&processId, &host, &application))
    AllowSetForegroundWindow(static_cast<DWORD>(processId));
#endif
  socket.write(frame);
  timer.restart();
  while (!socket.canReadLine()) {
    const auto remaining = kRequestTimeoutMs - timer.elapsed();
    if (remaining <= 0 || !socket.waitForReadyRead(static_cast<int>(remaining)))
      return false;
  }
  return socket.readLine() == "OK\n";
}
