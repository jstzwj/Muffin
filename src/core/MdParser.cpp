#include "MdParser.h"

#include <cmark-gfm.h>
#include <cmark-gfm-extension_api.h>
#include <cmark-gfm-core-extensions.h>
#include <registry.h>

namespace Md {

MdParser::MdParser() = default;

MdParser::~MdParser() = default;

ParseResult MdParser::parse(const QString &markdown) {
    cmark_gfm_core_extensions_ensure_registered();

    int options = CMARK_OPT_DEFAULT | CMARK_OPT_UNSAFE;
    cmark_parser *parser = cmark_parser_new(options);

    cmark_llist *extensions = cmark_list_syntax_extensions(cmark_get_default_mem_allocator());
    for (cmark_llist *it = extensions; it; it = it->next) {
        cmark_parser_attach_syntax_extension(parser,
            static_cast<cmark_syntax_extension *>(it->data));
    }
    cmark_llist_free(cmark_get_default_mem_allocator(), extensions);

    QByteArray utf8 = markdown.toUtf8();
    cmark_parser_feed(parser, utf8.constData(), utf8.size());

    cmark_node *root = cmark_parser_finish(parser);

    ParseResult result;
    result.cmarkRoot = root;
    if (root) {
        result.mdRoot.reset(buildTree(root));
    }

    cmark_parser_free(parser);
    return result;
}

MdNode *MdParser::buildTree(cmark_node *cnode) {
    if (!cnode) return nullptr;

    MdNode *node = new MdNode(cnode, m_nextId++);

    cmark_node *child = cmark_node_first_child(cnode);
    while (child) {
        buildTree(child);
        child = cmark_node_next(child);
    }

    return node;
}

} // namespace Md
