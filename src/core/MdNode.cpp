#include "MdNode.h"

#include <cmark-gfm.h>
#include <cmark-gfm-extension_api.h>
#include <cmark-gfm-core-extensions.h>
extern "C" {
#include <strikethrough.h>
#include <table.h>
#include <cmark-gfm-math.h>
}

namespace Md {

static BlockType toBlockType(cmark_node_type type) {
    switch (type) {
    case CMARK_NODE_DOCUMENT:     return BlockType::Document;
    case CMARK_NODE_PARAGRAPH:    return BlockType::Paragraph;
    case CMARK_NODE_HEADING:      return BlockType::Heading;
    case CMARK_NODE_BLOCK_QUOTE:  return BlockType::BlockQuote;
    case CMARK_NODE_LIST:         return BlockType::List;
    case CMARK_NODE_ITEM:         return BlockType::ListItem;
    case CMARK_NODE_CODE_BLOCK:   return BlockType::CodeBlock;
    case CMARK_NODE_HTML_BLOCK:   return BlockType::HtmlBlock;
    case CMARK_NODE_THEMATIC_BREAK: return BlockType::ThematicBreak;
    default: break;
    }
    // Check GFM extension types (these are dynamic, registered at runtime)
    if (CMARK_NODE_TABLE && type == CMARK_NODE_TABLE)
        return BlockType::Table;
    if (CMARK_NODE_TABLE_ROW && type == CMARK_NODE_TABLE_ROW)
        return BlockType::TableRow;
    if (CMARK_NODE_TABLE_CELL && type == CMARK_NODE_TABLE_CELL)
        return BlockType::TableCell;
    if (CMARK_NODE_MATH_DISPLAY && type == CMARK_NODE_MATH_DISPLAY)
        return BlockType::MathBlock;
    if (CMARK_NODE_MATH_FENCED && type == CMARK_NODE_MATH_FENCED)
        return BlockType::MathFenced;
    return BlockType::Paragraph;
}

static InlineType toInlineType(cmark_node_type type) {
    switch (type) {
    case CMARK_NODE_TEXT:           return InlineType::Text;
    case CMARK_NODE_SOFTBREAK:      return InlineType::SoftBreak;
    case CMARK_NODE_LINEBREAK:      return InlineType::LineBreak;
    case CMARK_NODE_EMPH:           return InlineType::Emph;
    case CMARK_NODE_STRONG:         return InlineType::Strong;
    case CMARK_NODE_CODE:           return InlineType::Code;
    case CMARK_NODE_LINK:           return InlineType::Link;
    case CMARK_NODE_IMAGE:          return InlineType::Image;
    case CMARK_NODE_HTML_INLINE:    return InlineType::HtmlInline;
    default: break;
    }
    // GFM extension types (dynamic)
    if (CMARK_NODE_STRIKETHROUGH && type == CMARK_NODE_STRIKETHROUGH)
        return InlineType::Strikethrough;
    if (CMARK_NODE_MATH_INLINE && type == CMARK_NODE_MATH_INLINE)
        return InlineType::InlineMath;
    return InlineType::Text;
}

MdNode::MdNode(cmark_node *node, NodeId id)
    : m_id(id), m_node(node) {
    extractInlines();
}

MdNode::~MdNode() = default;

BlockType MdNode::blockType() const {
    return toBlockType(cmark_node_get_type(m_node));
}

QString MdNode::literal() const {
    const char *lit = cmark_node_get_literal(m_node);
    return lit ? QString::fromUtf8(lit) : QString();
}

int MdNode::headingLevel() const {
    return cmark_node_get_heading_level(m_node);
}

QString MdNode::language() const {
    const char *info = cmark_node_get_fence_info(m_node);
    return info ? QString::fromUtf8(info) : QString();
}

QString MdNode::mathSource() const {
    // Math content is stored as literal of the code block or as children
    const char *lit = cmark_node_get_literal(m_node);
    if (lit) return QString::fromUtf8(lit);
    // Walk children for text content
    QString result;
    cmark_node *child = cmark_node_first_child(m_node);
    while (child) {
        const char *clit = cmark_node_get_literal(child);
        if (clit) result += QString::fromUtf8(clit);
        child = cmark_node_next(child);
    }
    return result;
}

QList<int> MdNode::tableAlign() const {
    // TODO: extract table alignment from extension data
    return {};
}

MdNode *MdNode::parent() const { return nullptr; /* TODO */ }
MdNode *MdNode::firstChild() const { return nullptr; /* TODO */ }
MdNode *MdNode::nextSibling() const { return nullptr; /* TODO */ }

int MdNode::startLine() const { return cmark_node_get_start_line(m_node); }
int MdNode::startColumn() const { return cmark_node_get_start_column(m_node); }
int MdNode::endLine() const { return cmark_node_get_end_line(m_node); }
int MdNode::endColumn() const { return cmark_node_get_end_column(m_node); }

void MdNode::extractInlines() {
    m_inlines.clear();
    // Only blocks that contain inlines have inline children
    cmark_node *child = cmark_node_first_child(m_node);
    while (child) {
        if (cmark_node_get_type(child) & CMARK_NODE_TYPE_INLINE) {
            InlineToken token;
            token.type = toInlineType(cmark_node_get_type(child));
            const char *lit = cmark_node_get_literal(child);
            if (lit) token.text = QString::fromUtf8(lit);
            if (token.type == InlineType::Link || token.type == InlineType::Image) {
                const char *url = cmark_node_get_url(child);
                if (url) token.href = QString::fromUtf8(url);
                const char *title = cmark_node_get_title(child);
                if (title) token.title = QString::fromUtf8(title);
            }
            // TODO: recurse for nested inlines (e.g., emph inside link)
            m_inlines.append(token);
        }
        child = cmark_node_next(child);
    }
}

} // namespace Md
