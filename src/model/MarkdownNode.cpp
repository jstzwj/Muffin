#include "MarkdownNode.h"

namespace Muffin {

QString markdownNodeTypeName(MarkdownNodeType type)
{
    switch (type) {
    case MarkdownNodeType::Document: return QStringLiteral("document");
    case MarkdownNodeType::Heading: return QStringLiteral("heading");
    case MarkdownNodeType::Paragraph: return QStringLiteral("paragraph");
    case MarkdownNodeType::BlockQuote: return QStringLiteral("blockquote");
    case MarkdownNodeType::List: return QStringLiteral("list");
    case MarkdownNodeType::ListItem: return QStringLiteral("list_item");
    case MarkdownNodeType::CodeBlock: return QStringLiteral("code_block");
    case MarkdownNodeType::ThematicBreak: return QStringLiteral("thematic_break");
    case MarkdownNodeType::HtmlBlock: return QStringLiteral("html_block");
    case MarkdownNodeType::Text: return QStringLiteral("text");
    case MarkdownNodeType::SoftBreak: return QStringLiteral("softbreak");
    case MarkdownNodeType::LineBreak: return QStringLiteral("linebreak");
    case MarkdownNodeType::Emphasis: return QStringLiteral("emphasis");
    case MarkdownNodeType::Strong: return QStringLiteral("strong");
    case MarkdownNodeType::InlineCode: return QStringLiteral("inline_code");
    case MarkdownNodeType::Link: return QStringLiteral("link");
    case MarkdownNodeType::Image: return QStringLiteral("image");
    case MarkdownNodeType::Table: return QStringLiteral("table");
    case MarkdownNodeType::TableRow: return QStringLiteral("table_row");
    case MarkdownNodeType::TableCell: return QStringLiteral("table_cell");
    case MarkdownNodeType::FormulaInline: return QStringLiteral("formula_inline");
    case MarkdownNodeType::FormulaBlock: return QStringLiteral("formula_block");
    case MarkdownNodeType::Strikethrough: return QStringLiteral("strikethrough");
    case MarkdownNodeType::Unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

MarkdownNodeType markdownNodeTypeFromCmark(const AstNode& node)
{
    if (node.isNull()) {
        return MarkdownNodeType::Unknown;
    }

    switch (node.type()) {
    case CMARK_NODE_DOCUMENT: return MarkdownNodeType::Document;
    case CMARK_NODE_HEADING: return MarkdownNodeType::Heading;
    case CMARK_NODE_PARAGRAPH: return MarkdownNodeType::Paragraph;
    case CMARK_NODE_BLOCK_QUOTE: return MarkdownNodeType::BlockQuote;
    case CMARK_NODE_LIST: return MarkdownNodeType::List;
    case CMARK_NODE_ITEM: return MarkdownNodeType::ListItem;
    case CMARK_NODE_CODE_BLOCK: return MarkdownNodeType::CodeBlock;
    case CMARK_NODE_THEMATIC_BREAK: return MarkdownNodeType::ThematicBreak;
    case CMARK_NODE_HTML_BLOCK: return MarkdownNodeType::HtmlBlock;
    case CMARK_NODE_TEXT: return MarkdownNodeType::Text;
    case CMARK_NODE_SOFTBREAK: return MarkdownNodeType::SoftBreak;
    case CMARK_NODE_LINEBREAK: return MarkdownNodeType::LineBreak;
    case CMARK_NODE_EMPH: return MarkdownNodeType::Emphasis;
    case CMARK_NODE_STRONG: return MarkdownNodeType::Strong;
    case CMARK_NODE_CODE: return MarkdownNodeType::InlineCode;
    case CMARK_NODE_LINK: return MarkdownNodeType::Link;
    case CMARK_NODE_IMAGE: return MarkdownNodeType::Image;
    default:
        if (node.type() == CMARK_NODE_TABLE) {
            return MarkdownNodeType::Table;
        }
        if (node.type() == CMARK_NODE_TABLE_ROW) {
            return MarkdownNodeType::TableRow;
        }
        if (node.type() == CMARK_NODE_TABLE_CELL) {
            return MarkdownNodeType::TableCell;
        }
        if (node.type() == CMARK_NODE_STRIKETHROUGH) {
            return MarkdownNodeType::Strikethrough;
        }
        return MarkdownNodeType::Unknown;
    }
}

} // namespace Muffin
