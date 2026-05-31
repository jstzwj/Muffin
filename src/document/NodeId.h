#pragma once

#include <QString>
#include <QStringView>

#include <cstdint>

namespace muffin {

class NodeId {
public:
  NodeId() = default;

  static NodeId create();
  static NodeId fromString(QStringView value);

  QString toString() const;
  bool isValid() const;

  friend bool operator==(const NodeId&, const NodeId&) = default;

private:
  explicit NodeId(QString value);

  QString value_;
};

uint qHash(const NodeId& id, uint seed = 0);

}  // namespace muffin
