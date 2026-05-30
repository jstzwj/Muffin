#pragma once

#include "editor/EditTransaction.h"

#include <QVector>

namespace Muffin {

class RenderInvalidation {
public:
    static QVector<MarkdownNodeId> invalidatedNodeIdsForTransaction(const EditTransaction& transaction);
};

} // namespace Muffin
