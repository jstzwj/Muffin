#pragma once

namespace Muffin {

struct SourceSpan {
    int start = -1;
    int end = -1;

    bool isValid() const { return start >= 0 && end >= start; }
    bool contains(int offset) const { return offset >= start && offset <= end; }
};

} // namespace Muffin
