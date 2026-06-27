#include "document/NodeId.h"

#include <QAtomicInteger>

namespace muffin {
namespace {
QAtomicInteger<quint64> nextNodeId = 1;
}

NodeId::NodeId(quint64 id) : id_(id) {}

NodeId NodeId::create() {
  return NodeId(nextNodeId.fetchAndAddRelaxed(1));
}

NodeId NodeId::fromString(QStringView value) {
  // Empty (or missing) string → invalid sentinel. Matches the old QString-based contract where ""
  // was not a valid id; without this guard the FNV fallback below would hash "" into a real id.
  if (value.isEmpty()) {
    return NodeId();
  }
  // Numeric strings are the exact inverse of toString() (the outline UI round-trips ids this way).
  bool ok = false;
  const quint64 parsed = value.toULongLong(&ok);
  if (ok) {
    return NodeId(parsed);
  }
  // Non-numeric labels (test fixtures use semantic tags like "heading"/"list"/"block"): hash to a
  // STABLE uint64 so the same label always yields the same id (preserving fromString's "same
  // string → same id" contract), distinct labels collide negligibly. FNV-1a, bit 0 forced on so
  // the result is never the 0 invalid sentinel.
  quint64 h = 1469598103934665603ULL;
  for (QChar ch : value) {
    h ^= static_cast<quint64>(ch.unicode());
    h *= 1099511628211ULL;
  }
  return NodeId(h | 1ULL);
}

QString NodeId::toString() const {
  return QString::number(id_);
}

bool NodeId::isValid() const {
  return id_ != 0;
}

uint qHash(const NodeId& id, uint seed) {
  // Mix both halves of the 64-bit id into the 32-bit hash (dependency-free).
  return seed ^ static_cast<quint32>(id.id_) ^ static_cast<quint32>(id.id_ >> 32);
}

}  // namespace muffin
