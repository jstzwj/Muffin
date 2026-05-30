#pragma once

struct cmark_node;

class QString;

namespace Md {

class MdSerializer {
public:
    static QString toMarkdown(cmark_node *root);
};

} // namespace Md
