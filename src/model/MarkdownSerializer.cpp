#include "MarkdownSerializer.h"

#include <functional>
#include <QStringList>

namespace Muffin {

namespace {

struct SerializationWriter {
    QString markdown;
    QHash<MarkdownNodeId, MarkdownSerializedNodeSpan> nodeSpans;

    int position() const { return markdown.size(); }

    void append(const QString& text)
    {
        markdown += text;
    }

    void append(QChar ch)
    {
        markdown += ch;
    }

    void recordNode(MarkdownNodeId nodeId, SourceSpan source, SourceSpan content = {})
    {
        if (!nodeId || !source.isValid()) {
            return;
        }
        nodeSpans.insert(nodeId, MarkdownSerializedNodeSpan{source, content.isValid() ? content : source});
    }
};

QString ensureTrailingNewline(QString text)
{
    return text.endsWith('\n') ? text : text + QChar('\n');
}

QString codeBlockLiteralForSerialization(const QString& literal)
{
    return literal.isEmpty() ? QString() : ensureTrailingNewline(literal);
}

QString trimTrailingNewline(QString text)
{
    while (text.endsWith('\n')) {
        text.chop(1);
    }
    return text;
}

QString indentLines(const QString& text, int spaces)
{
    const QString indent(spaces, QChar(' '));
    QStringList lines = text.split(QChar('\n'));
    for (QString& line : lines) {
        line.prepend(indent);
    }
    return lines.join(QChar('\n'));
}

} // namespace

std::optional<int> MarkdownSerializationResult::sourceOffsetForNodeOffset(MarkdownNodeId nodeId, int offsetInNode) const
{
    const auto it = nodeSpans.constFind(nodeId);
    if (it == nodeSpans.constEnd() || !it->content.isValid()) {
        return std::nullopt;
    }

    const int clampedOffset = qBound(0, offsetInNode, it->content.end - it->content.start);
    return it->content.start + clampedOffset;
}

QString MarkdownSerializer::serializeDocument(const MarkdownDocument& document) const
{
    return serializeDocumentWithSourceMap(document).markdown;
}

MarkdownSerializationResult MarkdownSerializer::serializeDocumentWithSourceMap(const MarkdownDocument& document) const
{
    const MarkdownNode* root = document.nodeById(document.rootId());
    if (!root) {
        return {};
    }

    SerializationWriter writer;
    std::function<void(const MarkdownNode&)> writeBlock;
    std::function<void(const MarkdownNode&)> writeBlockQuote;
    std::function<void(const QString&, const MarkdownNode&)> writeIndentedBlock;
    std::function<void(const MarkdownNode&)> writeInline;
    std::function<void(const MarkdownNode&)> writeInlineChildren;
    std::function<void(const MarkdownNode&)> writeList;
    std::function<void(const MarkdownNode&, const QString&)> writeListWithIndent;
    std::function<void(const MarkdownNode&, int, const QString&)> writeListItem;

    writeInlineChildren = [&](const MarkdownNode& node) {
        for (MarkdownNodeId childId : node.children) {
            const MarkdownNode* child = document.nodeById(childId);
            if (child) {
                writeInline(*child);
            }
        }
    };

    writeInline = [&](const MarkdownNode& node) {
        const int start = writer.position();
        int contentStart = start;
        int contentEnd = start;

        switch (node.type) {
        case MarkdownNodeType::Text:
            writer.append(node.literal);
            contentEnd = writer.position();
            break;
        case MarkdownNodeType::SoftBreak:
            writer.append(QChar('\n'));
            contentEnd = writer.position();
            break;
        case MarkdownNodeType::LineBreak:
            writer.append(QStringLiteral("  \n"));
            contentEnd = writer.position();
            break;
        case MarkdownNodeType::Emphasis:
            writer.append(QChar('*'));
            contentStart = writer.position();
            writeInlineChildren(node);
            contentEnd = writer.position();
            writer.append(QChar('*'));
            break;
        case MarkdownNodeType::Strong:
            writer.append(QStringLiteral("**"));
            contentStart = writer.position();
            writeInlineChildren(node);
            contentEnd = writer.position();
            writer.append(QStringLiteral("**"));
            break;
        case MarkdownNodeType::InlineCode:
            writer.append(QChar('`'));
            contentStart = writer.position();
            writer.append(node.literal);
            contentEnd = writer.position();
            writer.append(QChar('`'));
            break;
        case MarkdownNodeType::Link:
            writer.append(QChar('['));
            contentStart = writer.position();
            writeInlineChildren(node);
            contentEnd = writer.position();
            writer.append(QStringLiteral("]("));
            writer.append(node.url);
            writer.append(QChar(')'));
            break;
        case MarkdownNodeType::Image:
            writer.append(QStringLiteral("!["));
            contentStart = writer.position();
            writeInlineChildren(node);
            contentEnd = writer.position();
            writer.append(QStringLiteral("]("));
            writer.append(node.url);
            writer.append(QChar(')'));
            break;
        case MarkdownNodeType::FormulaInline: {
            const QString literal = node.literal.startsWith('$') && node.literal.endsWith('$')
                ? node.literal
                : QChar('$') + node.literal + QChar('$');
            writer.append(literal);
            contentStart = start + 1;
            contentEnd = writer.position();
            if (contentEnd > contentStart) {
                --contentEnd;
            }
            break;
        }
        case MarkdownNodeType::Strikethrough:
            writer.append(QStringLiteral("~~"));
            contentStart = writer.position();
            writeInlineChildren(node);
            contentEnd = writer.position();
            writer.append(QStringLiteral("~~"));
            break;
        default:
            writeInlineChildren(node);
            contentEnd = writer.position();
            break;
        }

        writer.recordNode(node.id, {start, writer.position()}, {contentStart, contentEnd});
    };

    writeBlock = [&](const MarkdownNode& node) {
        const int start = writer.position();
        int contentStart = start;
        int contentEnd = start;

        switch (node.type) {
        case MarkdownNodeType::Heading:
            writer.append(QString(node.headingLevel > 0 ? node.headingLevel : 1, QChar('#')));
            writer.append(QChar(' '));
            contentStart = writer.position();
            writeInlineChildren(node);
            contentEnd = writer.position();
            break;
        case MarkdownNodeType::Paragraph:
        case MarkdownNodeType::TableCell:
            writeInlineChildren(node);
            contentEnd = writer.position();
            break;
        case MarkdownNodeType::BlockQuote:
            writeBlockQuote(node);
            contentEnd = writer.position();
            break;
        case MarkdownNodeType::List:
            writeList(node);
            contentEnd = writer.position();
            break;
        case MarkdownNodeType::ListItem:
            writeListItem(node, -1, {});
            contentEnd = writer.position();
            break;
        case MarkdownNodeType::CodeBlock: {
            const QString fence = QStringLiteral("```");
            const QString info = node.fenceInfo.isEmpty() ? QString() : node.fenceInfo;
            writer.append(fence);
            writer.append(info);
            writer.append(QChar('\n'));
            contentStart = writer.position();
            writer.append(codeBlockLiteralForSerialization(node.literal));
            contentEnd = writer.position();
            if (contentEnd > contentStart) {
                --contentEnd;
            }
            writer.append(fence);
            break;
        }
        default: {
            const QString block = trimTrailingNewline(serializeBlock(document, node));
            writer.append(block);
            contentEnd = writer.position();
            break;
        }
        }

        writer.recordNode(node.id, {start, writer.position()}, {contentStart, contentEnd});
    };

    writeBlockQuote = [&](const MarkdownNode& node) {
        const int start = writer.position();
        bool wroteBlock = false;
        for (MarkdownNodeId childId : node.children) {
            const MarkdownNode* child = document.nodeById(childId);
            if (!child) {
                continue;
            }
            if (wroteBlock) {
                writer.append(QStringLiteral("\n> \n"));
            }

            const QString blockText = serializeBlock(document, *child);
            if (child->type == MarkdownNodeType::Paragraph && !blockText.contains(QChar('\n'))) {
                writer.append(QStringLiteral("> "));
                const int childStart = writer.position();
                writeInlineChildren(*child);
                writer.recordNode(child->id, {childStart, writer.position()}, {childStart, writer.position()});
            } else if (child->type == MarkdownNodeType::Heading && !blockText.contains(QChar('\n'))) {
                writer.append(QStringLiteral("> "));
                const int childStart = writer.position();
                writer.append(QString(child->headingLevel > 0 ? child->headingLevel : 1, QChar('#')));
                writer.append(QChar(' '));
                const int contentStart = writer.position();
                writeInlineChildren(*child);
                writer.recordNode(child->id, {childStart, writer.position()}, {contentStart, writer.position()});
            } else if (child->type == MarkdownNodeType::List) {
                writeListWithIndent(*child, QStringLiteral("> "));
            } else {
                const QStringList lines = blockText.split(QChar('\n'));
                for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
                    if (lineIndex > 0) {
                        writer.append(QChar('\n'));
                    }
                    writer.append(QStringLiteral("> "));
                    writer.append(lines.at(lineIndex));
                }
                writer.recordNode(child->id, {start, writer.position()});
            }
            wroteBlock = true;
        }
        writer.recordNode(node.id, {start, writer.position()});
    };

    writeIndentedBlock = [&](const QString& indent, const MarkdownNode& node) {
        const int start = writer.position();
        int contentStart = start;

        if (node.type == MarkdownNodeType::List) {
            writeListWithIndent(node, indent);
        } else {
            const QString blockText = serializeBlock(document, node);
            const QStringList lines = blockText.split(QChar('\n'));
            for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
                if (lineIndex > 0) {
                    writer.append(QChar('\n'));
                }
                writer.append(indent);
                if (lineIndex == 0) {
                    contentStart = writer.position();
                }
                writer.append(lines.at(lineIndex));
            }
        }

        writer.recordNode(node.id, {start, writer.position()}, {contentStart, writer.position()});
    };

    writeList = [&](const MarkdownNode& node) {
        writeListWithIndent(node, {});
    };

    writeListWithIndent = [&](const MarkdownNode& node, const QString& itemIndent) {
        const int start = writer.position();
        int contentStart = start;
        int index = node.listStart == 0 ? 1 : node.listStart;
        bool wroteItem = false;
        for (MarkdownNodeId childId : node.children) {
            const MarkdownNode* child = document.nodeById(childId);
            if (!child || child->type != MarkdownNodeType::ListItem) {
                continue;
            }
            if (wroteItem) {
                writer.append(QChar('\n'));
            }
            if (!wroteItem) {
                contentStart = writer.position();
            }
            writeListItem(*child, node.orderedList ? index : -1, itemIndent);
            ++index;
            wroteItem = true;
        }
        writer.recordNode(node.id, {start, writer.position()}, {contentStart, writer.position()});
    };

    writeListItem = [&](const MarkdownNode& node, int number, const QString& itemIndent) {
        const int start = writer.position();
        const QString marker = number >= 0 ? QString::number(number) + QStringLiteral(". ") : QStringLiteral("- ");
        QString itemPrefix = marker;
        if (node.taskList) {
            itemPrefix += node.taskChecked ? QStringLiteral("[x] ") : QStringLiteral("[ ] ");
        }

        writer.append(itemIndent);
        writer.append(itemPrefix);
        const int contentStart = writer.position();
        bool wroteFirstChild = false;
        for (MarkdownNodeId childId : node.children) {
            const MarkdownNode* child = document.nodeById(childId);
            if (!child) {
                continue;
            }

            if (child->type == MarkdownNodeType::Paragraph) {
                if (!wroteFirstChild) {
                    writeInlineChildren(*child);
                    writer.recordNode(child->id, {contentStart, writer.position()}, {contentStart, writer.position()});
                } else {
                    writer.append(QStringLiteral("\n\n"));
                    const int paragraphStart = writer.position();
                    const QString childText = serializeInlineChildren(document, *child);
                    if (!childText.contains(QChar('\n'))) {
                        writer.append(QString(itemPrefix.size(), QChar(' ')));
                        const int paragraphContentStart = writer.position();
                        writeInlineChildren(*child);
                        writer.recordNode(child->id,
                                          {paragraphStart, writer.position()},
                                          {paragraphContentStart, writer.position()});
                    } else {
                        writer.append(indentLines(childText, itemPrefix.size()));
                        writer.recordNode(child->id, {paragraphStart, writer.position()});
                    }
                }
            } else {
                writer.append(QChar('\n'));
                writeIndentedBlock(itemIndent + QString(marker.size(), QChar(' ')), *child);
            }
            wroteFirstChild = true;
        }
        writer.recordNode(node.id, {start, writer.position()}, {contentStart, writer.position()});
    };

    bool wroteBlock = false;
    for (MarkdownNodeId childId : root->children) {
        const MarkdownNode* child = document.nodeById(childId);
        if (!child) {
            continue;
        }
        if (trimTrailingNewline(serializeBlock(document, *child)).isEmpty()) {
            continue;
        }
        if (wroteBlock) {
            writer.append(QStringLiteral("\n\n"));
        }
        const int beforeBlock = writer.position();
        writeBlock(*child);
        wroteBlock = writer.position() > beforeBlock || wroteBlock;
    }
    return MarkdownSerializationResult{writer.markdown, writer.nodeSpans};
}

QString MarkdownSerializer::serializeNode(const MarkdownDocument& document, MarkdownNodeId nodeId) const
{
    const MarkdownNode* node = document.nodeById(nodeId);
    if (!node) {
        return {};
    }
    return node->type == MarkdownNodeType::Document
        ? serializeDocument(document)
        : serializeBlock(document, *node);
}

QString MarkdownSerializer::serializeBlock(const MarkdownDocument& document, const MarkdownNode& node) const
{
    switch (node.type) {
    case MarkdownNodeType::Document:
        return serializeDocument(document);
    case MarkdownNodeType::Heading:
        return QString(node.headingLevel > 0 ? node.headingLevel : 1, QChar('#'))
            + QChar(' ') + serializeInlineChildren(document, node);
    case MarkdownNodeType::Paragraph:
        return serializeInlineChildren(document, node);
    case MarkdownNodeType::BlockQuote:
        return serializeBlockQuote(document, node);
    case MarkdownNodeType::List:
        return serializeList(document, node);
    case MarkdownNodeType::ListItem:
        return serializeListItem(document, node, -1);
    case MarkdownNodeType::CodeBlock: {
        const QString fence = QStringLiteral("```");
        QString info = node.fenceInfo.isEmpty() ? QString() : node.fenceInfo;
        return fence + info + QChar('\n') + codeBlockLiteralForSerialization(node.literal) + fence;
    }
    case MarkdownNodeType::ThematicBreak:
        return QStringLiteral("---");
    case MarkdownNodeType::HtmlBlock:
        return node.literal.isEmpty() ? sourceSlice(document, node) : trimTrailingNewline(node.literal);
    case MarkdownNodeType::Table:
        return serializeTable(document, node);
    case MarkdownNodeType::FormulaBlock:
        return sourceSlice(document, node);
    default:
        if (node.type == MarkdownNodeType::Text || node.type == MarkdownNodeType::Emphasis ||
            node.type == MarkdownNodeType::Strong || node.type == MarkdownNodeType::InlineCode ||
            node.type == MarkdownNodeType::Link || node.type == MarkdownNodeType::Image ||
            node.type == MarkdownNodeType::FormulaInline || node.type == MarkdownNodeType::Strikethrough) {
            return serializeInline(document, node);
        }
        return sourceSlice(document, node);
    }
}

QString MarkdownSerializer::serializeInline(const MarkdownDocument& document, const MarkdownNode& node) const
{
    switch (node.type) {
    case MarkdownNodeType::Text:
        return node.literal;
    case MarkdownNodeType::SoftBreak:
        return QStringLiteral("\n");
    case MarkdownNodeType::LineBreak:
        return QStringLiteral("  \n");
    case MarkdownNodeType::Emphasis:
        return QChar('*') + serializeInlineChildren(document, node) + QChar('*');
    case MarkdownNodeType::Strong:
        return QStringLiteral("**") + serializeInlineChildren(document, node) + QStringLiteral("**");
    case MarkdownNodeType::InlineCode:
        return QChar('`') + node.literal + QChar('`');
    case MarkdownNodeType::Link:
        return QChar('[') + serializeInlineChildren(document, node) + QStringLiteral("](") + node.url + QChar(')');
    case MarkdownNodeType::Image:
        return QStringLiteral("![") + serializeInlineChildren(document, node) + QStringLiteral("](") + node.url + QChar(')');
    case MarkdownNodeType::FormulaInline:
        return node.literal.startsWith('$') && node.literal.endsWith('$')
            ? node.literal
            : QChar('$') + node.literal + QChar('$');
    case MarkdownNodeType::Strikethrough:
        return QStringLiteral("~~") + serializeInlineChildren(document, node) + QStringLiteral("~~");
    default:
        return serializeInlineChildren(document, node);
    }
}

QString MarkdownSerializer::serializeInlineChildren(const MarkdownDocument& document, const MarkdownNode& node) const
{
    QString result;
    for (MarkdownNodeId childId : node.children) {
        const MarkdownNode* child = document.nodeById(childId);
        if (!child) {
            continue;
        }
        result += serializeInline(document, *child);
    }
    return result;
}

QString MarkdownSerializer::serializeList(const MarkdownDocument& document, const MarkdownNode& node) const
{
    QStringList items;
    int index = node.listStart == 0 ? 1 : node.listStart;
    for (MarkdownNodeId childId : node.children) {
        const MarkdownNode* child = document.nodeById(childId);
        if (!child || child->type != MarkdownNodeType::ListItem) {
            continue;
        }
        items.append(serializeListItem(document, *child, node.orderedList ? index : -1));
        ++index;
    }
    return items.join(QChar('\n'));
}

QString MarkdownSerializer::serializeListItem(const MarkdownDocument& document, const MarkdownNode& node, int number) const
{
    const QString marker = number >= 0 ? QString::number(number) + QStringLiteral(". ") : QStringLiteral("- ");
    QString itemPrefix = marker;
    if (node.taskList) {
        itemPrefix += node.taskChecked ? QStringLiteral("[x] ") : QStringLiteral("[ ] ");
    }

    QString result = itemPrefix;
    bool wroteFirstChild = false;
    for (MarkdownNodeId childId : node.children) {
        const MarkdownNode* child = document.nodeById(childId);
        if (!child) {
            continue;
        }

        QString childText = child->type == MarkdownNodeType::Paragraph
            ? serializeInlineChildren(document, *child)
            : serializeBlock(document, *child);
        if (child->type == MarkdownNodeType::Paragraph) {
            if (!wroteFirstChild) {
                result += childText;
            } else {
                result += QStringLiteral("\n\n") + indentLines(childText, itemPrefix.size());
            }
        } else {
            result += QChar('\n') + indentLines(childText, itemPrefix.size());
        }
        wroteFirstChild = true;
    }
    return result;
}

QString MarkdownSerializer::serializeBlockQuote(const MarkdownDocument& document, const MarkdownNode& node) const
{
    QStringList quotedBlocks;
    for (MarkdownNodeId childId : node.children) {
        const MarkdownNode* child = document.nodeById(childId);
        if (!child) {
            continue;
        }
        QStringList quotedLines;
        const QString block = serializeBlock(document, *child);
        for (const QString& line : block.split(QChar('\n'))) {
            quotedLines.append(QStringLiteral("> ") + line);
        }
        quotedBlocks.append(quotedLines.join(QChar('\n')));
    }
    return quotedBlocks.join(QStringLiteral("\n> \n"));
}

QString MarkdownSerializer::serializeTable(const MarkdownDocument& document, const MarkdownNode& node) const
{
    QVector<const MarkdownNode*> rows;
    int cols = 0;
    for (MarkdownNodeId rowId : node.children) {
        const MarkdownNode* row = document.nodeById(rowId);
        if (!row || row->type != MarkdownNodeType::TableRow) {
            continue;
        }
        rows.append(row);
        cols = qMax(cols, row->children.size());
    }
    if (rows.isEmpty() || cols <= 0) {
        return sourceSlice(document, node);
    }

    auto rowText = [&](const MarkdownNode& row) {
        QStringList cells;
        for (int i = 0; i < cols; ++i) {
            QString cellText;
            if (i < row.children.size()) {
                if (const MarkdownNode* cell = document.nodeById(row.children.at(i))) {
                    cellText = serializeTableCell(document, *cell);
                }
            }
            cells.append(QStringLiteral(" ") + cellText + QStringLiteral(" "));
        }
        return QChar('|') + cells.join(QChar('|')) + QChar('|');
    };

    QStringList lines;
    lines.append(rowText(*rows.first()));
    QStringList delimiters;
    for (int i = 0; i < cols; ++i) {
        delimiters.append(QStringLiteral(" --- "));
    }
    lines.append(QChar('|') + delimiters.join(QChar('|')) + QChar('|'));
    for (int i = 1; i < rows.size(); ++i) {
        lines.append(rowText(*rows.at(i)));
    }
    return lines.join(QChar('\n'));
}

QString MarkdownSerializer::serializeTableCell(const MarkdownDocument& document, const MarkdownNode& node) const
{
    return serializeInlineChildren(document, node).replace(QChar('\n'), QChar(' ')).trimmed();
}

QString MarkdownSerializer::sourceSlice(const MarkdownDocument& document, const MarkdownNode& node) const
{
    if (!node.source.isValid() || node.source.start > document.source().size()) {
        return {};
    }
    const int end = qMin(node.source.end, document.source().size());
    return document.source().mid(node.source.start, end - node.source.start);
}

} // namespace Muffin
