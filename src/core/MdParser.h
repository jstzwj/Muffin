#pragma once

#include "MdNode.h"
#include <memory>

struct cmark_node;
struct cmark_parser;

namespace Md {

class MdParser {
public:
    MdParser();
    ~MdParser();

    std::unique_ptr<MdNode> parse(const QString &markdown);

private:
    MdNode *buildTree(cmark_node *root);

    NodeId m_nextId = 1;
};

} // namespace Md
