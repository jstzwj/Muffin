#pragma once

#include <QString>
#include <QList>
#include <cstdint>

struct cmark_node;

namespace Md {

using NodeId = uint32_t;

enum class BlockType : uint8_t {
    Document, Paragraph, Heading, BlockQuote, List, ListItem,
    CodeBlock, HtmlBlock, MathBlock, MathFenced, ThematicBreak,
    Table, TableRow, TableCell,
    FootnoteDef, MetaBlock,
};

enum class InlineType : uint8_t {
    Text, SoftBreak, LineBreak,
    Emph, Strong, Del, Highlight,
    Code, Link, Image, Autolink,
    InlineMath, FootnoteRef,
    HtmlInline, Strikethrough,
};

struct InlineToken {
    InlineType type = InlineType::Text;
    QString text;
    QString marker;
    QString href;
    QString title;
    QString src;
    QString alt;
    QList<InlineToken> children;
};

class MdNode {
public:
    explicit MdNode(cmark_node *node, NodeId id);
    ~MdNode();

    NodeId id() const { return m_id; }
    cmark_node *cmarkNode() const { return m_node; }

    BlockType blockType() const;
    QString literal() const;

    // Heading
    int headingLevel() const;

    // Code block
    QString language() const;

    // Math block
    QString mathSource() const;

    // Table
    QList<int> tableAlign() const;

    // Tree navigation
    MdNode *parent() const;
    MdNode *firstChild() const;
    MdNode *nextSibling() const;

    // Inline children
    const QList<InlineToken> &inlineTokens() const { return m_inlines; }

    // Source position
    int startLine() const;
    int startColumn() const;
    int endLine() const;
    int endColumn() const;

    cmark_node *cmarkNodePtr() const { return m_node; }

private:
    friend class MdDocument;
    NodeId m_id;
    cmark_node *m_node;
    QList<InlineToken> m_inlines;

    void extractInlines();
};

} // namespace Md
