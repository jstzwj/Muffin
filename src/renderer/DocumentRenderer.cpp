#include "DocumentRenderer.h"
#include "MathImageRenderer.h"
#include "SourceBlockData.h"
#include <QTextImageFormat>
#include <QTextTable>
#include <QTextList>
#include <QUrl>
#include <QVector>

namespace Muffin {

DocumentRenderer::DocumentRenderer(const ThemeStylesheet& stylesheet)
    : m_ss(stylesheet) {}

void DocumentRenderer::resetState(const MarkdownDocument& document, const QVector<MathSpan>& mathSpans)
{
    m_source = document.source();
    m_model = &document;
    m_mathSpans = mathSpans;
    m_sourceMapper = SourceCoordinateMapper(m_source);
    m_sourceMap.clear();
    m_blocks.clear();
    m_syntaxTokens.clear();
    m_fragments.clear();
    m_inEditableBlock = false;
    m_nonPlainInlineDepth = 0;
    m_currentEditableSourceSpan = {};
    m_currentSourceSearchOffset = -1;
    m_currentBlockRange = {};
    m_currentBlockKind = RenderSpan::Kind::Unsupported;
}

std::unique_ptr<QTextDocument> DocumentRenderer::createTextDocument() const
{
    auto doc = std::make_unique<QTextDocument>();
    doc->setDefaultFont(m_ss.theme().bodyFont);
    doc->setDefaultStyleSheet("");
    return doc;
}

RenderResult DocumentRenderer::render(const MarkdownDocument& document, const QVector<MathSpan>& mathSpans)
{
    resetState(document, mathSpans);
    auto doc = createTextDocument();

    QTextCursor cursor(doc.get());
    cursor.setBlockFormat(m_ss.bodyBlockFormat());
    cursor.setCharFormat(m_ss.bodyCharFormat());

    if (!document.isEmpty()) {
        renderModelNode(cursor, document.rootId());
        cursor.movePosition(QTextCursor::End);
        if (cursor.block().text().isEmpty() && cursor.block().blockNumber() > 0) {
            cursor.deletePreviousChar();
        }
    }

    return {std::move(doc), m_sourceMap, m_blocks, m_syntaxTokens, m_fragments};
}

MarkdownNodeId DocumentRenderer::renderableNodeIdForPartial(const MarkdownDocument& document, MarkdownNodeId nodeId) const
{
    const MarkdownNode* node = document.nodeById(nodeId);
    while (node) {
        switch (node->type) {
        case MarkdownNodeType::Document:
        case MarkdownNodeType::Heading:
        case MarkdownNodeType::Paragraph:
        case MarkdownNodeType::BlockQuote:
        case MarkdownNodeType::List:
        case MarkdownNodeType::ListItem:
        case MarkdownNodeType::CodeBlock:
        case MarkdownNodeType::ThematicBreak:
        case MarkdownNodeType::HtmlBlock:
        case MarkdownNodeType::Table:
        case MarkdownNodeType::FormulaBlock:
            return node->id;
        default:
            node = document.nodeById(node->parent);
            break;
        }
    }
    return 0;
}

PartialRenderResult DocumentRenderer::renderPartial(const MarkdownDocument& document,
                                                    const QVector<MarkdownNodeId>& nodeIds,
                                                    const QVector<MathSpan>& mathSpans)
{
    resetState(document, mathSpans);
    if (nodeIds.isEmpty() || document.isEmpty()) {
        return {};
    }

    QVector<MarkdownNodeId> renderableNodeIds;
    for (MarkdownNodeId nodeId : nodeIds) {
        const MarkdownNodeId renderableNodeId = renderableNodeIdForPartial(document, nodeId);
        if (renderableNodeId != 0 && !renderableNodeIds.contains(renderableNodeId)) {
            renderableNodeIds.append(renderableNodeId);
        }
    }
    if (renderableNodeIds.isEmpty()) {
        return {};
    }

    auto doc = createTextDocument();
    QTextCursor cursor(doc.get());
    cursor.setBlockFormat(m_ss.bodyBlockFormat());
    cursor.setCharFormat(m_ss.bodyCharFormat());

    for (MarkdownNodeId nodeId : renderableNodeIds) {
        renderModelNode(cursor, nodeId);
    }

    return {std::shared_ptr<QTextDocument>(std::move(doc)), renderableNodeIds, {}, m_sourceMap, m_blocks, m_syntaxTokens, m_fragments};
}

void DocumentRenderer::renderModelNode(QTextCursor& cursor, MarkdownNodeId nodeId)
{
    if (!m_model) {
        return;
    }
    const MarkdownNode* node = m_model->nodeById(nodeId);
    if (!node) {
        return;
    }

    switch (node->type) {
    case MarkdownNodeType::Document: renderModelDocument(cursor, *node); break;
    case MarkdownNodeType::Heading: renderModelHeading(cursor, *node); break;
    case MarkdownNodeType::Paragraph: renderModelParagraph(cursor, *node); break;
    case MarkdownNodeType::BlockQuote: renderModelBlockQuote(cursor, *node); break;
    case MarkdownNodeType::List: renderModelList(cursor, *node); break;
    case MarkdownNodeType::ListItem: renderModelItem(cursor, *node, -1); break;
    case MarkdownNodeType::CodeBlock: renderModelCodeBlock(cursor, *node); break;
    case MarkdownNodeType::ThematicBreak: renderModelThematicBreak(cursor, *node); break;
    case MarkdownNodeType::HtmlBlock: renderModelHtmlBlock(cursor, *node); break;
    case MarkdownNodeType::Text: renderModelText(cursor, *node); break;
    case MarkdownNodeType::SoftBreak: renderSoftBreak(cursor); break;
    case MarkdownNodeType::LineBreak: renderLineBreak(cursor); break;
    case MarkdownNodeType::Emphasis: renderModelEmph(cursor, *node); break;
    case MarkdownNodeType::Strong: renderModelStrong(cursor, *node); break;
    case MarkdownNodeType::InlineCode: renderModelInlineCode(cursor, *node); break;
    case MarkdownNodeType::Link: renderModelLink(cursor, *node); break;
    case MarkdownNodeType::Image: renderModelImage(cursor, *node); break;
    case MarkdownNodeType::Table: renderModelTable(cursor, *node); break;
    case MarkdownNodeType::TableRow:
    case MarkdownNodeType::TableCell:
        break;
    case MarkdownNodeType::FormulaInline: renderModelFormulaInline(cursor, *node); break;
    case MarkdownNodeType::FormulaBlock: renderModelFormulaBlock(cursor, *node); break;
    case MarkdownNodeType::Strikethrough: renderModelStrikethrough(cursor, *node); break;
    case MarkdownNodeType::Unknown: renderModelInlineChildren(cursor, *node); break;
    }
}

void DocumentRenderer::renderModelDocument(QTextCursor& cursor, const MarkdownNode& node)
{
    for (MarkdownNodeId child : node.children) {
        renderModelNode(cursor, child);
    }
}

void DocumentRenderer::renderModelHeading(QTextCursor& cursor, const MarkdownNode& node)
{
    const int level = qBound(1, node.headingLevel, 6);
    insertBlockForNode(cursor, m_ss.headingBlockFormat(level), m_ss.headingCharFormat(level));
    cursor.block().setUserData(new SourceBlockData(node.sourceRange));
    const int renderedStart = cursor.position();
    const SourceSpan editSource = m_sourceMapper.headingContentSpan(node.sourceRange, level);
    beginEditableBlock(node, RenderSpan::Kind::Heading, editSource);
    renderModelInlineChildren(cursor, node);
    endEditableBlock();
    recordSpan(renderedStart, cursor.position(), node.source, node.sourceRange,
               RenderSpan::Kind::Heading, false, true, RenderSpan::EditPolicy::BlockContent, editSource, node.id);
    recordBlock(renderedStart, cursor.position(), node.source, editSource, node.sourceRange, RenderSpan::Kind::Heading, true, node.id);
}

void DocumentRenderer::renderModelParagraph(QTextCursor& cursor, const MarkdownNode& node)
{
    insertBlockForNode(cursor, m_ss.bodyBlockFormat(), m_ss.bodyCharFormat());
    cursor.block().setUserData(new SourceBlockData(node.sourceRange));
    const int renderedStart = cursor.position();
    beginEditableBlock(node, RenderSpan::Kind::Paragraph, node.source);
    renderModelInlineChildren(cursor, node);
    endEditableBlock();
    recordSpan(renderedStart, cursor.position(), node.source, node.sourceRange,
               RenderSpan::Kind::Paragraph, false, true, RenderSpan::EditPolicy::BlockContent, node.source, node.id);
    recordBlock(renderedStart, cursor.position(), node.source, node.source, node.sourceRange, RenderSpan::Kind::Paragraph, true, node.id);
}

void DocumentRenderer::renderModelFormulaBlock(QTextCursor& cursor, const MarkdownNode& node)
{
    QTextBlockFormat blockFormat = m_ss.bodyBlockFormat();
    blockFormat.setAlignment(Qt::AlignHCenter);
    blockFormat.setTopMargin(12);
    blockFormat.setBottomMargin(12);
    insertBlockForNode(cursor, blockFormat, m_ss.bodyCharFormat());
    cursor.block().setUserData(new SourceBlockData(node.sourceRange));

    const MathSpan span{node.source, node.content, node.literal, true};
    MathImageRenderer renderer(m_ss.theme());
    const QImage image = renderer.render(span);
    const QString resourceName = QStringLiteral("muffin-math:%1:%2").arg(span.source.start).arg(span.source.end);
    cursor.document()->addResource(QTextDocument::ImageResource, QUrl(resourceName), image);

    QTextImageFormat imageFormat;
    imageFormat.setName(resourceName);
    imageFormat.setWidth(image.width());
    imageFormat.setHeight(image.height());

    const int renderedStart = cursor.position();
    cursor.insertImage(imageFormat);
    recordSpan(renderedStart, cursor.position(), span.source, node.sourceRange,
               RenderSpan::Kind::FormulaBlock, false, true, RenderSpan::EditPolicy::Atomic, span.source, node.id);
    recordBlock(renderedStart, cursor.position(), span.source, span.source, node.sourceRange, RenderSpan::Kind::FormulaBlock, false, node.id);
}

void DocumentRenderer::renderModelBlockQuote(QTextCursor& cursor, const MarkdownNode& node)
{
    QTextCharFormat charFmt = m_ss.bodyCharFormat();
    charFmt.merge(m_ss.blockquoteCharFormat());
    const bool recordQuoteBlock = node.children.size() == 1;
    for (MarkdownNodeId childId : node.children) {
        const MarkdownNode* child = m_model ? m_model->nodeById(childId) : nullptr;
        if (child && child->type == MarkdownNodeType::Paragraph) {
            insertBlockForNode(cursor, m_ss.blockquoteBlockFormat(), charFmt);
            cursor.block().setUserData(new SourceBlockData(child->sourceRange));
            const int renderedStart = cursor.position();
            const RenderSpan::Kind kind = recordQuoteBlock ? RenderSpan::Kind::BlockQuote : RenderSpan::Kind::Paragraph;
            const MarkdownNodeId nodeId = recordQuoteBlock ? node.id : child->id;
            beginEditableBlock(*child, kind, child->source);
            renderModelInlineChildren(cursor, *child);
            endEditableBlock();
            recordSpan(renderedStart, cursor.position(), child->source, child->sourceRange,
                       kind, false, true, RenderSpan::EditPolicy::BlockContent, child->source, nodeId);
            const SourceSpan blockSource = recordQuoteBlock ? node.source : child->source;
            const SourceRange blockSourceRange = recordQuoteBlock ? node.sourceRange : child->sourceRange;
            recordBlock(renderedStart, cursor.position(), blockSource, child->source, blockSourceRange, kind, true, nodeId);
        } else {
            renderModelNode(cursor, childId);
        }
    }
}

void DocumentRenderer::renderModelCodeBlock(QTextCursor& cursor, const MarkdownNode& node)
{
    insertBlockForNode(cursor, m_ss.bodyBlockFormat(), m_ss.bodyCharFormat());
    cursor.block().setUserData(new SourceBlockData(node.sourceRange));
    const int replacementRenderedStart = cursor.block().position();

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

    QString text = node.literal;
    if (text.endsWith('\n')) text.chop(1);
    const int renderedStart = codeCursor.position();
    codeCursor.insertText(text, m_ss.codeBlockCharFormat());
    const SourceSpan editSource = node.content.isValid() ? node.content : node.source;
    recordSpan(renderedStart, codeCursor.position(), node.source, node.sourceRange, RenderSpan::Kind::CodeBlock, true, true,
               RenderSpan::EditPolicy::LinearText, editSource, node.id);

    cursor = frame->lastCursorPosition();
    cursor.movePosition(QTextCursor::End);
    recordBlock(renderedStart, codeCursor.position(), node.source, editSource, node.sourceRange, RenderSpan::Kind::CodeBlock, true, node.id,
                replacementRenderedStart, cursor.position());
}

void DocumentRenderer::renderModelList(QTextCursor& cursor, const MarkdownNode& node)
{
    int index = 0;
    const int start = node.listStart == 0 ? 1 : node.listStart;
    for (MarkdownNodeId childId : node.children) {
        const MarkdownNode* child = m_model ? m_model->nodeById(childId) : nullptr;
        if (child && child->type == MarkdownNodeType::ListItem) {
            renderModelItem(cursor, *child, node.orderedList ? start + index : -1);
            ++index;
        }
    }
}

void DocumentRenderer::renderModelItem(QTextCursor& cursor, const MarkdownNode& node, int number)
{
    QString prefix = number < 0 ? QString::fromUtf8("\xe2\x80\xa2") + " " : QString::number(number) + ". ";

    QTextBlockFormat blockFmt = m_ss.bodyBlockFormat();
    blockFmt.setLeftMargin(blockFmt.leftMargin() + 20);
    insertBlockForNode(cursor, blockFmt, m_ss.bodyCharFormat());
    cursor.block().setUserData(new SourceBlockData(node.sourceRange));
    cursor.insertText(prefix, m_ss.bodyCharFormat());

    if (node.taskList) {
        cursor.insertText(node.taskChecked ? QString::fromUtf8("\xe2\x98\x91 ") : QString::fromUtf8("\xe2\x97\xbb "),
                          m_ss.bodyCharFormat());
    }

    const int renderedStart = cursor.position();
    const int markerEnd = qMin(node.source.end, node.source.start + prefix.size() + (node.taskList ? 4 : 0));
    const SourceSpan content{markerEnd, node.source.end};
    const bool recordChildParagraphBlocks = node.children.size() > 1;
    bool firstParagraph = true;
    for (MarkdownNodeId childId : node.children) {
        const MarkdownNode* child = m_model ? m_model->nodeById(childId) : nullptr;
        if (child && child->type == MarkdownNodeType::Paragraph) {
            if (!firstParagraph) {
                insertBlockForNode(cursor, blockFmt, m_ss.bodyCharFormat());
                cursor.block().setUserData(new SourceBlockData(child->sourceRange));
            }
            const int paragraphRenderedStart = cursor.position();
            beginEditableBlock(*child, RenderSpan::Kind::Paragraph, child->source);
            renderModelInlineChildren(cursor, *child);
            endEditableBlock();
            if (recordChildParagraphBlocks) {
                recordSpan(paragraphRenderedStart, cursor.position(), child->source, child->sourceRange,
                           RenderSpan::Kind::Paragraph, false, true, RenderSpan::EditPolicy::BlockContent, child->source, child->id);
                recordBlock(paragraphRenderedStart, cursor.position(), child->source, child->source, child->sourceRange,
                            RenderSpan::Kind::Paragraph, true, child->id);
            }
            firstParagraph = false;
        } else {
            renderModelNode(cursor, childId);
        }
    }
    recordSpan(renderedStart, cursor.position(), node.source, node.sourceRange,
               RenderSpan::Kind::List, false, true, RenderSpan::EditPolicy::BlockContent, content, node.id);
    recordBlock(renderedStart, cursor.position(), node.source, content, node.sourceRange, RenderSpan::Kind::List, true, node.id);
}

void DocumentRenderer::renderModelThematicBreak(QTextCursor& cursor, const MarkdownNode& node)
{
    QTextBlockFormat fmt = m_ss.thematicBreakFormat();
    insertBlockForNode(cursor, fmt, m_ss.bodyCharFormat());
    cursor.block().setUserData(new SourceBlockData(node.sourceRange));

    QTextFrameFormat lineFormat;
    lineFormat.setHeight(1);
    lineFormat.setBorder(0);
    lineFormat.setBackground(m_ss.theme().hrColor);
    cursor.insertFrame(lineFormat);
    cursor.movePosition(QTextCursor::End);
}

void DocumentRenderer::renderModelHtmlBlock(QTextCursor& cursor, const MarkdownNode& node)
{
    insertBlockForNode(cursor, m_ss.bodyBlockFormat(), m_ss.bodyCharFormat());
    cursor.block().setUserData(new SourceBlockData(node.sourceRange));
    const int renderedStart = cursor.position();
    cursor.insertText(node.literal);
    recordSpan(renderedStart, cursor.position(), node.source, node.sourceRange, RenderSpan::Kind::HtmlBlock, false, false,
               RenderSpan::EditPolicy::None, {}, node.id);
    recordBlock(renderedStart, cursor.position(), node.source, node.source, node.sourceRange, RenderSpan::Kind::HtmlBlock, false, node.id);
}

void DocumentRenderer::renderModelInlineChildren(QTextCursor& cursor, const MarkdownNode& node)
{
    for (MarkdownNodeId child : node.children) {
        renderModelNode(cursor, child);
    }
}

void DocumentRenderer::renderModelText(QTextCursor& cursor, const MarkdownNode& node)
{
    const QString literal = node.literal;
    SourceSpan source = node.source;
    if (!source.isValid() && !literal.isEmpty()) {
        source = nextInlineSourceSpan(literal);
    }

    renderModelTextChunk(cursor, literal, source, node.sourceRange, node.id);
}

void DocumentRenderer::renderModelTextChunk(QTextCursor& cursor, const QString& text, SourceSpan source,
                                            SourceRange sourceRange, MarkdownNodeId nodeId)
{
    if (text.isEmpty()) {
        return;
    }

    const int renderedStart = cursor.position();
    cursor.insertText(text);
    const bool linear = source.isValid() && m_source.mid(source.start, source.end - source.start) == text;
    const bool editable = m_inEditableBlock && m_nonPlainInlineDepth == 0 && linear;
    recordSpan(renderedStart, cursor.position(), source, sourceRange, RenderSpan::Kind::Text, editable, false,
               linear ? RenderSpan::EditPolicy::LinearText : RenderSpan::EditPolicy::NonLinearText, source, nodeId);
}

void DocumentRenderer::renderModelEmph(QTextCursor& cursor, const MarkdownNode& node)
{
    QTextCharFormat savedFmt = cursor.charFormat();
    cursor.mergeCharFormat(m_ss.italicCharFormat());
    recordSyntaxMarkers(node.source, 1, SyntaxTokenSpan::Kind::EmphasisMarker, node.id);
    const int renderedStart = cursor.position();
    renderModelInlineChildren(cursor, node);
    recordSpan(renderedStart, cursor.position(), node.source, node.sourceRange, RenderSpan::Kind::Emphasis, false, false,
               RenderSpan::EditPolicy::None, {}, node.id);
    cursor.setCharFormat(savedFmt);
}

void DocumentRenderer::renderModelStrong(QTextCursor& cursor, const MarkdownNode& node)
{
    QTextCharFormat savedFmt = cursor.charFormat();
    cursor.mergeCharFormat(m_ss.boldCharFormat());
    recordSyntaxMarkers(node.source, 2, SyntaxTokenSpan::Kind::StrongMarker, node.id);
    const int renderedStart = cursor.position();
    renderModelInlineChildren(cursor, node);
    recordSpan(renderedStart, cursor.position(), node.source, node.sourceRange, RenderSpan::Kind::Strong, false, false,
               RenderSpan::EditPolicy::None, {}, node.id);
    cursor.setCharFormat(savedFmt);
}

void DocumentRenderer::renderModelInlineCode(QTextCursor& cursor, const MarkdownNode& node)
{
    QTextCharFormat fmt = cursor.charFormat();
    fmt.merge(m_ss.inlineCodeCharFormat());
    const SourceSpan content = node.source.isValid() && node.source.end - node.source.start >= 2
        ? SourceSpan{node.source.start + 1, node.source.end - 1}
        : SourceSpan{};
    recordSyntaxMarkers(node.source, 1, SyntaxTokenSpan::Kind::InlineCodeMarker, node.id);
    const int renderedStart = cursor.position();
    cursor.insertText(node.literal, fmt);
    const bool editable = m_inEditableBlock && content.isValid();
    recordSpan(renderedStart, cursor.position(), content, node.sourceRange, RenderSpan::Kind::InlineCode, editable, false,
               RenderSpan::EditPolicy::LinearText, content, node.id);
}

void DocumentRenderer::renderModelFormulaInline(QTextCursor& cursor, const MarkdownNode& node)
{
    renderInlineFormula(cursor, {node.source, node.content, node.literal, false}, node.sourceRange, node.id);
}

void DocumentRenderer::renderModelLink(QTextCursor& cursor, const MarkdownNode& node)
{
    QTextCharFormat linkFmt = cursor.charFormat();
    linkFmt.merge(m_ss.linkCharFormat(node.url));
    linkFmt.setAnchor(true);
    linkFmt.setAnchorHref(node.url);

    QTextCharFormat savedFmt = cursor.charFormat();
    cursor.setCharFormat(linkFmt);
    const int renderedStart = cursor.position();
    renderModelInlineChildren(cursor, node);
    recordSpan(renderedStart, cursor.position(), node.source, node.sourceRange, RenderSpan::Kind::Link, false, false,
               RenderSpan::EditPolicy::None, {}, node.id);
    cursor.setCharFormat(savedFmt);
}

void DocumentRenderer::renderModelImage(QTextCursor& cursor, const MarkdownNode& node)
{
    QString alt;
    if (!node.children.isEmpty() && m_model) {
        if (const MarkdownNode* altNode = m_model->nodeById(node.children.first())) {
            alt = altNode->literal;
        }
    }

    const int renderedStart = cursor.position();
    if (!node.url.isEmpty()) {
        QTextImageFormat imgFmt;
        imgFmt.setName(node.url);
        imgFmt.setProperty(QTextFormat::UserProperty, alt);
        cursor.insertImage(imgFmt);
    } else {
        cursor.insertText(alt.isEmpty() ? QString("[image]") : QString("[%1]").arg(alt));
    }
    recordSpan(renderedStart, cursor.position(), node.source, node.sourceRange, RenderSpan::Kind::Image, false, false,
               RenderSpan::EditPolicy::None, {}, node.id);
}

void DocumentRenderer::renderModelStrikethrough(QTextCursor& cursor, const MarkdownNode& node)
{
    QTextCharFormat savedFmt = cursor.charFormat();
    cursor.mergeCharFormat(m_ss.strikethroughCharFormat());
    ++m_nonPlainInlineDepth;
    const int renderedStart = cursor.position();
    renderModelInlineChildren(cursor, node);
    recordSpan(renderedStart, cursor.position(), node.source, node.sourceRange, RenderSpan::Kind::Unsupported, false, false,
               RenderSpan::EditPolicy::None, {}, node.id);
    --m_nonPlainInlineDepth;
    cursor.setCharFormat(savedFmt);
}

void DocumentRenderer::renderModelTable(QTextCursor& cursor, const MarkdownNode& node)
{
    int rows = 0;
    int cols = 0;
    for (MarkdownNodeId rowId : node.children) {
        const MarkdownNode* row = m_model ? m_model->nodeById(rowId) : nullptr;
        if (!row || row->type != MarkdownNodeType::TableRow) {
            continue;
        }
        ++rows;
        cols = qMax(cols, row->children.size());
    }
    if (rows == 0 || cols <= 0) {
        return;
    }

    insertBlockForNode(cursor, m_ss.bodyBlockFormat(), m_ss.bodyCharFormat());
    cursor.block().setUserData(new SourceBlockData(node.sourceRange));

    QTextTableFormat tableFormat = m_ss.tableFormat();
    tableFormat.setColumnWidthConstraints(tableColumnConstraints(node, cols));

    const int renderedStart = cursor.position();
    QTextTable* table = cursor.insertTable(rows, cols, tableFormat);

    int rowIndex = 0;
    for (MarkdownNodeId rowId : node.children) {
        const MarkdownNode* row = m_model ? m_model->nodeById(rowId) : nullptr;
        if (!row || row->type != MarkdownNodeType::TableRow) {
            continue;
        }
        renderModelTableRow(table, rowIndex, *row, row->tableHeader);
        ++rowIndex;
    }

    cursor.movePosition(QTextCursor::End);
    recordSpan(renderedStart, cursor.position(), node.source, node.sourceRange, RenderSpan::Kind::Table, false, true,
               RenderSpan::EditPolicy::None, {}, node.id);
    recordBlock(renderedStart, cursor.position(), node.source, node.source, node.sourceRange, RenderSpan::Kind::Table, false, node.id);
}

void DocumentRenderer::renderModelTableRow(QTextTable* table, int row, const MarkdownNode& rowNode, bool isHeader)
{
    int col = 0;
    for (MarkdownNodeId cellId : rowNode.children) {
        const MarkdownNode* cell = m_model ? m_model->nodeById(cellId) : nullptr;
        if (cell && cell->type == MarkdownNodeType::TableCell && col < table->columns()) {
            renderModelTableCell(table, row, col, *cell, isHeader);
            ++col;
        }
    }
}

void DocumentRenderer::renderModelTableCell(QTextTable* table, int row, int col, const MarkdownNode& cellNode, bool isHeader)
{
    QTextTableCell cell = table->cellAt(row, col);
    cell.setFormat(m_ss.tableCellFormat(isHeader));

    QTextCursor cellCursor = cell.firstCursorPosition();
    cellCursor.setBlockFormat(m_ss.tableCellBlockFormat(isHeader));
    cellCursor.block().setUserData(new SourceBlockData(cellNode.sourceRange));

    QTextCharFormat fmt = m_ss.bodyCharFormat();
    if (isHeader) fmt.merge(m_ss.tableHeaderCharFormat());
    cellCursor.setCharFormat(fmt);

    QTextCharFormat savedFmt = cellCursor.charFormat();
    cellCursor.setCharFormat(fmt);
    renderModelInlineChildren(cellCursor, cellNode);
    cellCursor.setCharFormat(savedFmt);
}

QVector<QTextLength> DocumentRenderer::tableColumnConstraints(const MarkdownNode& node, int cols) const
{
    QVector<int> weights(cols, 4);
    for (MarkdownNodeId rowId : node.children) {
        const MarkdownNode* row = m_model ? m_model->nodeById(rowId) : nullptr;
        if (!row) {
            continue;
        }
        int col = 0;
        for (MarkdownNodeId cellId : row->children) {
            const MarkdownNode* cell = m_model ? m_model->nodeById(cellId) : nullptr;
            if (cell && col < cols) {
                weights[col] = qMax(weights[col], tableCellTextLength(*cell));
                ++col;
            }
        }
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

int DocumentRenderer::tableCellTextLength(const MarkdownNode& cellNode) const
{
    int length = cellNode.literal.trimmed().size();
    for (MarkdownNodeId childId : cellNode.children) {
        const MarkdownNode* child = m_model ? m_model->nodeById(childId) : nullptr;
        if (child) {
            length += tableCellTextLength(*child);
        }
    }
    return qMax(length, 1);
}

void DocumentRenderer::insertBlockForNode(QTextCursor& cursor, const QTextBlockFormat& blockFormat, const QTextCharFormat& charFormat)
{
    if (cursor.position() == 0 && cursor.document()->isEmpty()) {
        cursor.setBlockFormat(blockFormat);
        cursor.setCharFormat(charFormat);
        return;
    }
    cursor.insertBlock(blockFormat, charFormat);
}

void DocumentRenderer::renderInlineFormula(QTextCursor& cursor, const MathSpan& span, SourceRange sourceRange, MarkdownNodeId nodeId)
{
    MathImageRenderer renderer(m_ss.theme());
    const QImage image = renderer.render(span);
    const QString resourceName = QStringLiteral("muffin-math:%1:%2").arg(span.source.start).arg(span.source.end);
    cursor.document()->addResource(QTextDocument::ImageResource, QUrl(resourceName), image);

    QTextImageFormat imageFormat;
    imageFormat.setName(resourceName);
    imageFormat.setWidth(image.width());
    imageFormat.setHeight(image.height());

    const int renderedStart = cursor.position();
    cursor.insertImage(imageFormat);
    recordSpan(renderedStart, cursor.position(), span.source, sourceRange,
               RenderSpan::Kind::FormulaInline, m_inEditableBlock, false, RenderSpan::EditPolicy::Atomic, span.source, nodeId);
}

void DocumentRenderer::renderSoftBreak(QTextCursor& cursor) {
    cursor.insertText(" ");
}

void DocumentRenderer::renderLineBreak(QTextCursor& cursor) {
    cursor.insertText("\n");
}

void DocumentRenderer::beginEditableBlock(const MarkdownNode& node, RenderSpan::Kind kind, SourceSpan sourceSpan)
{
    m_inEditableBlock = sourceSpan.isValid();
    m_currentEditableSourceSpan = sourceSpan;
    m_currentSourceSearchOffset = sourceSpan.start;
    m_currentBlockRange = node.sourceRange;
    m_currentBlockKind = kind;
}

void DocumentRenderer::endEditableBlock()
{
    m_inEditableBlock = false;
    m_currentEditableSourceSpan = {};
    m_currentSourceSearchOffset = -1;
    m_currentBlockRange = {};
    m_currentBlockKind = RenderSpan::Kind::Unsupported;
}

SourceSpan DocumentRenderer::nextInlineSourceSpan(const QString& literal)
{
    if (!m_currentEditableSourceSpan.isValid() || literal.isEmpty()) {
        return {};
    }

    const int searchStart = qBound(m_currentEditableSourceSpan.start, m_currentSourceSearchOffset, m_currentEditableSourceSpan.end);
    const int relative = m_source.indexOf(literal, searchStart);
    const int literalSize = static_cast<int>(literal.size());
    if (relative < 0 || relative + literalSize > m_currentEditableSourceSpan.end) {
        return {};
    }

    m_currentSourceSearchOffset = relative + literalSize;
    return {relative, relative + literalSize};
}

void DocumentRenderer::recordSyntaxMarkers(SourceSpan source, int markerLength, SyntaxTokenSpan::Kind kind, MarkdownNodeId nodeId)
{
    if (!source.isValid() || markerLength <= 0 || source.end - source.start < markerLength * 2) {
        return;
    }

    const SourceSpan opening{source.start, source.start + markerLength};
    const SourceSpan closing{source.end - markerLength, source.end};
    m_syntaxTokens.append({opening, kind, nodeId});
    m_syntaxTokens.append({closing, kind, nodeId});
    m_fragments.append({nodeId, opening, {}, RenderFragment::Kind::Marker, kind, false, false});
    m_fragments.append({nodeId, closing, {}, RenderFragment::Kind::Marker, kind, false, false});
}

void DocumentRenderer::recordSpan(int renderedStart, int renderedEnd, SourceSpan source, SourceRange sourceRange,
                                  RenderSpan::Kind kind, bool editable, bool block,
                                  RenderSpan::EditPolicy editPolicy, SourceSpan editSource, MarkdownNodeId nodeId)
{
    if (!source.isValid() || renderedEnd < renderedStart) {
        return;
    }

    m_sourceMap.addSpan({renderedStart, renderedEnd, source, sourceRange, kind, editable, block, editSource, editPolicy, nodeId});
}

void DocumentRenderer::recordBlock(int renderedStart, int renderedEnd, SourceSpan source, SourceSpan content,
                                   SourceRange sourceRange, RenderSpan::Kind kind, bool editable, MarkdownNodeId nodeId,
                                   int replacementRenderedStart, int replacementRenderedEnd)
{
    if (!source.isValid() || renderedEnd < renderedStart) {
        return;
    }
    if (!content.isValid()) {
        content = source;
    }

    if (replacementRenderedStart < 0 || replacementRenderedEnd < replacementRenderedStart) {
        replacementRenderedStart = renderedStart;
        replacementRenderedEnd = renderedEnd;
    }

    m_blocks.append({source, content, sourceRange, renderedStart, renderedEnd, kind, editable, nodeId,
                     replacementRenderedStart, replacementRenderedEnd});
}

} // namespace Muffin
