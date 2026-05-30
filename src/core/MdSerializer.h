#pragma once

class QString;

namespace Md {

class MdNode;

class MdSerializer {
public:
    static QString toMarkdown(const MdNode *root);
};

} // namespace Md
