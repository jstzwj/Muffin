#pragma once

#include <QString>
#include <QStringView>

#include <cstdint>

namespace muffin {

// Node identity: a compact 64-bit counter. Was a short QString "n<counter>" — a heap QStringData
// per node (~50B × every block node). Now an inline quint64 (8B), saving ~97MB measured on a
// 100MB dense doc (1.07M block nodes). Not persisted: regenerated on every parse; only round-
// tripped through the outline UI (SidebarWidget). A default-constructed NodeId (id_ == 0) is the
// invalid sentinel.
class NodeId {
public:
  NodeId() = default;

  static NodeId create();
  static NodeId fromString(QStringView value);

  QString toString() const;
  bool isValid() const;

  friend bool operator==(const NodeId&, const NodeId&) = default;
  friend uint qHash(const NodeId& id, uint seed);

private:
  explicit NodeId(quint64 id);

  quint64 id_ = 0;
};

uint qHash(const NodeId& id, uint seed = 0);

}  // namespace muffin
