#include "SourceMap.h"

namespace Md {

void SourceMap::rebuild(const MdNode *root) {
    m_blockSpans.clear();
    m_nodeSpanIndex.clear();
    // TODO: build mapping during rendering
}

MdNode *SourceMap::nodeAtDocPos(int pos) const {
    // TODO: binary search in m_blockSpans
    return nullptr;
}

SourceSpan SourceMap::spanForNode(NodeId id) const {
    return m_nodeSpanIndex.value(id);
}

} // namespace Md
