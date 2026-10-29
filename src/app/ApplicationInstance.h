#pragma once

#include <QLocalServer>
#include <QLockFile>
#include <QObject>
#include <QStringList>

namespace muffin {

// Only external file-open requests cross this boundary. Explicit new-window
// commands and folder workspaces keep their existing independent lifetimes.
class ApplicationInstance final : public QObject {
  Q_OBJECT

public:
  explicit ApplicationInstance(const QString& identity, QObject* parent = nullptr);
  static QString profileIdentity();

  // Keep the listener even when tab reuse is disabled, so enabling the setting
  // takes effect without restarting. A lifetime lock makes stale socket cleanup
  // safe and elects one listener when several files launch simultaneously.
  bool listen();
  bool forwardFiles(const QStringList& absolutePaths) const;
  bool hasPendingFileRequests() const { return pendingFileRequests_ != 0; }

signals:
  // Emitted before acknowledgement, while delivery may still be queued.
  void acceptingFileRequest();
  void filesRequested(const QStringList& absolutePaths);

private:
  QString serverName_;
  QLockFile lock_;
  QLocalServer server_;
  qsizetype pendingFileRequests_ = 0;
};

} // namespace muffin
