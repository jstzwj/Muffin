#pragma once

#include "MdNode.h"
#include <QVector>
#include <QHash>

namespace Md {

struct SourceSpan {
    NodeId nodeId = 0;
    int blockStartPos = 0;
    int blockEndPos = 0;
    int sourceStartLine = 0;
    int sourceStartCol = 0;
    int sourceEndLine = 0;
    int sourceEndCol = 0;
};

class SourceMap {
public:
    void rebuild(const MdNode *root);

    MdNode *nodeAtDocPos(int pos) const;
    SourceSpan spanForNode(NodeId id) const;

private:
    QVector<SourceSpan> m_blockSpans;
    QHash<NodeId, SourceSpan> m_nodeSpanIndex;
};

} // namespace Md
