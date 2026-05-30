#include "MdSerializer.h"

#include <cmark-gfm.h>
#include <QString>
#include <cstdlib>

namespace Md {

QString MdSerializer::toMarkdown(cmark_node *root) {
    if (!root) return {};
    char *md = cmark_render_commonmark(root, CMARK_OPT_DEFAULT, 0);
    QString result = QString::fromUtf8(md);
    free(md);
    return result;
}

} // namespace Md
