#include "StyleCommandHandler.h"

namespace Muffin {

namespace {

MarkdownNodeType nodeTypeForStyle(StyleCommandHandler::InlineStyle style)
{
    switch (style) {
    case StyleCommandHandler::InlineStyle::Strong:
        return MarkdownNodeType::Strong;
    case StyleCommandHandler::InlineStyle::Emphasis:
        return MarkdownNodeType::Emphasis;
    case StyleCommandHandler::InlineStyle::InlineCode:
        return MarkdownNodeType::InlineCode;
    case StyleCommandHandler::InlineStyle::Strikethrough:
        return MarkdownNodeType::Strikethrough;
    }
    return MarkdownNodeType::Unknown;
}

int markerLengthForStyle(MarkdownNodeType styleType)
{
    switch (styleType) {
    case MarkdownNodeType::Emphasis:
    case MarkdownNodeType::InlineCode:
        return 1;
    case MarkdownNodeType::Strong:
    case MarkdownNodeType::Strikethrough:
        return 2;
    default:
        return 0;
    }
}

void clearGeneratedSource(MarkdownNode& node)
{
    node.source = {};
    node.content = {};
    node.sourceRange = {};
    node.inlineTokens.clear();
}

bool isContainerParentType(MarkdownNodeType type)
{
    return type == MarkdownNodeType::Paragraph || type == MarkdownNodeType::Heading || type == MarkdownNodeType::TableCell;
}

bool isInlineNodeType(MarkdownNodeType type)
{
    return type == MarkdownNodeType::Text || type == MarkdownNodeType::Emphasis || type == MarkdownNodeType::Strong
        || type == MarkdownNodeType::InlineCode || type == MarkdownNodeType::Link || type == MarkdownNodeType::Image
        || type == MarkdownNodeType::FormulaInline || type == MarkdownNodeType::Strikethrough;
}

bool inlineNodeCoversSelectionStart(const MarkdownNode& node, int offset)
{
    if (!node.source.isValid()) {
        return false;
    }
    return offset >= node.source.start && offset < node.source.end;
}

bool inlineNodeCoversSelectionEnd(const MarkdownNode& node, int offset)
{
    if (!node.source.isValid()) {
        return false;
    }
    return offset > node.source.start && offset <= node.source.end;
}

const MarkdownNode* firstTextChild(const MarkdownDocument& document, const MarkdownNode& node)
{
    if (node.type == MarkdownNodeType::Text) {
        return &node;
    }
    if (node.children.size() != 1) {
        return nullptr;
    }

    const MarkdownNode* child = document.nodeById(node.children.first());
    if (!child || child->type != MarkdownNodeType::Text) {
        return nullptr;
    }
    return child;
}

bool textContentCoversSelectionStart(const MarkdownDocument& document, const MarkdownNode& node, int offset)
{
    const MarkdownNode* text = firstTextChild(document, node);
    return text && text->source.isValid() && offset >= text->source.start && offset < text->source.end;
}

bool textContentCoversSelectionEnd(const MarkdownDocument& document, const MarkdownNode& node, int offset)
{
    const MarkdownNode* text = firstTextChild(document, node);
    return text && text->source.isValid() && offset > text->source.start && offset <= text->source.end;
}

} // namespace

struct StyleCommandHandler::InlineTransform {
    explicit InlineTransform(MarkdownDocument& target)
        : document(target)
    {
    }

    MarkdownNodeId appendTextNode(const MarkdownNode& textTemplate, const QString& literal, MarkdownNodeId parentId)
    {
        if (literal.isEmpty()) {
            return 0;
        }

        MarkdownNode node = textTemplate;
        node.id = document.m_nextId++;
        node.parent = parentId;
        node.type = MarkdownNodeType::Text;
        node.children.clear();
        node.literal = literal;
        clearGeneratedSource(node);
        const MarkdownNodeId nodeId = node.id;
        document.m_nodes.append(std::move(node));
        return nodeId;
    }

    MarkdownNodeId appendStyleNode(const MarkdownNode& nodeTemplate,
                                   MarkdownNodeId parentId,
                                   MarkdownNodeType type,
                                   const QVector<MarkdownNodeId>& children,
                                   const QString& literal = {})
    {
        return appendStyleNodeWithId(document.m_nextId++, nodeTemplate, parentId, type, children, literal);
    }

    MarkdownNodeId appendStyleNodeWithId(MarkdownNodeId id,
                                         const MarkdownNode& nodeTemplate,
                                         MarkdownNodeId parentId,
                                         MarkdownNodeType type,
                                         const QVector<MarkdownNodeId>& children,
                                         const QString& literal = {})
    {
        MarkdownNode node = nodeTemplate;
        node.id = id;
        node.parent = parentId;
        node.type = type;
        node.children = children;
        node.literal = literal;
        clearGeneratedSource(node);
        const MarkdownNodeId nodeId = node.id;
        document.m_nodes.append(std::move(node));
        return nodeId;
    }

    MarkdownNodeId appendStyledTextPart(const MarkdownNode& container, const QString& literal, MarkdownNodeId parentId)
    {
        const MarkdownNode* textTemplate = firstTextChild(document, container);
        if (!textTemplate) {
            return 0;
        }
        return appendTextNode(*textTemplate, literal, parentId);
    }

    void tombstoneNode(int nodeIndex)
    {
        MarkdownNode& node = document.m_nodes[nodeIndex];
        node.type = MarkdownNodeType::Unknown;
        node.parent = 0;
        node.children.clear();
        node.inlineTokens.clear();
        node.literal.clear();
    }

    void replaceParentChildren(int parentIndex, int firstChildIndex, int childCount, const QVector<MarkdownNodeId>& replacements)
    {
        MarkdownNode& parent = document.m_nodes[parentIndex];
        parent.children.remove(firstChildIndex, childCount);
        for (int i = 0; i < replacements.size(); ++i) {
            parent.children.insert(firstChildIndex + i, replacements.at(i));
        }
    }

    MarkdownDocument& document;
};

std::optional<StyleCommandHandler::StyleCommandResult> StyleCommandHandler::unwrapStyledSelection(const MarkdownDocument& source,
                                                                                                  int selectionStart,
                                                                                                  int selectionEnd,
                                                                                                  MarkdownNodeType styleType)
{
    MarkdownDocument document = source;
    InlineTransform transform(document);
    for (int i = 0; i < document.m_nodes.size(); ++i) {
        MarkdownNode& styled = document.m_nodes[i];
        if (styled.type != styleType) {
            continue;
        }
        const int styledIndex = i;

        QString literal;
        if (styled.type == MarkdownNodeType::InlineCode) {
            if (!styled.source.isValid() || selectionStart != styled.source.start || selectionEnd != styled.source.end) {
                continue;
            }
            literal = styled.literal;
        } else {
            if (styled.children.size() != 1) {
                continue;
            }

            const auto textIt = document.m_indexById.constFind(styled.children.first());
            if (textIt == document.m_indexById.constEnd()) {
                continue;
            }

            const MarkdownNode& text = document.m_nodes.at(*textIt);
            if (text.type != MarkdownNodeType::Text || !text.source.isValid()
                || selectionStart != text.source.start || selectionEnd != text.source.end) {
                continue;
            }
            literal = text.literal;
        }

        const MarkdownNodeId parentId = styled.parent;
        const auto parentIt = document.m_indexById.constFind(parentId);
        if (parentIt == document.m_indexById.constEnd()) {
            return std::nullopt;
        }

        MarkdownNode& parent = document.m_nodes[*parentIt];
        if (!isContainerParentType(parent.type)) {
            return std::nullopt;
        }

        const int childIndex = parent.children.indexOf(styled.id);
        if (childIndex < 0 || literal.isEmpty()) {
            return std::nullopt;
        }

        const MarkdownNodeId plainTextId = transform.appendTextNode(styled, literal, parentId);
        if (!plainTextId) {
            return std::nullopt;
        }

        const auto refreshedParentIt = document.m_indexById.constFind(parentId);
        if (refreshedParentIt == document.m_indexById.constEnd()) {
            return std::nullopt;
        }
        MarkdownNode& refreshedParent = document.m_nodes[*refreshedParentIt];
        refreshedParent.children[childIndex] = plainTextId;

        transform.tombstoneNode(styledIndex);
        document.rebuildIndex();
        const auto anchorIt = document.m_indexById.constFind(plainTextId);
        if (anchorIt == document.m_indexById.constEnd()) {
            return std::nullopt;
        }

        const int anchorOffsetInNode = literal.size();
        QVector<MarkdownNodeId> affected = {styled.id, plainTextId, parentId};
        return StyleCommandResult{std::move(document), plainTextId, anchorOffsetInNode, std::move(affected)};
    }
    return std::nullopt;
}

std::optional<StyleCommandHandler::StyleCommandResult> StyleCommandHandler::unwrapPartialStyledSelection(const MarkdownDocument& source,
                                                                                                         int selectionStart,
                                                                                                         int selectionEnd,
                                                                                                         MarkdownNodeType styleType)
{
    if (styleType == MarkdownNodeType::InlineCode) {
        return std::nullopt;
    }

    MarkdownDocument document = source;
    InlineTransform transform(document);
    for (int i = 0; i < document.m_nodes.size(); ++i) {
        const MarkdownNode styled = document.m_nodes.at(i);
        if (styled.type != styleType || styled.children.size() != 1) {
            continue;
        }

        const auto textIt = document.m_indexById.constFind(styled.children.first());
        if (textIt == document.m_indexById.constEnd()) {
            continue;
        }

        const int textIndex = *textIt;
        const MarkdownNode text = document.m_nodes.at(textIndex);
        if (text.type != MarkdownNodeType::Text || !text.source.isValid()
            || selectionStart < text.source.start || selectionEnd > text.source.end
            || (selectionStart == text.source.start && selectionEnd == text.source.end)) {
            continue;
        }

        const auto parentIt = document.m_indexById.constFind(styled.parent);
        if (parentIt == document.m_indexById.constEnd()) {
            return std::nullopt;
        }

        const int parentIndex = *parentIt;
        const MarkdownNode parent = document.m_nodes.at(parentIndex);
        if (!isContainerParentType(parent.type)) {
            return std::nullopt;
        }

        const int childIndex = parent.children.indexOf(styled.id);
        if (childIndex < 0) {
            return std::nullopt;
        }

        const int unwrapStart = selectionStart - text.source.start;
        const int unwrapEnd = selectionEnd - text.source.start;
        const QString leftLiteral = text.literal.left(unwrapStart);
        const QString plainLiteral = text.literal.mid(unwrapStart, unwrapEnd - unwrapStart);
        const QString rightLiteral = text.literal.mid(unwrapEnd);
        if (plainLiteral.isEmpty()) {
            return std::nullopt;
        }

        QVector<MarkdownNodeId> replacementChildren;
        MarkdownNodeId plainAnchorNodeId = 0;
        auto appendStyledPart = [&](const QString& literal) {
            if (literal.isEmpty()) {
                return;
            }
            const MarkdownNodeId styleId = document.m_nextId++;
            const MarkdownNodeId textId = transform.appendTextNode(text, literal, styleId);
            if (!textId) {
                return;
            }
            transform.appendStyleNodeWithId(styleId, styled, styled.parent, styleType, {textId});
            replacementChildren.append(styleId);
        };

        appendStyledPart(leftLiteral);
        if (MarkdownNodeId plainId = transform.appendTextNode(text, plainLiteral, styled.parent)) {
            replacementChildren.append(plainId);
            plainAnchorNodeId = plainId;
        }
        appendStyledPart(rightLiteral);

        if (replacementChildren.isEmpty() || !plainAnchorNodeId) {
            return std::nullopt;
        }

        transform.replaceParentChildren(parentIndex, childIndex, 1, replacementChildren);
        transform.tombstoneNode(i);
        transform.tombstoneNode(textIndex);
        document.rebuildIndex();
        const auto anchorIt = document.m_indexById.constFind(plainAnchorNodeId);
        if (anchorIt == document.m_indexById.constEnd()) {
            return std::nullopt;
        }

        const int anchorOffsetInNode = plainLiteral.size();
        QVector<MarkdownNodeId> affected = {styled.id, styled.parent};
        affected.append(replacementChildren);
        return StyleCommandResult{std::move(document), plainAnchorNodeId, anchorOffsetInNode, std::move(affected)};
    }

    return std::nullopt;
}

std::optional<StyleCommandHandler::StyleCommandResult> StyleCommandHandler::ensureOverlappingSelectionStyled(const MarkdownDocument& source,
                                                                                                             int selectionStart,
                                                                                                             int selectionEnd,
                                                                                                             MarkdownNodeType styleType)
{
    if (styleType == MarkdownNodeType::InlineCode) {
        return std::nullopt;
    }

    MarkdownDocument document = source;
    InlineTransform transform(document);
    int parentIndex = -1;
    int startChildIndex = -1;
    int endChildIndex = -1;

    for (int i = 0; i < document.m_nodes.size(); ++i) {
        const MarkdownNode& candidateParent = document.m_nodes.at(i);
        if (!isContainerParentType(candidateParent.type)) {
            continue;
        }

        int candidateStartIndex = -1;
        int candidateEndIndex = -1;
        bool includesSameStyle = false;
        for (int childIndex = 0; childIndex < candidateParent.children.size(); ++childIndex) {
            const MarkdownNode* child = document.nodeById(candidateParent.children.at(childIndex));
            if (!child || !isInlineNodeType(child->type)) {
                continue;
            }

            if (textContentCoversSelectionStart(document, *child, selectionStart)) {
                candidateStartIndex = childIndex;
            }
            if (textContentCoversSelectionEnd(document, *child, selectionEnd)) {
                candidateEndIndex = childIndex;
            }
        }

        if (candidateStartIndex < 0 || candidateEndIndex < candidateStartIndex) {
            continue;
        }

        for (int childIndex = candidateStartIndex; childIndex <= candidateEndIndex; ++childIndex) {
            const MarkdownNode* child = document.nodeById(candidateParent.children.at(childIndex));
            if (!child || !isInlineNodeType(child->type)) {
                includesSameStyle = false;
                break;
            }
            if (child->type == styleType) {
                includesSameStyle = true;
            }
        }

        if (includesSameStyle) {
            parentIndex = i;
            startChildIndex = candidateStartIndex;
            endChildIndex = candidateEndIndex;
            break;
        }
    }

    if (parentIndex < 0 || startChildIndex == endChildIndex) {
        return std::nullopt;
    }

    const MarkdownNode parentTemplate = document.m_nodes.at(parentIndex);
    QVector<MarkdownNodeId> replacementChildren;
    QVector<MarkdownNodeId> styledChildren;
    bool hasLeadingText = false;
    const MarkdownNodeId styleNodeId = document.m_nextId++;
    MarkdownNodeId anchorNodeId = 0;
    int anchorOffsetInNode = -1;

    for (int childIndex = startChildIndex; childIndex <= endChildIndex; ++childIndex) {
        const MarkdownNodeId childId = parentTemplate.children.at(childIndex);
        const auto childIt = document.m_indexById.constFind(childId);
        if (childIt == document.m_indexById.constEnd()) {
            return std::nullopt;
        }

        const int childNodeIndex = *childIt;
        const MarkdownNode childTemplate = document.m_nodes.at(childNodeIndex);
        const MarkdownNode* text = firstTextChild(document, childTemplate);
        if (!text || !text->source.isValid()) {
            return std::nullopt;
        }

        const bool isStart = childIndex == startChildIndex;
        const bool isEnd = childIndex == endChildIndex;

        if (isStart && isEnd) {
            return std::nullopt;
        }

        if (isStart) {
            const int splitOffset = selectionStart - text->source.start;
            if (splitOffset < 0 || splitOffset >= text->literal.size()) {
                return std::nullopt;
            }
            const QString leftLiteral = text->literal.left(splitOffset);
            const QString selectedLiteral = text->literal.mid(splitOffset);

            if (childTemplate.type == MarkdownNodeType::Text) {
                if (MarkdownNodeId leftId = transform.appendTextNode(childTemplate, leftLiteral, parentTemplate.id)) {
                    replacementChildren.append(leftId);
                    hasLeadingText = true;
                }
                if (MarkdownNodeId selectedId = transform.appendTextNode(childTemplate, selectedLiteral, styleNodeId)) {
                    styledChildren.append(selectedId);
                }
            } else if (childTemplate.type == styleType) {
                if (MarkdownNodeId leftId = transform.appendStyledTextPart(childTemplate, leftLiteral, styleNodeId)) {
                    styledChildren.append(leftId);
                }
                if (MarkdownNodeId selectedId = transform.appendStyledTextPart(childTemplate, selectedLiteral, styleNodeId)) {
                    styledChildren.append(selectedId);
                }
            } else {
                return std::nullopt;
            }

            transform.tombstoneNode(childNodeIndex);
            continue;
        }

        if (isEnd) {
            const int splitOffset = selectionEnd - text->source.start;
            if (splitOffset <= 0 || splitOffset > text->literal.size()) {
                return std::nullopt;
            }
            const QString selectedLiteral = text->literal.left(splitOffset);
            const QString rightLiteral = text->literal.mid(splitOffset);

            if (childTemplate.type == MarkdownNodeType::Text) {
                if (MarkdownNodeId selectedId = transform.appendTextNode(childTemplate, selectedLiteral, styleNodeId)) {
                    styledChildren.append(selectedId);
                    anchorNodeId = selectedId;
                    anchorOffsetInNode = selectedLiteral.size();
                }
                if (MarkdownNodeId rightId = transform.appendTextNode(childTemplate, rightLiteral, parentTemplate.id)) {
                    replacementChildren.append(rightId);
                }
            } else if (childTemplate.type == styleType) {
                if (MarkdownNodeId selectedId = transform.appendStyledTextPart(childTemplate, selectedLiteral, styleNodeId)) {
                    styledChildren.append(selectedId);
                    anchorNodeId = selectedId;
                    anchorOffsetInNode = selectedLiteral.size();
                }
                if (MarkdownNodeId rightId = transform.appendStyledTextPart(childTemplate, rightLiteral, styleNodeId)) {
                    styledChildren.append(rightId);
                }
            } else {
                return std::nullopt;
            }

            transform.tombstoneNode(childNodeIndex);
            continue;
        }

        if (childTemplate.type == styleType) {
            for (MarkdownNodeId grandchildId : childTemplate.children) {
                const auto grandchildIt = document.m_indexById.constFind(grandchildId);
                if (grandchildIt == document.m_indexById.constEnd()) {
                    return std::nullopt;
                }
                document.m_nodes[*grandchildIt].parent = styleNodeId;
                clearGeneratedSource(document.m_nodes[*grandchildIt]);
                styledChildren.append(grandchildId);
                anchorNodeId = grandchildId;
                anchorOffsetInNode = document.m_nodes.at(*grandchildIt).literal.size();
            }
            transform.tombstoneNode(childNodeIndex);
        } else {
            document.m_nodes[childNodeIndex].parent = styleNodeId;
            clearGeneratedSource(document.m_nodes[childNodeIndex]);
            styledChildren.append(childTemplate.id);
            if (const MarkdownNode* anchorText = firstTextChild(document, document.m_nodes.at(childNodeIndex))) {
                anchorNodeId = anchorText->id;
                anchorOffsetInNode = anchorText->literal.size();
            }
        }
    }

    if (styledChildren.isEmpty() || !anchorNodeId || anchorOffsetInNode < 0) {
        return std::nullopt;
    }

    transform.appendStyleNodeWithId(styleNodeId, parentTemplate, parentTemplate.id, styleType, styledChildren);
    replacementChildren.insert(hasLeadingText ? 1 : 0, styleNodeId);

    transform.replaceParentChildren(parentIndex, startChildIndex, endChildIndex - startChildIndex + 1, replacementChildren);
    document.rebuildIndex();
    const auto anchorIt = document.m_indexById.constFind(anchorNodeId);
    if (anchorIt == document.m_indexById.constEnd()) {
        return std::nullopt;
    }

    QVector<MarkdownNodeId> affected = {parentTemplate.id, styleNodeId};
    affected.append(replacementChildren);
    return StyleCommandResult{std::move(document), anchorNodeId, anchorOffsetInNode, std::move(affected)};
}

std::optional<StyleCommandHandler::StyleCommandResult> StyleCommandHandler::wrapSelectionAcrossInlineSiblings(const MarkdownDocument& source,
                                                                                                              int selectionStart,
                                                                                                              int selectionEnd,
                                                                                                              MarkdownNodeType styleType)
{
    MarkdownDocument document = source;
    InlineTransform transform(document);
    MarkdownNode* parent = nullptr;
    int parentIndex = -1;
    int startChildIndex = -1;
    int endChildIndex = -1;

    for (int i = 0; i < document.m_nodes.size(); ++i) {
        MarkdownNode& candidateParent = document.m_nodes[i];
        if (!isContainerParentType(candidateParent.type)) {
            continue;
        }

        int candidateStartIndex = -1;
        int candidateEndIndex = -1;
        for (int childIndex = 0; childIndex < candidateParent.children.size(); ++childIndex) {
            const auto childIt = document.m_indexById.constFind(candidateParent.children.at(childIndex));
            if (childIt == document.m_indexById.constEnd()) {
                continue;
            }

            const MarkdownNode& child = document.m_nodes.at(*childIt);
            if (!isInlineNodeType(child.type)) {
                continue;
            }
            if (inlineNodeCoversSelectionStart(child, selectionStart)) {
                candidateStartIndex = childIndex;
            }
            if (inlineNodeCoversSelectionEnd(child, selectionEnd)) {
                candidateEndIndex = childIndex;
            }
        }

        if (candidateStartIndex >= 0 && candidateEndIndex >= candidateStartIndex) {
            parent = &candidateParent;
            parentIndex = i;
            startChildIndex = candidateStartIndex;
            endChildIndex = candidateEndIndex;
            break;
        }
    }

    if (!parent || parentIndex < 0 || startChildIndex == endChildIndex) {
        return std::nullopt;
    }

    const MarkdownNode parentTemplate = *parent;
    QVector<MarkdownNodeId> replacementChildren;
    QVector<MarkdownNodeId> wrappedChildren;
    bool hasLeadingText = false;
    const MarkdownNodeId styleNodeId = document.m_nextId++;
    MarkdownNodeId anchorNodeId = 0;
    int anchorOffsetInNode = -1;

    for (int childIndex = startChildIndex; childIndex <= endChildIndex; ++childIndex) {
        const MarkdownNodeId childId = parentTemplate.children.at(childIndex);
        const auto childIt = document.m_indexById.constFind(childId);
        if (childIt == document.m_indexById.constEnd()) {
            return std::nullopt;
        }

        const int childNodeIndex = *childIt;
        const MarkdownNode childTemplate = document.m_nodes.at(childNodeIndex);
        MarkdownNode& child = document.m_nodes[childNodeIndex];
        if (!isInlineNodeType(child.type) || !child.source.isValid()) {
            return std::nullopt;
        }

        if (childIndex == startChildIndex) {
            if (childTemplate.type != MarkdownNodeType::Text) {
                return std::nullopt;
            }

            const int splitOffset = selectionStart - childTemplate.source.start;
            if (splitOffset < 0 || splitOffset >= childTemplate.literal.size()) {
                return std::nullopt;
            }

            const QString leftLiteral = childTemplate.literal.left(splitOffset);
            const QString selectedLiteral = childTemplate.literal.mid(splitOffset);
            if (MarkdownNodeId leftId = transform.appendTextNode(childTemplate, leftLiteral, parentTemplate.id)) {
                replacementChildren.append(leftId);
                hasLeadingText = true;
            }
            if (MarkdownNodeId selectedId = transform.appendTextNode(childTemplate, selectedLiteral, styleNodeId)) {
                wrappedChildren.append(selectedId);
            }

            transform.tombstoneNode(childNodeIndex);
            continue;
        }

        if (childIndex == endChildIndex) {
            if (childTemplate.type != MarkdownNodeType::Text) {
                return std::nullopt;
            }

            const int splitOffset = selectionEnd - childTemplate.source.start;
            if (splitOffset <= 0 || splitOffset > childTemplate.literal.size()) {
                return std::nullopt;
            }

            const QString selectedLiteral = childTemplate.literal.left(splitOffset);
            const QString rightLiteral = childTemplate.literal.mid(splitOffset);
            if (MarkdownNodeId selectedId = transform.appendTextNode(childTemplate, selectedLiteral, styleNodeId)) {
                wrappedChildren.append(selectedId);
                anchorNodeId = selectedId;
                anchorOffsetInNode = selectedLiteral.size();
            }
            if (MarkdownNodeId rightId = transform.appendTextNode(childTemplate, rightLiteral, parentTemplate.id)) {
                replacementChildren.append(rightId);
            }

            transform.tombstoneNode(childNodeIndex);
            continue;
        }

        child.parent = styleNodeId;
        clearGeneratedSource(child);
        wrappedChildren.append(child.id);
    }

    if (wrappedChildren.isEmpty()) {
        return std::nullopt;
    }

    if (styleType == MarkdownNodeType::InlineCode) {
        return std::nullopt;
    }

    transform.appendStyleNodeWithId(styleNodeId, parentTemplate, parentTemplate.id, styleType, wrappedChildren);
    replacementChildren.insert(hasLeadingText ? 1 : 0, styleNodeId);

    transform.replaceParentChildren(parentIndex, startChildIndex, endChildIndex - startChildIndex + 1, replacementChildren);
    document.rebuildIndex();
    if (!anchorNodeId || anchorOffsetInNode < 0) {
        return std::nullopt;
    }
    const auto anchorIt = document.m_indexById.constFind(anchorNodeId);
    if (anchorIt == document.m_indexById.constEnd()) {
        return std::nullopt;
    }

    QVector<MarkdownNodeId> affected = {parentTemplate.id, styleNodeId};
    affected.append(replacementChildren);
    return StyleCommandResult{std::move(document), anchorNodeId, anchorOffsetInNode, std::move(affected)};
}

std::optional<StyleCommandHandler::StyleCommandResult> StyleCommandHandler::toggleInlineStyleWithSelection(const MarkdownDocument& source,
                                                                                                           SourceSelection selection,
                                                                                                           InlineStyle style)
{
    const int selectionStart = selection.normalizedStart();
    const int selectionEnd = selection.normalizedEnd();
    if (selectionStart < 0 || selectionEnd <= selectionStart || selectionEnd > source.source().size()) {
        return std::nullopt;
    }

    if (std::optional<StyleCommandResult> unwrapped = unwrapStyledSelection(source,
                                                                            selectionStart,
                                                                            selectionEnd,
                                                                            nodeTypeForStyle(style))) {
        return unwrapped;
    }

    if (std::optional<StyleCommandResult> partiallyUnwrapped = unwrapPartialStyledSelection(source,
                                                                                            selectionStart,
                                                                                            selectionEnd,
                                                                                            nodeTypeForStyle(style))) {
        return partiallyUnwrapped;
    }

    if (std::optional<StyleCommandResult> ensured = ensureOverlappingSelectionStyled(source,
                                                                                     selectionStart,
                                                                                     selectionEnd,
                                                                                     nodeTypeForStyle(style))) {
        return ensured;
    }

    if (std::optional<StyleCommandResult> wrapped = wrapSelectionAcrossInlineSiblings(source,
                                                                                      selectionStart,
                                                                                      selectionEnd,
                                                                                      nodeTypeForStyle(style))) {
        return wrapped;
    }

    MarkdownDocument document = source;
    InlineTransform transform(document);
    MarkdownNode* text = nullptr;
    int textIndex = -1;
    for (int i = 0; i < document.m_nodes.size(); ++i) {
        MarkdownNode& candidate = document.m_nodes[i];
        if (candidate.type != MarkdownNodeType::Text || !candidate.source.isValid()) {
            continue;
        }
        if (selectionStart >= candidate.source.start && selectionEnd <= candidate.source.end) {
            text = &candidate;
            textIndex = i;
            break;
        }
    }
    if (!text || textIndex < 0) {
        return std::nullopt;
    }

    const auto parentIt = document.m_indexById.constFind(text->parent);
    if (parentIt == document.m_indexById.constEnd()) {
        return std::nullopt;
    }

    const MarkdownNodeId parentId = text->parent;
    const MarkdownNodeId textId = text->id;
    MarkdownNode& parent = document.m_nodes[*parentIt];
    if (!isContainerParentType(parent.type)) {
        return std::nullopt;
    }

    const int childIndex = parent.children.indexOf(text->id);
    if (childIndex < 0) {
        return std::nullopt;
    }

    const MarkdownNode textTemplate = *text;
    const int wrapStart = selectionStart - text->source.start;
    const int wrapEnd = selectionEnd - text->source.start;
    const QString leftLiteral = text->literal.left(wrapStart);
    const QString selectedLiteral = text->literal.mid(wrapStart, wrapEnd - wrapStart);
    const QString rightLiteral = text->literal.mid(wrapEnd);
    if (selectedLiteral.isEmpty()) {
        return std::nullopt;
    }

    QVector<MarkdownNodeId> replacementChildren;
    if (MarkdownNodeId leftId = transform.appendTextNode(textTemplate, leftLiteral, parentId)) {
        replacementChildren.append(leftId);
    }

    const MarkdownNodeType styleType = nodeTypeForStyle(style);
    const MarkdownNodeId styleNodeId = document.m_nextId++;
    MarkdownNodeId anchorNodeId = 0;
    if (styleType == MarkdownNodeType::InlineCode) {
        transform.appendStyleNodeWithId(styleNodeId, textTemplate, parentId, styleType, {}, selectedLiteral);
        anchorNodeId = styleNodeId;
    } else {
        const MarkdownNodeId styledTextId = transform.appendTextNode(textTemplate, selectedLiteral, styleNodeId);
        if (!styledTextId) {
            return std::nullopt;
        }
        transform.appendStyleNodeWithId(styleNodeId, textTemplate, parentId, styleType, {styledTextId});
        anchorNodeId = styledTextId;
    }
    replacementChildren.append(styleNodeId);

    if (MarkdownNodeId rightId = transform.appendTextNode(textTemplate, rightLiteral, parentId)) {
        replacementChildren.append(rightId);
    }

    const auto refreshedParentIt = document.m_indexById.constFind(parentId);
    if (refreshedParentIt == document.m_indexById.constEnd()) {
        return std::nullopt;
    }
    transform.replaceParentChildren(*refreshedParentIt, childIndex, 1, replacementChildren);

    transform.tombstoneNode(textIndex);
    document.rebuildIndex();
    const auto anchorIt = document.m_indexById.constFind(anchorNodeId);
    if (anchorIt == document.m_indexById.constEnd()) {
        return std::nullopt;
    }

    const int anchorOffsetInNode = selectedLiteral.size();
    QVector<MarkdownNodeId> affected = {textId, parentId};
    affected.append(replacementChildren);
    for (MarkdownNodeId childId : replacementChildren) {
        const MarkdownNode* child = document.nodeById(childId);
        if (child) {
            affected.append(child->children);
        }
    }
    return StyleCommandResult{std::move(document), anchorNodeId, anchorOffsetInNode, std::move(affected)};
}

std::optional<MarkdownDocument> StyleCommandHandler::toggleInlineStyle(const MarkdownDocument& source,
                                                                       SourceSelection selection,
                                                                       InlineStyle style)
{
    std::optional<StyleCommandResult> result = toggleInlineStyleWithSelection(source, selection, style);
    if (!result) {
        return std::nullopt;
    }
    return std::move(result->document);
}

} // namespace Muffin
