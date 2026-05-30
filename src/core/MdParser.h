#pragma once

#include "MdNode.h"
#include <memory>

struct cmark_node;

namespace Md {

struct ParseResult {
    std::unique_ptr<MdNode> mdRoot;
    cmark_node *cmarkRoot = nullptr;
};

class MdParser {
public:
    MdParser();
    ~MdParser();

    ParseResult parse(const QString &markdown);

private:
    MdNode *buildTree(cmark_node *root);
    NodeId m_nextId = 1;
};

} // namespace Md
