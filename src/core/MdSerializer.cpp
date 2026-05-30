#include "MdSerializer.h"
#include "MdNode.h"

#include <cmark-gfm.h>

namespace Md {

QString MdSerializer::toMarkdown(const MdNode *root) {
    if (!root || !root->cmarkNodePtr()) return {};
    char *md = cmark_render_commonmark(root->cmarkNodePtr(), CMARK_OPT_DEFAULT, 0);
    QString result = QString::fromUtf8(md);
    free(md);
    return result;
}

} // namespace Md
