#include "render/LayoutResources.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QMutexLocker>
#include <vector>

namespace muffin {
namespace {
thread_local std::vector<LayoutResourceScope*> scopes;
}
LayoutResources& LayoutResources::instance() {
  // Like ImageLoader, this may outlive QApplication in a shared-library build.
  static auto* resources = new LayoutResources();
  return *resources;
}
LayoutResources::LayoutResources() {
  if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
    connect(app, &QGuiApplication::fontDatabaseChanged, this, [this] {
      const auto version = read(fontKey());
      publish(fontKey(), QByteArray::number(version.geometry + 1));
    });
}
LayoutResourceVersion LayoutResources::read(const QString& key) const {
  LayoutResourceVersion version;
  {
    QMutexLocker lock(&mutex_);
    version = entries_.value(key).version;
  }
  for (auto* scope : scopes) scope->dependencies.insert(key, version);
  return version;
}
void LayoutResources::record(const LayoutResourceDependencies& dependencies) const {
  for (auto* scope : scopes)
    for (auto it = dependencies.cbegin(); it != dependencies.cend(); ++it) scope->dependencies.insert(it.key(), it.value());
}
bool LayoutResources::matches(const LayoutResourceDependencies& dependencies, bool includePaint) const {
  QMutexLocker lock(&mutex_);
  for (auto it = dependencies.cbegin(); it != dependencies.cend(); ++it) {
    const auto current = entries_.value(it.key()).version;
    if (current.geometry != it->geometry || (includePaint && current.paint != it->paint)) return false;
  }
  return true;
}
void LayoutResources::publish(const QString& key, const QByteArray& metrics) {
  {
    QMutexLocker lock(&mutex_);
    auto& entry = entries_[key];
    if (entry.version.paint == 0 || entry.metrics != metrics) ++entry.version.geometry;
    ++entry.version.paint;
    entry.metrics = metrics;
  }
  emit resourceChanged(key);
}
LayoutResourceScope::LayoutResourceScope() { scopes.push_back(this); }
LayoutResourceScope::~LayoutResourceScope() { scopes.pop_back(); }
}  // namespace muffin
