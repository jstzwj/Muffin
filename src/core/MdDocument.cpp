#include "MdDocument.h"
#include "MdParser.h"
#include "MdSerializer.h"

namespace Md {

MdDocument::MdDocument(QObject *parent)
    : QObject(parent) {}

bool MdDocument::loadFromMarkdown(const QString &markdown) {
    MdParser parser;
    m_root = parser.parse(markdown);
    m_nodeIndex.clear();
    // TODO: build node index by walking tree
    emit documentReset();
    return m_root != nullptr;
}

QString MdDocument::toMarkdown() const {
    if (!m_root) return {};
    return MdSerializer::toMarkdown(m_root.get());
}

MdNode *MdDocument::nodeById(NodeId id) const {
    return m_nodeIndex.value(id, nullptr);
}

} // namespace Md
