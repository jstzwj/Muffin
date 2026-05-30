#pragma once

#include <QtGlobal>

class QString;

namespace Muffin {

struct SourceSelection {
    int start = 0;
    int end = 0;

    int normalizedStart() const { return qMin(start, end); }
    int normalizedEnd() const { return qMax(start, end); }
    bool isValidFor(const QString& text) const;
};

} // namespace Muffin
