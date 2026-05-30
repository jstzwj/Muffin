#include "MarkdownTransform.h"

#include <QSet>
#include <utility>

namespace Muffin {

namespace {

QString inlineBoundaryWhitespace(const QString& source, int start, int end)
{
    QString whitespace;
    if (start < 0 || end < start || end > source.size()) {
        return whitespace;
    }

    for (int i = start; i < end; ++i) {
        const QChar ch = source.at(i);
        if (ch == QChar('\n') || ch == QChar('\r')) {
            break;
        }
        if (ch != QChar(' ') && ch != QChar('\t')) {
            whitespace.clear();
            break;
        }
        whitespace.append(ch);
    }
    return whitespace;
}

bool isFormattedInlineType(MarkdownNodeType type)
{
    return type == MarkdownNodeType::Strong || type == MarkdownNodeType::Emphasis || type == MarkdownNodeType::Link;
}

bool isSupportedParagraphParentType(MarkdownNodeType type)
{
    return type == MarkdownNodeType::Document || type == MarkdownNodeType::BlockQuote
        || type == MarkdownNodeType::ListItem;
}

bool isSimpleInlineCode(const MarkdownNode& node)
{
    return node.type == MarkdownNodeType::InlineCode && !node.literal.contains(QChar('`'));
}

void resetGeneratedNode(MarkdownNode& node)
{
    node.children.clear();
    node.inlineTokens.clear();
    node.source = {};
    node.content = {};
    node.sourceRange = {};
}

bool appendTextLiteral(MarkdownNode& previousText, const MarkdownNode& nextText, const QString& boundaryWhitespace)
{
    if (previousText.type != MarkdownNodeType::Text || nextText.type != MarkdownNodeType::Text) {
        return false;
    }
    previousText.literal += boundaryWhitespace + nextText.literal;
    return true;
}

QVector<MarkdownNodeId> collectAffectedNodeIds(const MarkdownDocument& source, const MarkdownDocument& result)
{
    QSet<MarkdownNodeId> sourceIds;
    for (const MarkdownNode& node : source.nodes()) {
        sourceIds.insert(node.id);
    }

    QSet<MarkdownNodeId> affected;
    for (const MarkdownNode& sourceNode : source.nodes()) {
        const MarkdownNode* resultNode = result.nodeById(sourceNode.id);
        if (!resultNode || resultNode->type == MarkdownNodeType::Unknown) {
            affected.insert(sourceNode.id);
            continue;
        }
        if (sourceNode.literal != resultNode->literal
            || sourceNode.parent != resultNode->parent
            || sourceNode.children != resultNode->children
            || sourceNode.type != resultNode->type) {
            affected.insert(sourceNode.id);
            continue;
        }
        if (resultNode->parent != 0) {
            const MarkdownNode* resultParent = result.nodeById(resultNode->parent);
            if (!resultParent || !resultParent->children.contains(resultNode->id)) {
                affected.insert(sourceNode.id);
            }
        }
    }

    for (const MarkdownNode& resultNode : result.nodes()) {
        if (!sourceIds.contains(resultNode.id)) {
            affected.insert(resultNode.id);
        }
    }

    return affected.values().toVector();
}

} // namespace

MarkdownNode* MarkdownTransform::mutableNodeById(MarkdownDocument& document, MarkdownNodeId nodeId)
{
    const auto it = document.m_indexById.constFind(nodeId);
    return it == document.m_indexById.constEnd() ? nullptr : &document.m_nodes[*it];
}

void MarkdownTransform::moveTrailingChildren(MarkdownDocument& document,
                                             MarkdownNode& leftParent,
                                             int retainedLastIndex,
                                             MarkdownNode& rightParent)
{
    const QVector<MarkdownNodeId> movedSiblings = leftParent.children.mid(retainedLastIndex + 1);
    leftParent.children.resize(retainedLastIndex + 1);
    for (MarkdownNodeId childId : movedSiblings) {
        if (MarkdownNode* child = mutableNodeById(document, childId)) {
            child->parent = rightParent.id;
        }
        rightParent.children.append(childId);
    }
}

bool MarkdownTransform::mergeAdjacentInlineNodes(MarkdownDocument& document,
                                                 MarkdownNode& previousInline,
                                                 const MarkdownNode& nextInline,
                                                 const QString& boundaryWhitespace)
{
    if (previousInline.type == MarkdownNodeType::Text && nextInline.type == MarkdownNodeType::Text) {
        previousInline.literal += boundaryWhitespace + nextInline.literal;
        return true;
    }
    return mergeFormattedInlineNodes(document, previousInline, nextInline, boundaryWhitespace)
        || mergeInlineCodeNodes(previousInline, nextInline, boundaryWhitespace);
}

bool MarkdownTransform::mergeFormattedInlineNodes(MarkdownDocument& document,
                                                  MarkdownNode& previousInline,
                                                  const MarkdownNode& nextInline,
                                                  const QString& boundaryWhitespace)
{
    if ((previousInline.type != MarkdownNodeType::Strong && previousInline.type != MarkdownNodeType::Emphasis
            && previousInline.type != MarkdownNodeType::Link)
        || previousInline.type != nextInline.type
        || previousInline.url != nextInline.url || previousInline.title != nextInline.title
        || previousInline.children.size() != 1 || nextInline.children.size() != 1) {
        return false;
    }

    MarkdownNode* previousText = mutableNodeById(document, previousInline.children.first());
    const MarkdownNode* nextText = document.nodeById(nextInline.children.first());
    if (!previousText || !nextText || previousText->type != MarkdownNodeType::Text || nextText->type != MarkdownNodeType::Text) {
        return false;
    }

    previousText->literal += boundaryWhitespace + nextText->literal;
    return true;
}

bool MarkdownTransform::mergeInlineCodeNodes(MarkdownNode& previousInline,
                                             const MarkdownNode& nextInline,
                                             const QString& boundaryWhitespace)
{
    if (previousInline.type != MarkdownNodeType::InlineCode || nextInline.type != MarkdownNodeType::InlineCode
        || previousInline.literal.contains(QChar('`')) || nextInline.literal.contains(QChar('`'))) {
        return false;
    }
    previousInline.literal += boundaryWhitespace + nextInline.literal;
    return true;
}

MarkdownDocument MarkdownTransform::replaceNodeLiteral(const MarkdownDocument& source, MarkdownNodeId id, const QString& literal)
{
    MarkdownDocument document = source;
    const auto it = document.m_indexById.constFind(id);
    if (it == document.m_indexById.constEnd()) {
        return document;
    }

    document.m_nodes[*it].literal = literal;
    return document;
}

DocumentTransformResult MarkdownTransform::replaceNodeLiteralWithResult(const MarkdownDocument& source,
                                                                        MarkdownNodeId id,
                                                                        const QString& literal,
                                                                        int cursorSourceOffset)
{
    MarkdownDocument document = replaceNodeLiteral(source, id, literal);
    MarkdownNodeId anchorNodeId = 0;
    int anchorOffsetInNode = literal.size();
    const MarkdownNode* node = document.nodeById(id);
    if (node && (node->type == MarkdownNodeType::Text
            || node->type == MarkdownNodeType::InlineCode
            || node->type == MarkdownNodeType::CodeBlock)) {
        anchorNodeId = node->id;
        const MarkdownNode* sourceNode = source.nodeById(id);
        const SourceSpan editableSource = sourceNode && sourceNode->content.isValid()
            ? sourceNode->content
            : sourceNode ? sourceNode->source : SourceSpan{};
        if (editableSource.isValid() && cursorSourceOffset >= editableSource.start) {
            anchorOffsetInNode = cursorSourceOffset - editableSource.start;
        }
    }

    return DocumentTransformResult{std::move(document), anchorNodeId, anchorOffsetInNode};
}

MarkdownDocument MarkdownTransform::replaceFormulaNode(const MarkdownDocument& source, MarkdownNodeId id, const QString& replacement)
{
    MarkdownDocument document = source;
    const auto it = document.m_indexById.constFind(id);
    if (it == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& node = document.m_nodes[*it];
    if (node.type != MarkdownNodeType::FormulaInline || !node.source.isValid()) {
        return document;
    }

    node.literal = replacement.startsWith('$') && replacement.endsWith('$') && replacement.size() >= 2
        ? replacement.mid(1, replacement.size() - 2)
        : replacement;
    node.source = {node.source.start, node.source.start + static_cast<int>(replacement.size())};
    node.content = replacement.startsWith('$') && replacement.endsWith('$') && replacement.size() >= 2
        ? SourceSpan{node.source.start + 1, node.source.end - 1}
        : node.source;
    return document;
}

DocumentTransformResult MarkdownTransform::replaceFormulaNodeWithResult(const MarkdownDocument& source,
                                                                        MarkdownNodeId id,
                                                                        const QString& replacement,
                                                                        int cursorSourceOffset)
{
    MarkdownDocument document = replaceFormulaNode(source, id, replacement);
    MarkdownNodeId anchorNodeId = 0;
    int anchorOffsetInNode = replacement.size();
    const MarkdownNode* node = document.nodeById(id);
    if (node && node->type == MarkdownNodeType::FormulaInline) {
        anchorNodeId = node->id;
        const MarkdownNode* sourceNode = source.nodeById(id);
        const SourceSpan editableSource = sourceNode && sourceNode->content.isValid()
            ? sourceNode->content
            : sourceNode ? sourceNode->source : SourceSpan{};
        if (editableSource.isValid() && cursorSourceOffset >= editableSource.start) {
            anchorOffsetInNode = cursorSourceOffset - editableSource.start;
        }
    }

    return DocumentTransformResult{std::move(document), anchorNodeId, anchorOffsetInNode};
}

MarkdownDocument MarkdownTransform::splitTextNodeIntoParagraphs(const MarkdownDocument& source,
                                                                MarkdownNodeId id,
                                                                int splitOffset,
                                                                int splitEndOffset)
{
    MarkdownDocument document = source;
    const auto textIt = document.m_indexById.constFind(id);
    if (textIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& text = document.m_nodes[*textIt];
    if (splitEndOffset < 0) {
        splitEndOffset = splitOffset;
    }
    if (text.type != MarkdownNodeType::Text || splitOffset < 0 || splitEndOffset < splitOffset
        || splitEndOffset > text.literal.size()) {
        return document;
    }

    const auto paragraphIt = document.m_indexById.constFind(text.parent);
    if (paragraphIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& paragraph = document.m_nodes[*paragraphIt];
    if (paragraph.type != MarkdownNodeType::Paragraph || paragraph.children.size() != 1 || paragraph.children.first() != id) {
        return document;
    }

    const auto parentIt = document.m_indexById.constFind(paragraph.parent);
    if (parentIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& parent = document.m_nodes[*parentIt];
    if (!isSupportedParagraphParentType(parent.type)) {
        return document;
    }

    const int paragraphPosition = parent.children.indexOf(paragraph.id);
    if (paragraphPosition < 0) {
        return document;
    }

    const QString leftLiteral = text.literal.left(splitOffset);
    const QString rightLiteral = text.literal.mid(splitEndOffset);
    text.literal = leftLiteral;

    MarkdownNode rightParagraph = paragraph;
    rightParagraph.id = document.m_nextId++;
    rightParagraph.parent = parent.id;
    rightParagraph.children.clear();
    rightParagraph.inlineTokens.clear();

    MarkdownNode rightText = text;
    rightText.id = document.m_nextId++;
    rightText.parent = rightParagraph.id;
    rightText.children.clear();
    rightText.inlineTokens.clear();
    rightText.literal = rightLiteral;

    rightParagraph.children.append(rightText.id);
    parent.children.insert(paragraphPosition + 1, rightParagraph.id);
    document.m_nodes.append(std::move(rightParagraph));
    document.m_nodes.append(std::move(rightText));
    document.rebuildIndex();
    return document;
}

DocumentTransformResult MarkdownTransform::splitTextNodeIntoParagraphsWithResult(const MarkdownDocument& source,
                                                                                 MarkdownNodeId id,
                                                                                 int splitOffset,
                                                                                 int splitEndOffset)
{
    MarkdownDocument result = splitTextNodeIntoParagraphs(source, id, splitOffset, splitEndOffset);
    QVector<MarkdownNodeId> affected = collectAffectedNodeIds(source, result);
    DocumentTransformResult dtr = resultWithAnchor(std::move(result), source, id, MarkdownNodeType::Text);
    dtr.affectedNodeIds = std::move(affected);
    return dtr;
}

MarkdownDocument MarkdownTransform::splitParagraphAtChildBoundary(const MarkdownDocument& source,
                                                                  MarkdownNodeId paragraphId,
                                                                  int childIndex)
{
    MarkdownDocument document = source;
    const auto paragraphIt = document.m_indexById.constFind(paragraphId);
    if (paragraphIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& paragraph = document.m_nodes[*paragraphIt];
    if (paragraph.type != MarkdownNodeType::Paragraph || childIndex <= 0 || childIndex >= paragraph.children.size()) {
        return document;
    }

    const auto parentIt = document.m_indexById.constFind(paragraph.parent);
    if (parentIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& parent = document.m_nodes[*parentIt];
    if (!isSupportedParagraphParentType(parent.type)) {
        return document;
    }

    const int paragraphPosition = parent.children.indexOf(paragraph.id);
    if (paragraphPosition < 0) {
        return document;
    }

    MarkdownNode rightParagraph = paragraph;
    rightParagraph.id = document.m_nextId++;
    rightParagraph.parent = parent.id;
    rightParagraph.children = paragraph.children.mid(childIndex);
    rightParagraph.inlineTokens.clear();
    rightParagraph.source = {};
    rightParagraph.content = {};
    rightParagraph.sourceRange = {};

    paragraph.children.resize(childIndex);
    for (MarkdownNodeId childId : rightParagraph.children) {
        if (const auto childIt = document.m_indexById.constFind(childId); childIt != document.m_indexById.constEnd()) {
            document.m_nodes[*childIt].parent = rightParagraph.id;
        }
    }

    parent.children.insert(paragraphPosition + 1, rightParagraph.id);
    document.m_nodes.append(std::move(rightParagraph));
    document.rebuildIndex();
    return document;
}

DocumentTransformResult MarkdownTransform::splitParagraphAtChildBoundaryWithResult(const MarkdownDocument& source,
                                                                                   MarkdownNodeId paragraphId,
                                                                                   int childIndex)
{
    MarkdownDocument result = splitParagraphAtChildBoundary(source, paragraphId, childIndex);
    QVector<MarkdownNodeId> affected = collectAffectedNodeIds(source, result);
    DocumentTransformResult dtr = resultWithFirstMovedChildAnchor(std::move(result), source, paragraphId, childIndex);
    dtr.affectedNodeIds = std::move(affected);
    return dtr;
}

MarkdownDocument MarkdownTransform::splitInlineNodeIntoParagraphs(const MarkdownDocument& source,
                                                                  MarkdownNodeId nodeId,
                                                                  int splitOffset,
                                                                  int splitEndOffset)
{
    MarkdownDocument document = source;
    MarkdownNode* node = mutableNodeById(document, nodeId);
    if (!node) {
        return document;
    }

    if (splitEndOffset < 0) {
        splitEndOffset = splitOffset;
    }
    if (splitOffset < 0 || splitEndOffset < splitOffset || splitEndOffset > node->literal.size()) {
        return document;
    }

    MarkdownNodeId splitChildId = node->id;
    MarkdownNode* paragraph = nullptr;
    MarkdownNode* inlineContainer = nullptr;
    if (node->type == MarkdownNodeType::Text) {
        MarkdownNode* parent = mutableNodeById(document, node->parent);
        if (!parent) {
            return document;
        }
        if (parent->type == MarkdownNodeType::Paragraph) {
            paragraph = parent;
        } else if (isFormattedInlineType(parent->type) && parent->children.size() == 1 && parent->children.first() == node->id) {
            inlineContainer = parent;
            paragraph = mutableNodeById(document, parent->parent);
            splitChildId = parent->id;
        } else {
            return document;
        }
    } else if (node->type == MarkdownNodeType::InlineCode) {
        if (!isSimpleInlineCode(*node)) {
            return document;
        }
        paragraph = mutableNodeById(document, node->parent);
    } else {
        return document;
    }

    if (!paragraph || paragraph->type != MarkdownNodeType::Paragraph) {
        return document;
    }

    MarkdownNode* parent = mutableNodeById(document, paragraph->parent);
    if (!parent || !isSupportedParagraphParentType(parent->type)) {
        return document;
    }

    const int paragraphPosition = parent->children.indexOf(paragraph->id);
    const int splitChildPosition = paragraph->children.indexOf(splitChildId);
    if (paragraphPosition < 0 || splitChildPosition < 0) {
        return document;
    }

    const QString leftLiteral = node->literal.left(splitOffset);
    const QString rightLiteral = node->literal.mid(splitEndOffset);
    node->literal = leftLiteral;

    MarkdownNode rightParagraph = *paragraph;
    rightParagraph.id = document.m_nextId++;
    rightParagraph.parent = parent->id;
    resetGeneratedNode(rightParagraph);

    if (inlineContainer) {
        MarkdownNode rightContainer = *inlineContainer;
        rightContainer.id = document.m_nextId++;
        rightContainer.parent = rightParagraph.id;
        resetGeneratedNode(rightContainer);

        MarkdownNode rightText = *node;
        rightText.id = document.m_nextId++;
        rightText.parent = rightContainer.id;
        rightText.literal = rightLiteral;
        resetGeneratedNode(rightText);

        rightContainer.children.append(rightText.id);
        rightParagraph.children.append(rightContainer.id);
        moveTrailingChildren(document, *paragraph, splitChildPosition, rightParagraph);

        parent->children.insert(paragraphPosition + 1, rightParagraph.id);
        document.m_nodes.append(std::move(rightParagraph));
        document.m_nodes.append(std::move(rightContainer));
        document.m_nodes.append(std::move(rightText));
    } else {
        MarkdownNode rightNode = *node;
        rightNode.id = document.m_nextId++;
        rightNode.parent = rightParagraph.id;
        rightNode.literal = rightLiteral;
        resetGeneratedNode(rightNode);

        rightParagraph.children.append(rightNode.id);
        moveTrailingChildren(document, *paragraph, splitChildPosition, rightParagraph);

        parent->children.insert(paragraphPosition + 1, rightParagraph.id);
        document.m_nodes.append(std::move(rightParagraph));
        document.m_nodes.append(std::move(rightNode));
    }

    document.rebuildIndex();
    return document;
}

DocumentTransformResult MarkdownTransform::splitInlineNodeIntoParagraphsWithResult(const MarkdownDocument& source,
                                                                                   MarkdownNodeId nodeId,
                                                                                   int splitOffset,
                                                                                   int splitEndOffset)
{
    const MarkdownNode* sourceNode = source.nodeById(nodeId);
    const MarkdownNodeType anchorType = sourceNode ? sourceNode->type : MarkdownNodeType::Unknown;
    MarkdownDocument result = splitInlineNodeIntoParagraphs(source, nodeId, splitOffset, splitEndOffset);
    QVector<MarkdownNodeId> affected = collectAffectedNodeIds(source, result);
    DocumentTransformResult dtr = resultWithAnchor(std::move(result), source, nodeId, anchorType);
    dtr.affectedNodeIds = std::move(affected);
    return dtr;
}

MarkdownDocument MarkdownTransform::splitFormattedTextIntoParagraphs(const MarkdownDocument& source,
                                                                     MarkdownNodeId textId,
                                                                     int splitOffset,
                                                                     int splitEndOffset)
{
    MarkdownDocument document = source;
    const auto textIt = document.m_indexById.constFind(textId);
    if (textIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& text = document.m_nodes[*textIt];
    if (splitEndOffset < 0) {
        splitEndOffset = splitOffset;
    }
    if (text.type != MarkdownNodeType::Text || splitOffset < 0 || splitEndOffset < splitOffset
        || splitEndOffset > text.literal.size()) {
        return document;
    }

    const auto formattedIt = document.m_indexById.constFind(text.parent);
    if (formattedIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& formatted = document.m_nodes[*formattedIt];
    if (!isFormattedInlineType(formatted.type) || formatted.children.size() != 1 || formatted.children.first() != text.id) {
        return document;
    }

    const auto paragraphIt = document.m_indexById.constFind(formatted.parent);
    if (paragraphIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& paragraph = document.m_nodes[*paragraphIt];
    if (paragraph.type != MarkdownNodeType::Paragraph) {
        return document;
    }

    const auto parentIt = document.m_indexById.constFind(paragraph.parent);
    if (parentIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& parent = document.m_nodes[*parentIt];
    if (!isSupportedParagraphParentType(parent.type)) {
        return document;
    }

    const int paragraphPosition = parent.children.indexOf(paragraph.id);
    const int formattedPosition = paragraph.children.indexOf(formatted.id);
    if (paragraphPosition < 0 || formattedPosition < 0) {
        return document;
    }

    const QString leftLiteral = text.literal.left(splitOffset);
    const QString rightLiteral = text.literal.mid(splitEndOffset);
    text.literal = leftLiteral;

    MarkdownNode rightParagraph = paragraph;
    rightParagraph.id = document.m_nextId++;
    rightParagraph.parent = parent.id;
    resetGeneratedNode(rightParagraph);

    MarkdownNode rightFormatted = formatted;
    rightFormatted.id = document.m_nextId++;
    rightFormatted.parent = rightParagraph.id;
    resetGeneratedNode(rightFormatted);

    MarkdownNode rightText = text;
    rightText.id = document.m_nextId++;
    rightText.parent = rightFormatted.id;
    rightText.literal = rightLiteral;
    resetGeneratedNode(rightText);

    rightFormatted.children.append(rightText.id);
    rightParagraph.children.append(rightFormatted.id);

    moveTrailingChildren(document, paragraph, formattedPosition, rightParagraph);

    parent.children.insert(paragraphPosition + 1, rightParagraph.id);
    document.m_nodes.append(std::move(rightParagraph));
    document.m_nodes.append(std::move(rightFormatted));
    document.m_nodes.append(std::move(rightText));
    document.rebuildIndex();
    return document;
}

DocumentTransformResult MarkdownTransform::splitFormattedTextIntoParagraphsWithResult(const MarkdownDocument& source,
                                                                                      MarkdownNodeId textId,
                                                                                      int splitOffset,
                                                                                      int splitEndOffset)
{
    MarkdownDocument result = splitFormattedTextIntoParagraphs(source, textId, splitOffset, splitEndOffset);
    QVector<MarkdownNodeId> affected = collectAffectedNodeIds(source, result);
    DocumentTransformResult dtr = resultWithAnchor(std::move(result), source, textId, MarkdownNodeType::Text);
    dtr.affectedNodeIds = std::move(affected);
    return dtr;
}

MarkdownDocument MarkdownTransform::splitInlineCodeIntoParagraphs(const MarkdownDocument& source,
                                                                  MarkdownNodeId codeId,
                                                                  int splitOffset,
                                                                  int splitEndOffset)
{
    MarkdownDocument document = source;
    const auto codeIt = document.m_indexById.constFind(codeId);
    if (codeIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& code = document.m_nodes[*codeIt];
    if (splitEndOffset < 0) {
        splitEndOffset = splitOffset;
    }
    if (!isSimpleInlineCode(code) || splitOffset < 0 || splitEndOffset < splitOffset
        || splitEndOffset > code.literal.size()) {
        return document;
    }

    const auto paragraphIt = document.m_indexById.constFind(code.parent);
    if (paragraphIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& paragraph = document.m_nodes[*paragraphIt];
    if (paragraph.type != MarkdownNodeType::Paragraph) {
        return document;
    }

    const auto parentIt = document.m_indexById.constFind(paragraph.parent);
    if (parentIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& parent = document.m_nodes[*parentIt];
    if (!isSupportedParagraphParentType(parent.type)) {
        return document;
    }

    const int paragraphPosition = parent.children.indexOf(paragraph.id);
    const int codePosition = paragraph.children.indexOf(code.id);
    if (paragraphPosition < 0 || codePosition < 0) {
        return document;
    }

    const QString leftLiteral = code.literal.left(splitOffset);
    const QString rightLiteral = code.literal.mid(splitEndOffset);
    code.literal = leftLiteral;

    MarkdownNode rightParagraph = paragraph;
    rightParagraph.id = document.m_nextId++;
    rightParagraph.parent = parent.id;
    resetGeneratedNode(rightParagraph);

    MarkdownNode rightCode = code;
    rightCode.id = document.m_nextId++;
    rightCode.parent = rightParagraph.id;
    rightCode.literal = rightLiteral;
    resetGeneratedNode(rightCode);

    rightParagraph.children.append(rightCode.id);

    moveTrailingChildren(document, paragraph, codePosition, rightParagraph);

    parent.children.insert(paragraphPosition + 1, rightParagraph.id);
    document.m_nodes.append(std::move(rightParagraph));
    document.m_nodes.append(std::move(rightCode));
    document.rebuildIndex();
    return document;
}

DocumentTransformResult MarkdownTransform::splitInlineCodeIntoParagraphsWithResult(const MarkdownDocument& source,
                                                                                   MarkdownNodeId codeId,
                                                                                   int splitOffset,
                                                                                   int splitEndOffset)
{
    MarkdownDocument result = splitInlineCodeIntoParagraphs(source, codeId, splitOffset, splitEndOffset);
    QVector<MarkdownNodeId> affected = collectAffectedNodeIds(source, result);
    DocumentTransformResult dtr = resultWithAnchor(std::move(result), source, codeId, MarkdownNodeType::InlineCode);
    dtr.affectedNodeIds = std::move(affected);
    return dtr;
}

MarkdownDocument MarkdownTransform::splitTextNodeIntoHeading(const MarkdownDocument& source,
                                                             MarkdownNodeId id,
                                                             int splitOffset,
                                                             int splitEndOffset)
{
    MarkdownDocument document = source;
    const auto textIt = document.m_indexById.constFind(id);
    if (textIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& text = document.m_nodes[*textIt];
    if (splitEndOffset < 0) {
        splitEndOffset = splitOffset;
    }
    if (text.type != MarkdownNodeType::Text || splitOffset < 0 || splitEndOffset < splitOffset
        || splitEndOffset > text.literal.size()) {
        return document;
    }

    const auto headingIt = document.m_indexById.constFind(text.parent);
    if (headingIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& heading = document.m_nodes[*headingIt];
    if (heading.type != MarkdownNodeType::Heading || heading.children.size() != 1 || heading.children.first() != id) {
        return document;
    }

    const auto rootIt = document.m_indexById.constFind(heading.parent);
    if (rootIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& root = document.m_nodes[*rootIt];
    if (root.type != MarkdownNodeType::Document) {
        return document;
    }

    const int headingPosition = root.children.indexOf(heading.id);
    if (headingPosition < 0) {
        return document;
    }

    const QString leftLiteral = text.literal.left(splitOffset);
    const QString rightLiteral = text.literal.mid(splitEndOffset);
    text.literal = leftLiteral;

    MarkdownNode rightHeading = heading;
    rightHeading.id = document.m_nextId++;
    rightHeading.parent = root.id;
    rightHeading.children.clear();
    rightHeading.inlineTokens.clear();

    MarkdownNode rightText = text;
    rightText.id = document.m_nextId++;
    rightText.parent = rightHeading.id;
    rightText.children.clear();
    rightText.inlineTokens.clear();
    rightText.literal = rightLiteral;

    rightHeading.children.append(rightText.id);
    root.children.insert(headingPosition + 1, rightHeading.id);
    document.m_nodes.append(std::move(rightHeading));
    document.m_nodes.append(std::move(rightText));
    document.rebuildIndex();
    return document;
}

DocumentTransformResult MarkdownTransform::splitTextNodeIntoHeadingWithResult(const MarkdownDocument& source,
                                                                              MarkdownNodeId id,
                                                                              int splitOffset,
                                                                              int splitEndOffset)
{
    MarkdownDocument result = splitTextNodeIntoHeading(source, id, splitOffset, splitEndOffset);
    QVector<MarkdownNodeId> affected = collectAffectedNodeIds(source, result);
    DocumentTransformResult dtr = resultWithAnchor(std::move(result), source, id, MarkdownNodeType::Text);
    dtr.affectedNodeIds = std::move(affected);
    return dtr;
}

MarkdownDocument MarkdownTransform::mergeParagraphs(const MarkdownDocument& source,
                                                    MarkdownNodeId previousParagraphId,
                                                    MarkdownNodeId nextParagraphId)
{
    MarkdownDocument document = source;
    const auto previousIt = document.m_indexById.constFind(previousParagraphId);
    const auto nextIt = document.m_indexById.constFind(nextParagraphId);
    if (previousIt == document.m_indexById.constEnd() || nextIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& previousParagraph = document.m_nodes[*previousIt];
    MarkdownNode& nextParagraph = document.m_nodes[*nextIt];
    if (previousParagraph.type != MarkdownNodeType::Paragraph || nextParagraph.type != MarkdownNodeType::Paragraph
        || previousParagraph.parent != nextParagraph.parent) {
        return document;
    }

    const auto parentIt = document.m_indexById.constFind(previousParagraph.parent);
    if (parentIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& parent = document.m_nodes[*parentIt];
    if (parent.type != MarkdownNodeType::Document && parent.type != MarkdownNodeType::BlockQuote
        && parent.type != MarkdownNodeType::ListItem) {
        return document;
    }

    const int previousPosition = parent.children.indexOf(previousParagraphId);
    const int nextPosition = parent.children.indexOf(nextParagraphId);
    if (previousPosition < 0 || nextPosition != previousPosition + 1) {
        return document;
    }

    QVector<MarkdownNodeId> childrenToMove = nextParagraph.children;
    QString boundaryWhitespace;
    if (!previousParagraph.children.isEmpty()) {
        if (const MarkdownNode* lastChild = document.nodeById(previousParagraph.children.last())) {
            boundaryWhitespace = inlineBoundaryWhitespace(document.source(), lastChild->source.end, nextParagraph.source.start);
        }
    }
    bool consumedBoundaryWhitespace = boundaryWhitespace.isEmpty();

    const auto mergeAdjacentInlineChildren = [&]() {
        if (previousParagraph.children.isEmpty() || childrenToMove.isEmpty()) {
            return false;
        }
        MarkdownNode* previousInline = mutableNodeById(document, previousParagraph.children.last());
        const MarkdownNode* nextInline = document.nodeById(childrenToMove.first());
        if (!previousInline || !nextInline) {
            return false;
        }
        const bool merged = mergeAdjacentInlineNodes(document, *previousInline, *nextInline, boundaryWhitespace);
        if (!merged) {
            return false;
        }
        childrenToMove.removeFirst();
        return true;
    };

    const bool mergedAdjacentInline = mergeAdjacentInlineChildren();
    consumedBoundaryWhitespace = consumedBoundaryWhitespace || mergedAdjacentInline;

    if (!consumedBoundaryWhitespace && !previousParagraph.children.isEmpty()) {
        const auto previousTextIt = document.m_indexById.constFind(previousParagraph.children.last());
        if (previousTextIt != document.m_indexById.constEnd()) {
            MarkdownNode& previousText = document.m_nodes[*previousTextIt];
            if (previousText.type == MarkdownNodeType::Text) {
                previousText.literal += boundaryWhitespace;
                consumedBoundaryWhitespace = true;
            }
        }
    }

    for (MarkdownNodeId childId : childrenToMove) {
        const auto childIt = document.m_indexById.constFind(childId);
        if (childIt == document.m_indexById.constEnd()) {
            return source;
        }
        document.m_nodes[*childIt].parent = previousParagraph.id;
        previousParagraph.children.append(childId);
    }
    nextParagraph.children.clear();
    parent.children.removeAt(nextPosition);
    return document;
}

DocumentTransformResult MarkdownTransform::mergeParagraphsWithResult(const MarkdownDocument& source,
                                                                     MarkdownNodeId previousParagraphId,
                                                                     MarkdownNodeId nextParagraphId)
{
    MarkdownDocument result = mergeParagraphs(source, previousParagraphId, nextParagraphId);
    QVector<MarkdownNodeId> affected = collectAffectedNodeIds(source, result);
    DocumentTransformResult dtr = resultWithParagraphEndAnchor(std::move(result), source, previousParagraphId);
    dtr.affectedNodeIds = std::move(affected);
    return dtr;
}

MarkdownDocument MarkdownTransform::mergeTextBlocks(const MarkdownDocument& source,
                                                    MarkdownNodeId previousBlockId,
                                                    MarkdownNodeId nextBlockId)
{
    MarkdownDocument document = source;
    const auto previousIt = document.m_indexById.constFind(previousBlockId);
    const auto nextIt = document.m_indexById.constFind(nextBlockId);
    if (previousIt == document.m_indexById.constEnd() || nextIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& previousBlock = document.m_nodes[*previousIt];
    MarkdownNode& nextBlock = document.m_nodes[*nextIt];
    if ((previousBlock.type != MarkdownNodeType::Paragraph && previousBlock.type != MarkdownNodeType::Heading)
        || (nextBlock.type != MarkdownNodeType::Paragraph && nextBlock.type != MarkdownNodeType::Heading)
        || previousBlock.parent != nextBlock.parent
        || previousBlock.children.size() != 1 || nextBlock.children.size() != 1) {
        return document;
    }

    const auto previousTextIt = document.m_indexById.constFind(previousBlock.children.first());
    const auto nextTextIt = document.m_indexById.constFind(nextBlock.children.first());
    if (previousTextIt == document.m_indexById.constEnd() || nextTextIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& previousText = document.m_nodes[*previousTextIt];
    const MarkdownNode& nextText = document.m_nodes.at(*nextTextIt);
    if (previousText.type != MarkdownNodeType::Text || nextText.type != MarkdownNodeType::Text) {
        return document;
    }

    const auto rootIt = document.m_indexById.constFind(previousBlock.parent);
    if (rootIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& root = document.m_nodes[*rootIt];
    if (root.type != MarkdownNodeType::Document) {
        return document;
    }

    const int previousPosition = root.children.indexOf(previousBlockId);
    const int nextPosition = root.children.indexOf(nextBlockId);
    if (previousPosition < 0 || nextPosition != previousPosition + 1) {
        return document;
    }

    previousText.literal += nextText.literal;
    root.children.removeAt(nextPosition);
    return document;
}

DocumentTransformResult MarkdownTransform::mergeTextBlocksWithResult(const MarkdownDocument& source,
                                                                     MarkdownNodeId previousBlockId,
                                                                     MarkdownNodeId nextBlockId)
{
    MarkdownDocument result = mergeTextBlocks(source, previousBlockId, nextBlockId);
    QVector<MarkdownNodeId> affected = collectAffectedNodeIds(source, result);
    DocumentTransformResult dtr = resultWithTextBlockEndAnchor(std::move(result), source, previousBlockId);
    dtr.affectedNodeIds = std::move(affected);
    return dtr;
}

MarkdownDocument MarkdownTransform::splitTextNodeIntoListItems(const MarkdownDocument& source,
                                                               MarkdownNodeId id,
                                                               int splitOffset,
                                                               int splitEndOffset)
{
    MarkdownDocument document = source;
    const auto textIt = document.m_indexById.constFind(id);
    if (textIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& text = document.m_nodes[*textIt];
    if (splitEndOffset < 0) {
        splitEndOffset = splitOffset;
    }
    if (text.type != MarkdownNodeType::Text || splitOffset < 0 || splitEndOffset < splitOffset
        || splitEndOffset > text.literal.size()) {
        return document;
    }

    const auto paragraphIt = document.m_indexById.constFind(text.parent);
    if (paragraphIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& paragraph = document.m_nodes[*paragraphIt];
    if (paragraph.type != MarkdownNodeType::Paragraph || paragraph.children.size() != 1 || paragraph.children.first() != id) {
        return document;
    }

    const auto itemIt = document.m_indexById.constFind(paragraph.parent);
    if (itemIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& item = document.m_nodes[*itemIt];
    if (item.type != MarkdownNodeType::ListItem) {
        return document;
    }

    const int paragraphPosition = item.children.indexOf(paragraph.id);
    if (paragraphPosition < 0) {
        return document;
    }

    const auto listIt = document.m_indexById.constFind(item.parent);
    if (listIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& list = document.m_nodes[*listIt];
    if (list.type != MarkdownNodeType::List) {
        return document;
    }

    const int itemPosition = list.children.indexOf(item.id);
    if (itemPosition < 0) {
        return document;
    }

    const QString leftLiteral = text.literal.left(splitOffset);
    const QString rightLiteral = text.literal.mid(splitEndOffset);
    text.literal = leftLiteral;

    MarkdownNode rightItem = item;
    rightItem.id = document.m_nextId++;
    rightItem.parent = list.id;
    rightItem.children.clear();
    rightItem.inlineTokens.clear();
    rightItem.source = {};
    rightItem.content = {};
    rightItem.sourceRange = {};
    if (rightItem.taskList) {
        rightItem.taskChecked = false;
    }

    MarkdownNode rightParagraph = paragraph;
    rightParagraph.id = document.m_nextId++;
    rightParagraph.parent = rightItem.id;
    rightParagraph.children.clear();
    rightParagraph.inlineTokens.clear();
    rightParagraph.source = {};
    rightParagraph.content = {};
    rightParagraph.sourceRange = {};

    MarkdownNode rightText = text;
    rightText.id = document.m_nextId++;
    rightText.parent = rightParagraph.id;
    rightText.children.clear();
    rightText.inlineTokens.clear();
    rightText.literal = rightLiteral;
    rightText.source = {};
    rightText.content = {};
    rightText.sourceRange = {};

    rightParagraph.children.append(rightText.id);
    rightItem.children.append(rightParagraph.id);
    while (item.children.size() > paragraphPosition + 1) {
        const MarkdownNodeId movedChildId = item.children.takeAt(paragraphPosition + 1);
        if (const auto childIt = document.m_indexById.constFind(movedChildId); childIt != document.m_indexById.constEnd()) {
            document.m_nodes[*childIt].parent = rightItem.id;
        }
        rightItem.children.append(movedChildId);
    }
    list.children.insert(itemPosition + 1, rightItem.id);
    document.m_nodes.append(std::move(rightItem));
    document.m_nodes.append(std::move(rightParagraph));
    document.m_nodes.append(std::move(rightText));
    document.rebuildIndex();
    return document;
}

DocumentTransformResult MarkdownTransform::splitTextNodeIntoListItemsWithResult(const MarkdownDocument& source,
                                                                                MarkdownNodeId id,
                                                                                int splitOffset,
                                                                                int splitEndOffset)
{
    MarkdownDocument result = splitTextNodeIntoListItems(source, id, splitOffset, splitEndOffset);
    QVector<MarkdownNodeId> affected = collectAffectedNodeIds(source, result);
    DocumentTransformResult dtr = resultWithAnchor(std::move(result), source, id, MarkdownNodeType::Text);
    dtr.affectedNodeIds = std::move(affected);
    return dtr;
}

MarkdownDocument MarkdownTransform::mergeListItems(const MarkdownDocument& source,
                                                   MarkdownNodeId previousItemId,
                                                   MarkdownNodeId nextItemId)
{
    MarkdownDocument document = source;
    const auto previousItemIt = document.m_indexById.constFind(previousItemId);
    const auto nextItemIt = document.m_indexById.constFind(nextItemId);
    if (previousItemIt == document.m_indexById.constEnd() || nextItemIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& previousItem = document.m_nodes[*previousItemIt];
    MarkdownNode& nextItem = document.m_nodes[*nextItemIt];
    if (previousItem.type != MarkdownNodeType::ListItem || nextItem.type != MarkdownNodeType::ListItem
        || previousItem.parent != nextItem.parent) {
        return document;
    }

    auto mutableNodeById = [&document](MarkdownNodeId nodeId) -> MarkdownNode* {
        const auto it = document.m_indexById.constFind(nodeId);
        return it == document.m_indexById.constEnd() ? nullptr : &document.m_nodes[*it];
    };

    auto firstParagraphChild = [&mutableNodeById](const MarkdownNode& node) -> MarkdownNode* {
        for (MarkdownNodeId childId : node.children) {
            MarkdownNode* child = mutableNodeById(childId);
            if (child && child->type == MarkdownNodeType::Paragraph) {
                return child;
            }
        }
        return nullptr;
    };

    auto lastParagraphChild = [&mutableNodeById](const MarkdownNode& node) -> MarkdownNode* {
        for (int i = node.children.size() - 1; i >= 0; --i) {
            MarkdownNode* child = mutableNodeById(node.children.at(i));
            if (child && child->type == MarkdownNodeType::Paragraph) {
                return child;
            }
        }
        return nullptr;
    };

    auto singleTextChild = [&mutableNodeById](const MarkdownNode& paragraph) -> MarkdownNode* {
        if (paragraph.type != MarkdownNodeType::Paragraph || paragraph.children.size() != 1) {
            return nullptr;
        }

        MarkdownNode* text = mutableNodeById(paragraph.children.first());
        return text && text->type == MarkdownNodeType::Text ? text : nullptr;
    };

    MarkdownNode* previousParagraph = lastParagraphChild(previousItem);
    MarkdownNode* nextParagraph = firstParagraphChild(nextItem);
    if (!previousParagraph || !nextParagraph) {
        return document;
    }

    MarkdownNode* previousText = singleTextChild(*previousParagraph);
    MarkdownNode* nextText = singleTextChild(*nextParagraph);
    if (!previousText || !nextText) {
        return document;
    }

    const auto listIt = document.m_indexById.constFind(previousItem.parent);
    if (listIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& list = document.m_nodes[*listIt];
    if (list.type != MarkdownNodeType::List) {
        return document;
    }

    const int previousPosition = list.children.indexOf(previousItemId);
    const int nextPosition = list.children.indexOf(nextItemId);
    if (previousPosition < 0 || nextPosition != previousPosition + 1) {
        return document;
    }

    previousText->literal += nextText->literal;
    nextItem.children.removeOne(nextParagraph->id);
    for (MarkdownNodeId childId : nextItem.children) {
        if (MarkdownNode* child = mutableNodeById(childId)) {
            child->parent = previousItem.id;
        }
        previousItem.children.append(childId);
    }
    list.children.removeAt(nextPosition);
    document.rebuildIndex();
    return document;
}

DocumentTransformResult MarkdownTransform::mergeListItemsWithResult(const MarkdownDocument& source,
                                                                    MarkdownNodeId previousItemId,
                                                                    MarkdownNodeId nextItemId)
{
    MarkdownDocument result = mergeListItems(source, previousItemId, nextItemId);
    QVector<MarkdownNodeId> affected = collectAffectedNodeIds(source, result);
    DocumentTransformResult dtr = resultWithListItemEndAnchor(std::move(result), source, previousItemId);
    dtr.affectedNodeIds = std::move(affected);
    return dtr;
}

MarkdownDocument MarkdownTransform::removeEmptyListItem(const MarkdownDocument& source, MarkdownNodeId itemId)
{
    MarkdownDocument document = source;
    const auto itemIt = document.m_indexById.constFind(itemId);
    if (itemIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& item = document.m_nodes[*itemIt];
    if (item.type != MarkdownNodeType::ListItem || item.children.size() > 1) {
        return document;
    }

    if (item.children.size() == 1) {
        const auto paragraphIt = document.m_indexById.constFind(item.children.first());
        if (paragraphIt == document.m_indexById.constEnd()) {
            return document;
        }

        const MarkdownNode& paragraph = document.m_nodes.at(*paragraphIt);
        if (paragraph.type != MarkdownNodeType::Paragraph || paragraph.children.size() > 1) {
            return document;
        }

        if (paragraph.children.size() == 1) {
            const auto textIt = document.m_indexById.constFind(paragraph.children.first());
            if (textIt == document.m_indexById.constEnd()) {
                return document;
            }

            const MarkdownNode& text = document.m_nodes.at(*textIt);
            if (text.type != MarkdownNodeType::Text || !text.literal.isEmpty()) {
                return document;
            }
        }
    }

    const auto listIt = document.m_indexById.constFind(item.parent);
    if (listIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& list = document.m_nodes[*listIt];
    if (list.type != MarkdownNodeType::List) {
        return document;
    }
    const int itemPosition = list.children.indexOf(itemId);
    if (itemPosition < 0) {
        return document;
    }

    list.children.removeAt(itemPosition);
    if (!list.children.isEmpty()) {
        return document;
    }

    const auto parentIt = document.m_indexById.constFind(list.parent);
    if (parentIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& parent = document.m_nodes[*parentIt];
    if (parent.type != MarkdownNodeType::Document && parent.type != MarkdownNodeType::ListItem
        && parent.type != MarkdownNodeType::BlockQuote) {
        return document;
    }

    const int listPosition = parent.children.indexOf(list.id);
    if (listPosition >= 0) {
        parent.children.removeAt(listPosition);
    }
    return document;
}

DocumentTransformResult MarkdownTransform::removeEmptyListItemWithResult(const MarkdownDocument& source,
                                                                         MarkdownNodeId itemId)
{
    MarkdownNodeId anchorItemId = 0;
    const MarkdownNode* item = source.nodeById(itemId);
    if (item && item->type == MarkdownNodeType::ListItem) {
        const MarkdownNode* list = source.nodeById(item->parent);
        if (list && list->type == MarkdownNodeType::List) {
            const int itemPosition = list->children.indexOf(itemId);
            if (itemPosition > 0) {
                anchorItemId = list->children.at(itemPosition - 1);
            } else {
                const MarkdownNode* parent = source.nodeById(list->parent);
                if (parent && parent->type == MarkdownNodeType::ListItem) {
                    anchorItemId = parent->id;
                }
            }
        }
    }

    MarkdownDocument document = removeEmptyListItem(source, itemId);
    if (!anchorItemId || !document.nodeById(anchorItemId)) {
        return DocumentTransformResult{std::move(document), 0, -1};
    }

    return resultWithListItemEndAnchor(std::move(document), source, anchorItemId);
}

MarkdownDocument MarkdownTransform::demoteListItem(const MarkdownDocument& source, MarkdownNodeId itemId)
{
    MarkdownDocument document = source;
    const auto itemIt = document.m_indexById.constFind(itemId);
    if (itemIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& item = document.m_nodes[*itemIt];
    if (item.type != MarkdownNodeType::ListItem) {
        return document;
    }

    const auto listIt = document.m_indexById.constFind(item.parent);
    if (listIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& list = document.m_nodes[*listIt];
    if (list.type != MarkdownNodeType::List) {
        return document;
    }
    const int itemPosition = list.children.indexOf(itemId);
    if (itemPosition <= 0) {
        return document;
    }

    const MarkdownNodeId previousItemId = list.children.at(itemPosition - 1);
    const auto previousItemIt = document.m_indexById.constFind(previousItemId);
    if (previousItemIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& previousItem = document.m_nodes[*previousItemIt];
    if (previousItem.type != MarkdownNodeType::ListItem) {
        return document;
    }

    MarkdownNodeId childListId = 0;
    for (MarkdownNodeId childId : previousItem.children) {
        const auto childIt = document.m_indexById.constFind(childId);
        if (childIt == document.m_indexById.constEnd()) {
            continue;
        }

        MarkdownNode& child = document.m_nodes[*childIt];
        if (child.type == MarkdownNodeType::List && child.orderedList == list.orderedList) {
            childListId = child.id;
            break;
        }
    }

    if (childListId == 0) {
        MarkdownNode nestedList = list;
        nestedList.id = document.m_nextId++;
        nestedList.parent = previousItem.id;
        nestedList.children.clear();
        nestedList.source = {};
        nestedList.content = {};
        nestedList.sourceRange = {};
        childListId = nestedList.id;
        previousItem.children.append(childListId);
        document.m_nodes.append(std::move(nestedList));
        document.rebuildIndex();
    }

    const auto refreshedItemIt = document.m_indexById.constFind(itemId);
    const auto refreshedListIt = document.m_indexById.constFind(list.id);
    const auto childListIt = document.m_indexById.constFind(childListId);
    if (refreshedItemIt == document.m_indexById.constEnd()
        || refreshedListIt == document.m_indexById.constEnd()
        || childListIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& refreshedItem = document.m_nodes[*refreshedItemIt];
    MarkdownNode& refreshedList = document.m_nodes[*refreshedListIt];
    MarkdownNode& childList = document.m_nodes[*childListIt];
    refreshedList.children.removeAt(itemPosition);
    refreshedItem.parent = childListId;
    childList.children.append(refreshedItem.id);
    document.rebuildIndex();
    return document;
}

DocumentTransformResult MarkdownTransform::demoteListItemWithResult(const MarkdownDocument& source,
                                                                    MarkdownNodeId itemId)
{
    return resultWithListItemEndAnchor(demoteListItem(source, itemId),
                                       source,
                                       itemId);
}

MarkdownDocument MarkdownTransform::demoteListItems(const MarkdownDocument& source, const QVector<MarkdownNodeId>& itemIds)
{
    if (itemIds.isEmpty()) {
        return source;
    }

    MarkdownDocument document = source;
    const MarkdownNode* firstSourceItem = source.nodeById(itemIds.first());
    if (!firstSourceItem || firstSourceItem->type != MarkdownNodeType::ListItem) {
        return document;
    }
    const MarkdownNodeId listId = firstSourceItem->parent;
    const MarkdownNode* sourceList = source.nodeById(listId);
    if (!sourceList || sourceList->type != MarkdownNodeType::List) {
        return document;
    }

    QVector<int> positions;
    positions.reserve(itemIds.size());
    for (MarkdownNodeId itemId : itemIds) {
        const MarkdownNode* item = source.nodeById(itemId);
        if (!item || item->type != MarkdownNodeType::ListItem || item->parent != listId) {
            return document;
        }
        positions.append(sourceList->children.indexOf(itemId));
    }
    for (int i = 0; i < positions.size(); ++i) {
        if (positions.at(i) < 0 || (i > 0 && positions.at(i) != positions.at(i - 1) + 1)) {
            return document;
        }
    }
    if (positions.first() <= 0) {
        return document;
    }

    MarkdownNode* list = mutableNodeById(document, listId);
    MarkdownNode* previousItem = mutableNodeById(document, sourceList->children.at(positions.first() - 1));
    if (!list || list->type != MarkdownNodeType::List || !previousItem || previousItem->type != MarkdownNodeType::ListItem) {
        return document;
    }

    MarkdownNodeId childListId = 0;
    for (MarkdownNodeId childId : previousItem->children) {
        MarkdownNode* child = mutableNodeById(document, childId);
        if (child && child->type == MarkdownNodeType::List && child->orderedList == list->orderedList) {
            childListId = child->id;
            break;
        }
    }

    if (!childListId) {
        MarkdownNode nestedList = *list;
        nestedList.id = document.m_nextId++;
        nestedList.parent = previousItem->id;
        nestedList.children.clear();
        nestedList.source = {};
        nestedList.content = {};
        nestedList.sourceRange = {};
        childListId = nestedList.id;
        previousItem->children.append(childListId);
        document.m_nodes.append(std::move(nestedList));
        document.rebuildIndex();
        list = mutableNodeById(document, listId);
    }

    MarkdownNode* childList = mutableNodeById(document, childListId);
    if (!list || !childList || childList->type != MarkdownNodeType::List) {
        return document;
    }

    for (int i = itemIds.size() - 1; i >= 0; --i) {
        list->children.removeAt(positions.at(i));
    }
    for (MarkdownNodeId itemId : itemIds) {
        MarkdownNode* item = mutableNodeById(document, itemId);
        if (!item || item->type != MarkdownNodeType::ListItem) {
            return document;
        }
        item->parent = childListId;
        childList->children.append(item->id);
    }

    document.rebuildIndex();
    return document;
}

DocumentTransformResult MarkdownTransform::demoteListItemsWithResult(const MarkdownDocument& source,
                                                                     const QVector<MarkdownNodeId>& itemIds)
{
    return resultWithListItemEndAnchor(demoteListItems(source, itemIds),
                                       source,
                                       itemIds.isEmpty() ? 0 : itemIds.last());
}

MarkdownDocument MarkdownTransform::promoteListItem(const MarkdownDocument& source, MarkdownNodeId itemId)
{
    MarkdownDocument document = source;
    const auto itemIt = document.m_indexById.constFind(itemId);
    if (itemIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& item = document.m_nodes[*itemIt];
    if (item.type != MarkdownNodeType::ListItem) {
        return document;
    }

    const auto listIt = document.m_indexById.constFind(item.parent);
    if (listIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& list = document.m_nodes[*listIt];
    if (list.type != MarkdownNodeType::List) {
        return document;
    }
    const MarkdownNodeId listId = list.id;

    const int itemPosition = list.children.indexOf(itemId);
    if (itemPosition < 0) {
        return document;
    }

    const auto parentItemIt = document.m_indexById.constFind(list.parent);
    if (parentItemIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& parentItem = document.m_nodes[*parentItemIt];
    if (parentItem.type != MarkdownNodeType::ListItem) {
        return document;
    }
    const MarkdownNodeId parentItemId = parentItem.id;

    const auto parentListIt = document.m_indexById.constFind(parentItem.parent);
    if (parentListIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& parentList = document.m_nodes[*parentListIt];
    if (parentList.type != MarkdownNodeType::List) {
        return document;
    }
    const MarkdownNodeId parentListId = parentList.id;

    const int parentItemPosition = parentList.children.indexOf(parentItem.id);
    if (parentItemPosition < 0) {
        return document;
    }

    QVector<MarkdownNodeId> followingSiblings;
    while (list.children.size() > itemPosition + 1) {
        followingSiblings.append(list.children.takeAt(itemPosition + 1));
    }

    MarkdownNodeId childListId = 0;
    if (!followingSiblings.isEmpty()) {
        for (MarkdownNodeId childId : item.children) {
            const auto childIt = document.m_indexById.constFind(childId);
            if (childIt == document.m_indexById.constEnd()) {
                continue;
            }

            const MarkdownNode& child = document.m_nodes.at(*childIt);
            if (child.type == MarkdownNodeType::List && child.orderedList == list.orderedList) {
                childListId = child.id;
                break;
            }
        }

        if (childListId == 0) {
            MarkdownNode nestedList = list;
            nestedList.id = document.m_nextId++;
            nestedList.parent = item.id;
            nestedList.children.clear();
            nestedList.source = {};
            nestedList.content = {};
            nestedList.sourceRange = {};
            childListId = nestedList.id;
            item.children.append(childListId);
            document.m_nodes.append(std::move(nestedList));
            document.rebuildIndex();
        }
    }

    const auto refreshedItemIt = document.m_indexById.constFind(itemId);
    const auto refreshedListIt = document.m_indexById.constFind(listId);
    const auto refreshedParentItemIt = document.m_indexById.constFind(parentItemId);
    const auto refreshedParentListIt = document.m_indexById.constFind(parentListId);
    if (refreshedItemIt == document.m_indexById.constEnd()
        || refreshedListIt == document.m_indexById.constEnd()
        || refreshedParentItemIt == document.m_indexById.constEnd()
        || refreshedParentListIt == document.m_indexById.constEnd()) {
        return document;
    }

    MarkdownNode& refreshedItem = document.m_nodes[*refreshedItemIt];
    MarkdownNode& refreshedList = document.m_nodes[*refreshedListIt];
    MarkdownNode& refreshedParentItem = document.m_nodes[*refreshedParentItemIt];
    MarkdownNode& refreshedParentList = document.m_nodes[*refreshedParentListIt];

    const int refreshedItemPosition = refreshedList.children.indexOf(itemId);
    const int refreshedParentItemPosition = refreshedParentList.children.indexOf(refreshedParentItem.id);
    if (refreshedItemPosition < 0 || refreshedParentItemPosition < 0) {
        return document;
    }

    refreshedList.children.removeAt(refreshedItemPosition);
    refreshedItem.parent = refreshedParentList.id;
    refreshedParentList.children.insert(refreshedParentItemPosition + 1, refreshedItem.id);

    if (!followingSiblings.isEmpty()) {
        const auto childListIt = document.m_indexById.constFind(childListId);
        if (childListIt == document.m_indexById.constEnd()) {
            return document;
        }

        MarkdownNode& childList = document.m_nodes[*childListIt];
        for (MarkdownNodeId siblingId : followingSiblings) {
            if (const auto siblingIt = document.m_indexById.constFind(siblingId); siblingIt != document.m_indexById.constEnd()) {
                document.m_nodes[*siblingIt].parent = childList.id;
            }
            childList.children.append(siblingId);
        }
    }

    if (refreshedList.children.isEmpty()) {
        refreshedParentItem.children.removeOne(refreshedList.id);
    }
    document.rebuildIndex();
    return document;
}

DocumentTransformResult MarkdownTransform::promoteListItemWithResult(const MarkdownDocument& source,
                                                                     MarkdownNodeId itemId)
{
    return resultWithListItemEndAnchor(promoteListItem(source, itemId),
                                       source,
                                       itemId);
}

MarkdownDocument MarkdownTransform::promoteListItems(const MarkdownDocument& source, const QVector<MarkdownNodeId>& itemIds)
{
    if (itemIds.isEmpty()) {
        return source;
    }

    MarkdownDocument document = source;
    const MarkdownNode* firstSourceItem = source.nodeById(itemIds.first());
    if (!firstSourceItem || firstSourceItem->type != MarkdownNodeType::ListItem) {
        return document;
    }
    const MarkdownNodeId listId = firstSourceItem->parent;
    const MarkdownNode* sourceList = source.nodeById(listId);
    if (!sourceList || sourceList->type != MarkdownNodeType::List) {
        return document;
    }

    const MarkdownNode* parentItem = source.nodeById(sourceList->parent);
    if (!parentItem || parentItem->type != MarkdownNodeType::ListItem) {
        return document;
    }
    const MarkdownNode* parentList = source.nodeById(parentItem->parent);
    if (!parentList || parentList->type != MarkdownNodeType::List) {
        return document;
    }

    QVector<int> positions;
    positions.reserve(itemIds.size());
    for (MarkdownNodeId itemId : itemIds) {
        const MarkdownNode* item = source.nodeById(itemId);
        if (!item || item->type != MarkdownNodeType::ListItem || item->parent != listId) {
            return document;
        }
        positions.append(sourceList->children.indexOf(itemId));
    }
    for (int i = 0; i < positions.size(); ++i) {
        if (positions.at(i) < 0 || (i > 0 && positions.at(i) != positions.at(i - 1) + 1)) {
            return document;
        }
    }

    QVector<MarkdownNodeId> followingSiblings;
    for (int i = positions.last() + 1; i < sourceList->children.size(); ++i) {
        followingSiblings.append(sourceList->children.at(i));
    }

    MarkdownNode* list = mutableNodeById(document, listId);
    MarkdownNode* mutableParentItem = mutableNodeById(document, parentItem->id);
    MarkdownNode* mutableParentList = mutableNodeById(document, parentList->id);
    if (!list || list->type != MarkdownNodeType::List || !mutableParentItem || !mutableParentList) {
        return document;
    }

    MarkdownNodeId childListId = 0;
    if (!followingSiblings.isEmpty()) {
        MarkdownNode* lastPromotedItem = mutableNodeById(document, itemIds.last());
        if (!lastPromotedItem || lastPromotedItem->type != MarkdownNodeType::ListItem) {
            return document;
        }
        for (MarkdownNodeId childId : lastPromotedItem->children) {
            MarkdownNode* child = mutableNodeById(document, childId);
            if (child && child->type == MarkdownNodeType::List && child->orderedList == list->orderedList) {
                childListId = child->id;
                break;
            }
        }
        if (!childListId) {
            MarkdownNode nestedList = *list;
            nestedList.id = document.m_nextId++;
            nestedList.parent = lastPromotedItem->id;
            nestedList.children.clear();
            nestedList.source = {};
            nestedList.content = {};
            nestedList.sourceRange = {};
            childListId = nestedList.id;
            lastPromotedItem->children.append(childListId);
            document.m_nodes.append(std::move(nestedList));
            document.rebuildIndex();
            list = mutableNodeById(document, listId);
            mutableParentItem = mutableNodeById(document, parentItem->id);
            mutableParentList = mutableNodeById(document, parentList->id);
        }
    }

    if (!list || !mutableParentItem || !mutableParentList) {
        return document;
    }

    while (list->children.size() > positions.first()) {
        list->children.removeAt(positions.first());
    }

    const int parentItemPosition = mutableParentList->children.indexOf(parentItem->id);
    if (parentItemPosition < 0) {
        return document;
    }
    int insertPosition = parentItemPosition + 1;
    for (MarkdownNodeId itemId : itemIds) {
        MarkdownNode* item = mutableNodeById(document, itemId);
        if (!item || item->type != MarkdownNodeType::ListItem) {
            return document;
        }
        item->parent = mutableParentList->id;
        mutableParentList->children.insert(insertPosition, item->id);
        ++insertPosition;
    }

    if (!followingSiblings.isEmpty()) {
        MarkdownNode* childList = mutableNodeById(document, childListId);
        if (!childList || childList->type != MarkdownNodeType::List) {
            return document;
        }
        for (MarkdownNodeId siblingId : followingSiblings) {
            MarkdownNode* sibling = mutableNodeById(document, siblingId);
            if (sibling) {
                sibling->parent = childList->id;
            }
            childList->children.append(siblingId);
        }
    }

    if (list->children.isEmpty()) {
        mutableParentItem->children.removeOne(list->id);
    }
    document.rebuildIndex();
    return document;
}

DocumentTransformResult MarkdownTransform::promoteListItemsWithResult(const MarkdownDocument& source,
                                                                      const QVector<MarkdownNodeId>& itemIds)
{
    return resultWithListItemEndAnchor(promoteListItems(source, itemIds),
                                       source,
                                       itemIds.isEmpty() ? 0 : itemIds.last());
}

DocumentTransformResult MarkdownTransform::resultWithAnchor(MarkdownDocument document,
                                                            const MarkdownDocument& source,
                                                            MarkdownNodeId originalNodeId,
                                                            MarkdownNodeType anchorType)
{
    const MarkdownNodeId firstNewId = source.m_nextId;
    MarkdownNodeId anchorNodeId = 0;
    for (const MarkdownNode& node : document.m_nodes) {
        if (node.id < firstNewId || node.type != anchorType) {
            continue;
        }
        if (anchorType == MarkdownNodeType::Text || anchorType == MarkdownNodeType::InlineCode) {
            anchorNodeId = node.id;
            break;
        }
    }

    if (!anchorNodeId) {
        const MarkdownNode* originalNode = document.nodeById(originalNodeId);
        if (originalNode && originalNode->type == anchorType) {
            anchorNodeId = originalNode->id;
        }
    }

    return DocumentTransformResult{std::move(document), anchorNodeId, anchorNodeId ? 0 : -1};
}

DocumentTransformResult MarkdownTransform::resultWithFirstMovedChildAnchor(MarkdownDocument document,
                                                                           const MarkdownDocument& source,
                                                                           MarkdownNodeId paragraphId,
                                                                           int childIndex)
{
    MarkdownNodeId anchorNodeId = 0;
    const MarkdownNode* sourceParagraph = source.nodeById(paragraphId);
    if (sourceParagraph && childIndex >= 0 && childIndex < sourceParagraph->children.size()) {
        anchorNodeId = sourceParagraph->children.at(childIndex);
        const MarkdownNode* anchorNode = document.nodeById(anchorNodeId);
        if (anchorNode && (anchorNode->type == MarkdownNodeType::Strong || anchorNode->type == MarkdownNodeType::Emphasis
                              || anchorNode->type == MarkdownNodeType::Link)
            && !anchorNode->children.isEmpty()) {
            anchorNodeId = anchorNode->children.first();
        }
    }

    return DocumentTransformResult{std::move(document), anchorNodeId, anchorNodeId ? 0 : -1};
}

DocumentTransformResult MarkdownTransform::resultWithParagraphEndAnchor(MarkdownDocument document,
                                                                        const MarkdownDocument& source,
                                                                        MarkdownNodeId paragraphId)
{
    MarkdownNodeId anchorNodeId = 0;
    int anchorOffsetInNode = -1;
    const MarkdownNode* paragraph = source.nodeById(paragraphId);
    if (paragraph && paragraph->type == MarkdownNodeType::Paragraph && !paragraph->children.isEmpty()) {
        const MarkdownNode* lastChild = source.nodeById(paragraph->children.last());
        if (lastChild) {
            if (lastChild->type == MarkdownNodeType::Text || lastChild->type == MarkdownNodeType::InlineCode) {
                anchorNodeId = lastChild->id;
                anchorOffsetInNode = lastChild->literal.size();
            } else if ((lastChild->type == MarkdownNodeType::Strong || lastChild->type == MarkdownNodeType::Emphasis
                           || lastChild->type == MarkdownNodeType::Link)
                       && !lastChild->children.isEmpty()) {
                const MarkdownNode* text = source.nodeById(lastChild->children.last());
                if (text && text->type == MarkdownNodeType::Text) {
                    anchorNodeId = text->id;
                    anchorOffsetInNode = text->literal.size();
                }
            }
        }
    }

    if (anchorNodeId && !document.nodeById(anchorNodeId)) {
        anchorNodeId = 0;
        anchorOffsetInNode = -1;
    }

    return DocumentTransformResult{std::move(document), anchorNodeId, anchorOffsetInNode};
}

DocumentTransformResult MarkdownTransform::resultWithListItemEndAnchor(MarkdownDocument document,
                                                                       const MarkdownDocument& source,
                                                                       MarkdownNodeId itemId)
{
    MarkdownNodeId paragraphId = 0;
    const MarkdownNode* item = source.nodeById(itemId);
    if (item && item->type == MarkdownNodeType::ListItem) {
        for (int i = item->children.size() - 1; i >= 0; --i) {
            const MarkdownNode* child = source.nodeById(item->children.at(i));
            if (child && child->type == MarkdownNodeType::Paragraph) {
                paragraphId = child->id;
                break;
            }
        }
    }

    if (!paragraphId) {
        return DocumentTransformResult{std::move(document), 0, -1};
    }

    return resultWithParagraphEndAnchor(std::move(document), source, paragraphId);
}

DocumentTransformResult MarkdownTransform::resultWithTextBlockEndAnchor(MarkdownDocument document,
                                                                        const MarkdownDocument& source,
                                                                        MarkdownNodeId blockId)
{
    MarkdownNodeId anchorNodeId = 0;
    int anchorOffsetInNode = -1;
    const MarkdownNode* block = source.nodeById(blockId);
    if (block && (block->type == MarkdownNodeType::Paragraph || block->type == MarkdownNodeType::Heading)
        && block->children.size() == 1) {
        const MarkdownNode* text = source.nodeById(block->children.first());
        if (text && text->type == MarkdownNodeType::Text) {
            anchorNodeId = text->id;
            anchorOffsetInNode = text->literal.size();
        }
    }

    if (anchorNodeId && !document.nodeById(anchorNodeId)) {
        anchorNodeId = 0;
        anchorOffsetInNode = -1;
    }

    return DocumentTransformResult{std::move(document), anchorNodeId, anchorOffsetInNode};
}

DocumentTransformResult MarkdownTransform::insertNode(MarkdownDocument source,
                                                      MarkdownNodeId parentId,
                                                      int childIndex,
                                                      MarkdownNode node)
{
    MarkdownNode* parent = mutableNodeById(source, parentId);
    if (!parent) {
        return {std::move(source), 0, -1};
    }

    node.parent = parentId;
    if (childIndex < 0 || childIndex > parent->children.size()) {
        childIndex = parent->children.size();
    }

    if (node.id == 0) {
        node.id = source.m_nextId++;
    }

    parent->children.insert(childIndex, node.id);
    if (childIndex > 0) {
        const MarkdownNodeId prevSiblingId = parent->children.at(childIndex - 1);
        MarkdownNode* prevSibling = mutableNodeById(source, prevSiblingId);
        if (prevSibling) {
            // Update previous sibling's source spans if needed
        }
    }

    source.m_nodes.append(std::move(node));
    source.rebuildIndex();

    MarkdownNodeId insertedId = source.m_nodes.last().id;
    return {std::move(source), insertedId, 0};
}

DocumentTransformResult MarkdownTransform::removeNode(MarkdownDocument source,
                                                      MarkdownNodeId nodeId)
{
    MarkdownNode* node = mutableNodeById(source, nodeId);
    if (!node) {
        return {std::move(source), 0, -1};
    }

    const MarkdownNodeId parentId = node->parent;
    MarkdownNode* parent = mutableNodeById(source, parentId);
    if (!parent) {
        return {std::move(source), 0, -1};
    }

    parent->children.removeOne(nodeId);

    for (MarkdownNodeId childId : node->children) {
        MarkdownNode* child = mutableNodeById(source, childId);
        if (child && child->parent == nodeId) {
            child->parent = 0;
        }
    }

    node->type = MarkdownNodeType::Unknown;
    node->parent = 0;
    node->children.clear();

    source.rebuildIndex();
    return {std::move(source), parentId, 0};
}

DocumentTransformResult MarkdownTransform::reparentNode(MarkdownDocument source,
                                                        MarkdownNodeId nodeId,
                                                        MarkdownNodeId newParentId,
                                                        int childIndex)
{
    MarkdownNode* node = mutableNodeById(source, nodeId);
    if (!node) {
        return {std::move(source), 0, -1};
    }

    MarkdownNode* oldParent = mutableNodeById(source, node->parent);
    if (oldParent) {
        oldParent->children.removeOne(nodeId);
    }

    MarkdownNode* newParent = mutableNodeById(source, newParentId);
    if (!newParent) {
        return {std::move(source), 0, -1};
    }

    if (childIndex < 0 || childIndex > newParent->children.size()) {
        childIndex = newParent->children.size();
    }

    newParent->children.insert(childIndex, nodeId);
    node->parent = newParentId;

    source.rebuildIndex();
    return {std::move(source), nodeId, 0};
}

} // namespace Muffin
