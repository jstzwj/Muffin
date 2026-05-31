#pragma once

#include <QtGlobal>

namespace muffin {

struct SourceRange {
  qsizetype byteStart = 0;
  qsizetype byteEnd = 0;
  int lineStart = 0;
  int lineEnd = 0;
  int columnStart = 0;
  int columnEnd = 0;

  bool containsByte(qsizetype byteOffset) const {
    return byteOffset >= byteStart && byteOffset <= byteEnd;
  }

  qsizetype byteLength() const {
    return byteEnd >= byteStart ? byteEnd - byteStart : 0;
  }
};

}  // namespace muffin
