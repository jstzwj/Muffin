#pragma once

#include <QtGlobal>

namespace muffin {

struct TopLevelRangeChange {
  qsizetype first = -1;
  qsizetype oldCount = 0;
  qsizetype newCount = 0;
  quint64 documentRevision = 0;

  bool isValid() const {
    return first >= 0 && oldCount >= 0 && newCount >= 0 && documentRevision > 0;
  }

  friend bool operator==(const TopLevelRangeChange&, const TopLevelRangeChange&) = default;
};

}  // namespace muffin
