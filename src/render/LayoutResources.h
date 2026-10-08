#pragma once

#include "Export.h"
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QByteArray>

namespace muffin {
struct LayoutResourceVersion {
  quint64 paint = 0, geometry = 0;
  bool operator==(const LayoutResourceVersion&) const = default;
};
using LayoutResourceDependencies = QHash<QString, LayoutResourceVersion>;

// GUI resource producers publish immutable metric identities. Paint changes do
// not invalidate measurements; geometry changes do. Reads collect dependencies
// on the calling thread, including dependencies consumed from a cached child.
class MUFFIN_CORE_EXPORT LayoutResources : public QObject {
  Q_OBJECT
 public:
  static LayoutResources& instance();
  static QString fontKey() { return QStringLiteral("fonts"); }
  static QString imageKey(const QString& url) { return QStringLiteral("image:") + url; }
  LayoutResourceVersion read(const QString& key) const;
  void record(const LayoutResourceDependencies& dependencies) const;
  bool matches(const LayoutResourceDependencies& dependencies, bool includePaint = true) const;
  void publish(const QString& key, const QByteArray& metrics);
 signals:
  void resourceChanged(QString key);

 private:
  LayoutResources();
  struct Entry {
    LayoutResourceVersion version;
    QByteArray metrics;
  };
  mutable QMutex mutex_;
  QHash<QString, Entry> entries_;
};

class MUFFIN_CORE_EXPORT LayoutResourceScope {
 public:
  LayoutResourceScope();
  ~LayoutResourceScope();
  LayoutResourceScope(const LayoutResourceScope&) = delete;
  LayoutResourceScope& operator=(const LayoutResourceScope&) = delete;
  LayoutResourceDependencies dependencies;
};
}  // namespace muffin
