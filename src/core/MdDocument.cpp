#include "MdDocument.h"
#include "MdParser.h"
#include "MdSerializer.h"

#include <cmark-gfm.h>

namespace Md {

void CmarkNodeDeleter::operator()(cmark_node *n) const {
    if (n) cmark_node_free(n);
}

MdDocument::MdDocument(QObject *parent)
    : QObject(parent) {}

MdDocument::~MdDocument() = default;

bool MdDocument::loadFromMarkdown(const QString &markdown) {
    m_root.reset();
    m_cmarkRoot.reset();

    MdParser parser;
    ParseResult result = parser.parse(markdown);
    m_root = std::move(result.mdRoot);
    m_cmarkRoot = CmarkNodePtr(result.cmarkRoot);

    m_nodeIndex.clear();

    emit documentReset();
    return m_root != nullptr;
}

QString MdDocument::toMarkdown() const {
    if (!m_cmarkRoot) return {};
    return MdSerializer::toMarkdown(m_cmarkRoot.get());
}

MdNode *MdDocument::nodeById(NodeId id) const {
    return m_nodeIndex.value(id, nullptr);
}

} // namespace Md
