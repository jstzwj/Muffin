#include "DocumentRenderer.h"
#include <QTextTable>
#include <QTextList>
#include <QVector>

namespace Muffin {

DocumentRenderer::DocumentRenderer(const ThemeStylesheet& stylesheet)
    : m_ss(stylesheet) {}

std::unique_ptr<QTextDocument> DocumentRenderer::render(const AstTree& tree) {
    auto doc = std::make_unique<QTextDocument>();
    doc->setDefaultFont(m_ss.theme().bodyFont);
    doc->setDefaultStyleSheet("");

    QTextCursor cursor(doc.get());
    cursor.setBlockFormat(m_ss.bodyBlockFormat());
    cursor.setCharFormat(m_ss.bodyCharFormat());

    if (!tree.isNull()) {
        renderNode(cursor, tree.root());
        // Remove trailing empty block
        cursor.movePosition(QTextCursor::End);
        if (cursor.block().text().isEmpty() && cursor.block().blockNumber() > 0) {
            cursor.deletePreviousChar();
        }
    }

    return doc;
}

void DocumentRenderer::renderNode(QTextCursor& cursor, const AstNode& node) {
    if (node.isNull()) return;

    auto type = node.type();

    switch (type) {
    case CMARK_NODE_DOCUMENT:
        renderDocument(cursor, node);
        break;
    case CMARK_NODE_HEADING:
        renderHeading(cursor, node);
        break;
    case CMARK_NODE_PARAGRAPH:
        renderParagraph(cursor, node);
        break;
    case CMARK_NODE_BLOCK_QUOTE:
        renderBlockQuote(cursor, node);
        break;
    case CMARK_NODE_CODE_BLOCK:
        renderCodeBlock(cursor, node);
        break;
    case CMARK_NODE_LIST:
        renderList(cursor, node);
        break;
    case CMARK_NODE_THEMATIC_BREAK:
        renderThematicBreak(cursor);
        break;
    case CMARK_NODE_HTML_BLOCK:
        renderHtmlBlock(cursor, node);
        break;
    case CMARK_NODE_TEXT:
        renderText(cursor, node);
        break;
    case CMARK_NODE_SOFTBREAK:
        renderSoftBreak(cursor);
        break;
    case CMARK_NODE_LINEBREAK:
        renderLineBreak(cursor);
        break;
    case CMARK_NODE_EMPH:
        renderEmph(cursor, node);
        break;
    case CMARK_NODE_STRONG:
        renderStrong(cursor, node);
        break;
    case CMARK_NODE_CODE:
        renderInlineCode(cursor, node);
        break;
    case CMARK_NODE_LINK:
        renderLink(cursor, node);
        break;
    case CMARK_NODE_IMAGE:
        renderImage(cursor, node);
        break;
    default:
        if (type == CMARK_NODE_TABLE) {
            renderTable(cursor, node);
        } else if (type == CMARK_NODE_TABLE_ROW || type == CMARK_NODE_TABLE_CELL) {
            // Handled by renderTable
        } else if (type == CMARK_NODE_STRIKETHROUGH) {
            renderStrikethrough(cursor, node);
        } else {
            renderInlineChildren(cursor, node);
        }
        break;
    }
}

void DocumentRenderer::renderDocument(QTextCursor& cursor, const AstNode& node) {
    AstNode child = node.firstChild();
    while (!child.isNull()) {
        renderNode(cursor, child);
        child = child.next();
    }
}

void DocumentRenderer::renderHeading(QTextCursor& cursor, const AstNode& node) {
    int level = node.headingLevel();

    cursor.insertBlock(m_ss.headingBlockFormat(level), m_ss.headingCharFormat(level));
    renderInlineChildren(cursor, node);
}

void DocumentRenderer::renderParagraph(QTextCursor& cursor, const AstNode& node) {
    cursor.insertBlock(m_ss.bodyBlockFormat(), m_ss.bodyCharFormat());
    renderInlineChildren(cursor, node);
}

void DocumentRenderer::renderBlockQuote(QTextCursor& cursor, const AstNode& node) {
    QTextCharFormat charFmt = m_ss.bodyCharFormat();
    charFmt.merge(m_ss.blockquoteCharFormat());

    AstNode child = node.firstChild();
    while (!child.isNull()) {
        if (child.type() == CMARK_NODE_PARAGRAPH) {
            cursor.insertBlock(m_ss.blockquoteBlockFormat(), charFmt);
            renderInlineChildren(cursor, child);
        } else {
            renderNode(cursor, child);
        }
        child = child.next();
    }
}

void DocumentRenderer::renderCodeBlock(QTextCursor& cursor, const AstNode& node) {
    cursor.insertBlock(m_ss.bodyBlockFormat(), m_ss.bodyCharFormat());

    QTextFrameFormat frameFormat;
    frameFormat.setBackground(m_ss.theme().codeBackground);
    frameFormat.setBorder(0);
    frameFormat.setPadding(12);
    frameFormat.setMargin(0);
    frameFormat.setWidth(QTextLength(QTextLength::PercentageLength, 100));

    QTextFrame* frame = cursor.insertFrame(frameFormat);
    QTextCursor codeCursor = frame->firstCursorPosition();
    codeCursor.setBlockFormat(m_ss.codeBlockFormat());
    codeCursor.setCharFormat(m_ss.codeBlockCharFormat());

    QString text = node.literal();
    if (text.endsWith('\n')) text.chop(1);
    codeCursor.insertText(text, m_ss.codeBlockCharFormat());

    cursor = frame->lastCursorPosition();
    cursor.movePosition(QTextCursor::End);
}

void DocumentRenderer::renderList(QTextCursor& cursor, const AstNode& node) {
    auto listType = node.listType();
    int start = node.listStart();
    if (start == 0) start = 1;

    AstNode child = node.firstChild();
    int index = 0;
    while (!child.isNull()) {
        if (child.type() == CMARK_NODE_ITEM) {
            renderItem(cursor, child, listType == CMARK_ORDERED_LIST ? start + index : -1);
            index++;
        }
        child = child.next();
    }
}

void DocumentRenderer::renderItem(QTextCursor& cursor, const AstNode& node, int number) {
    QString prefix;
    if (number < 0) {
        // Bullet list
        prefix = QString::fromUtf8("\xe2\x80\xa2") + " ";  // bullet character
    } else {
        prefix = QString::number(number) + ". ";
    }

    QTextBlockFormat blockFmt = m_ss.bodyBlockFormat();
    blockFmt.setLeftMargin(blockFmt.leftMargin() + 20);

    cursor.insertBlock(blockFmt, m_ss.bodyCharFormat());
    QTextCharFormat prefixFmt = m_ss.bodyCharFormat();
    cursor.insertText(prefix, prefixFmt);

    // Task list checkbox
    bool isTasklist = (strcmp(node.typeString(), "tasklist") == 0);
    if (isTasklist) {
        if (node.isTasklistChecked()) {
            cursor.insertText(QString::fromUtf8("\xe2\x98\x91 "), prefixFmt);  // ☑
        } else {
            cursor.insertText(QString::fromUtf8("\xe2\x97\xbb "), prefixFmt);  // ◻
        }
    }

    AstNode child = node.firstChild();
    while (!child.isNull()) {
        if (child.type() == CMARK_NODE_PARAGRAPH) {
            renderInlineChildren(cursor, child);
        } else {
            renderNode(cursor, child);
        }
        child = child.next();
    }
}

void DocumentRenderer::renderThematicBreak(QTextCursor& cursor) {
    QTextBlockFormat fmt = m_ss.thematicBreakFormat();
    cursor.insertBlock(fmt, m_ss.bodyCharFormat());

    QTextFrameFormat lineFormat;
    lineFormat.setHeight(1);
    lineFormat.setBorder(0);
    lineFormat.setBackground(m_ss.theme().hrColor);
    cursor.insertFrame(lineFormat);
    cursor.movePosition(QTextCursor::End);
}

void DocumentRenderer::renderHtmlBlock(QTextCursor& cursor, const AstNode& node) {
    cursor.insertBlock(m_ss.bodyBlockFormat(), m_ss.bodyCharFormat());
    cursor.insertText(node.literal());
}

void DocumentRenderer::renderInlineChildren(QTextCursor& cursor, const AstNode& node) {
    AstNode child = node.firstChild();
    while (!child.isNull()) {
        renderNode(cursor, child);
        child = child.next();
    }
}

void DocumentRenderer::renderText(QTextCursor& cursor, const AstNode& node) {
    cursor.insertText(node.literal());
}

void DocumentRenderer::renderSoftBreak(QTextCursor& cursor) {
    cursor.insertText(" ");
}

void DocumentRenderer::renderLineBreak(QTextCursor& cursor) {
    cursor.insertText("\n");
}

void DocumentRenderer::renderEmph(QTextCursor& cursor, const AstNode& node) {
    QTextCharFormat savedFmt = cursor.charFormat();
    cursor.mergeCharFormat(m_ss.italicCharFormat());
    renderInlineChildren(cursor, node);
    cursor.setCharFormat(savedFmt);
}

void DocumentRenderer::renderStrong(QTextCursor& cursor, const AstNode& node) {
    QTextCharFormat savedFmt = cursor.charFormat();
    cursor.mergeCharFormat(m_ss.boldCharFormat());
    renderInlineChildren(cursor, node);
    cursor.setCharFormat(savedFmt);
}

void DocumentRenderer::renderInlineCode(QTextCursor& cursor, const AstNode& node) {
    QTextCharFormat fmt = cursor.charFormat();
    fmt.merge(m_ss.inlineCodeCharFormat());
    cursor.insertText(node.literal(), fmt);
}

void DocumentRenderer::renderLink(QTextCursor& cursor, const AstNode& node) {
    QTextCharFormat linkFmt = cursor.charFormat();
    linkFmt.merge(m_ss.linkCharFormat(node.url()));
    linkFmt.setAnchor(true);
    linkFmt.setAnchorHref(node.url());

    // Save and set format, render children, then restore
    QTextCharFormat savedFmt = cursor.charFormat();
    cursor.setCharFormat(linkFmt);
    renderInlineChildren(cursor, node);
    cursor.setCharFormat(savedFmt);
}

void DocumentRenderer::renderImage(QTextCursor& cursor, const AstNode& node) {
    QString url = node.url();
    QString alt = node.firstChild().isNull() ? QString() : node.firstChild().literal();

    if (!url.isEmpty()) {
        QTextImageFormat imgFmt;
        imgFmt.setName(url);
        imgFmt.setProperty(QTextFormat::UserProperty, alt);
        cursor.insertImage(imgFmt);
    } else {
        cursor.insertText(alt.isEmpty() ? QString("[image]") : QString("[%1]").arg(alt));
    }
}

void DocumentRenderer::renderStrikethrough(QTextCursor& cursor, const AstNode& node) {
    QTextCharFormat savedFmt = cursor.charFormat();
    cursor.mergeCharFormat(m_ss.strikethroughCharFormat());
    renderInlineChildren(cursor, node);
    cursor.setCharFormat(savedFmt);
}

void DocumentRenderer::renderTable(QTextCursor& cursor, const AstNode& node) {
    int cols = node.tableColumns();
    if (cols <= 0) return;

    // Count rows
    int rows = 0;
    AstNode rowNode = node.firstChild();
    while (!rowNode.isNull()) {
        rows++;
        rowNode = rowNode.next();
    }
    if (rows == 0) return;

    cursor.insertBlock(m_ss.bodyBlockFormat(), m_ss.bodyCharFormat());

    QTextTableFormat tableFormat = m_ss.tableFormat();
    tableFormat.setColumnWidthConstraints(tableColumnConstraints(node, cols));

    QTextTable* table = cursor.insertTable(rows, cols, tableFormat);

    rowNode = node.firstChild();
    int rowIdx = 0;
    while (!rowNode.isNull()) {
        bool isHeader = rowNode.isTableRowHeader();
        renderTableRow(table, rowIdx, rowNode, isHeader);
        rowIdx++;
        rowNode = rowNode.next();
    }

    // Move cursor after table
    cursor.movePosition(QTextCursor::End);
}

QVector<QTextLength> DocumentRenderer::tableColumnConstraints(const AstNode& node, int cols) const {
    QVector<int> weights(cols, 4);

    AstNode rowNode = node.firstChild();
    while (!rowNode.isNull()) {
        AstNode cellNode = rowNode.firstChild();
        int col = 0;
        while (!cellNode.isNull() && col < cols) {
            weights[col] = qMax(weights[col], tableCellTextLength(cellNode));
            cellNode = cellNode.next();
            col++;
        }
        rowNode = rowNode.next();
    }

    int total = 0;
    for (int weight : weights) {
        total += weight;
    }

    QVector<QTextLength> constraints;
    constraints.reserve(cols);
    const qreal minColumnPercent = qMin<qreal>(18.0, 100.0 / cols);
    qreal assigned = 0;
    for (int i = 0; i < cols; ++i) {
        qreal percent = total > 0 ? (weights[i] * 100.0 / total) : (100.0 / cols);
        percent = qMax(minColumnPercent, percent);
        if (i == cols - 1) {
            percent = qMax<qreal>(minColumnPercent, 100.0 - assigned);
        }
        assigned += percent;
        constraints.append(QTextLength(QTextLength::PercentageLength, percent));
    }
    return constraints;
}

int DocumentRenderer::tableCellTextLength(const AstNode& cellNode) const {
    int length = cellNode.literal().trimmed().size();

    AstNode child = cellNode.firstChild();
    while (!child.isNull()) {
        length += tableCellTextLength(child);
        child = child.next();
    }

    return qMax(length, 1);
}

void DocumentRenderer::renderTableRow(QTextTable* table, int row, const AstNode& rowNode, bool isHeader) {
    AstNode cellNode = rowNode.firstChild();
    int col = 0;
    while (!cellNode.isNull() && col < table->columns()) {
        renderTableCell(table, row, col, cellNode, isHeader);
        col++;
        cellNode = cellNode.next();
    }
}

void DocumentRenderer::renderTableCell(QTextTable* table, int row, int col,
                                        const AstNode& cellNode, bool isHeader) {
    QTextTableCell cell = table->cellAt(row, col);
    cell.setFormat(m_ss.tableCellFormat(isHeader));

    QTextCursor cellCursor = cell.firstCursorPosition();
    cellCursor.setBlockFormat(m_ss.tableCellBlockFormat(isHeader));

    QTextCharFormat fmt = m_ss.bodyCharFormat();
    if (isHeader) fmt.merge(m_ss.tableHeaderCharFormat());
    cellCursor.setCharFormat(fmt);

    QTextCharFormat savedFmt = cellCursor.charFormat();
    cellCursor.setCharFormat(fmt);
    renderInlineChildren(cellCursor, cellNode);
    cellCursor.setCharFormat(savedFmt);
}

} // namespace Muffin
