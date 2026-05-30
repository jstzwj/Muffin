#pragma once

#include "parser/AstNode.h"
#include "parser/SourceSpan.h"

#include <QString>
#include <QVector>

namespace Muffin {

using MarkdownNodeId = quint64;

enum class MarkdownNodeType {
    Document,
    Heading,
    Paragraph,
    BlockQuote,
    List,
    ListItem,
    CodeBlock,
    ThematicBreak,
    HtmlBlock,
    Text,
    SoftBreak,
    LineBreak,
    Emphasis,
    Strong,
    InlineCode,
    Link,
    Image,
    Table,
    TableRow,
    TableCell,
    FormulaInline,
    FormulaBlock,
    Strikethrough,
    Unknown
};

struct MarkdownInlineToken {
    MarkdownNodeType type = MarkdownNodeType::Unknown;
    SourceSpan source;
    QString literal;
};

struct MarkdownNode {
    MarkdownNodeId id = 0;
    MarkdownNodeId parent = 0;
    MarkdownNodeType type = MarkdownNodeType::Unknown;

    SourceSpan source;
    SourceSpan content;
    SourceRange sourceRange;

    QVector<MarkdownNodeId> children;
    QVector<MarkdownInlineToken> inlineTokens;

    QString literal;
    QString url;
    QString title;
    QString fenceInfo;
    int headingLevel = 0;
    int listStart = 0;
    bool orderedList = false;
    bool taskList = false;
    bool taskChecked = false;
    bool tableHeader = false;
};

QString markdownNodeTypeName(MarkdownNodeType type);
MarkdownNodeType markdownNodeTypeFromCmark(const AstNode& node);

} // namespace Muffin
