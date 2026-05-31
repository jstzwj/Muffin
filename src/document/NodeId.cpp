#include "document/NodeId.h"

#include <QAtomicInteger>

namespace muffin {
namespace {
QAtomicInteger<quint64> nextNodeId = 1;
}

NodeId::NodeId(QString value) : value_(std::move(value)) {}

NodeId NodeId::create() {
  return NodeId(QStringLiteral("n%1").arg(nextNodeId.fetchAndAddRelaxed(1)));
}

NodeId NodeId::fromString(QStringView value) {
  return NodeId(value.toString());
}

QString NodeId::toString() const {
  return value_;
}

bool NodeId::isValid() const {
  return !value_.isEmpty();
}

uint qHash(const NodeId& id, uint seed) {
  uint hash = seed;
  const QString value = id.toString();
  for (const QChar ch : value) {
    hash = hash * 31u + ch.unicode();
  }
  return hash;
}

}  // namespace muffin
