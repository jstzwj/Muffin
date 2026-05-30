#include "MarkdownDocument.h"

#include <algorithm>
#include <QSet>
#include <utility>

namespace Muffin {

namespace {

int indexOfNode(const QVector<MarkdownNode>& nodes, MarkdownNodeId id)
{
    for (int i = 0; i < nodes.size(); ++i) {
        if (nodes.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

bool isBlockNode(MarkdownNodeType type)
{
    switch (type) {
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
    case MarkdownNodeType::TableRow:
    case MarkdownNodeType::TableCell:
    case MarkdownNodeType::FormulaBlock:
        return true;
    default:
        return false;
    }
}

SourceSpan codeBlockContentSpan(const QString& source, SourceSpan blockSource)
{
    if (!blockSource.isValid()) {
        return {};
    }

    const int openingFenceEnd = source.indexOf(QChar('\n'), blockSource.start);
    if (openingFenceEnd < 0 || openingFenceEnd >= blockSource.end) {
        return {};
    }

    const int closingFenceStart = source.lastIndexOf(QChar('\n'), blockSource.end - 1);
    if (closingFenceStart < openingFenceEnd) {
        return {};
    }
    if (closingFenceStart == openingFenceEnd) {
        return {openingFenceEnd + 1, openingFenceEnd + 1};
    }
    return {openingFenceEnd + 1, closingFenceStart};
}

QString identityKey(const MarkdownNode& node)
{
    return QStringLiteral("%1|%2:%3-%4:%5|%6|%7|%8|%9|%10")
        .arg(markdownNodeTypeName(node.type))
        .arg(node.sourceRange.startLine)
        .arg(node.sourceRange.startColumn)
        .arg(node.sourceRange.endLine)
        .arg(node.sourceRange.endColumn)
        .arg(node.headingLevel)
        .arg(node.literal)
        .arg(node.url)
        .arg(node.title)
        .arg(node.fenceInfo);
}

} // namespace

MarkdownDocument MarkdownDocument::fromAst(const AstTree& tree, const QString& source, const QVector<MathSpan>& mathSpans)
{
    MarkdownDocument document;
    document.m_source = source;

    if (!tree.isNull()) {
        SourceCoordinateMapper mapper(source);
        document.m_rootId = document.appendNode(tree.root(), 0, mapper);
    }

    document.applyMathSpans(mathSpans);
    document.rebuildIndex();
    return document;
}

MarkdownDocument MarkdownDocument::fromAst(const AstTree& tree, const QString& source, const QVector<MathSpan>& mathSpans,
                                           const MarkdownDocument& previous)
{
    MarkdownDocument document = fromAst(tree, source, mathSpans);
    document.reuseNodeIdsFrom(previous);
    document.rebuildIndex();
    return document;
}

const MarkdownNode* MarkdownDocument::nodeById(MarkdownNodeId id) const
{
    const auto it = m_indexById.constFind(id);
    if (it == m_indexById.constEnd()) {
        return nullptr;
    }
    return &m_nodes.at(*it);
}

const MarkdownNode* MarkdownDocument::nodeAtSourceOffset(int offset) const
{
    const MarkdownNode* best = nullptr;
    for (const MarkdownNode& node : m_nodes) {
        if (!node.source.isValid() || !node.source.contains(offset)) {
            continue;
        }
        if (!best || (node.source.end - node.source.start) < (best->source.end - best->source.start)) {
            best = &node;
        }
    }
    return best;
}

const MarkdownNode* MarkdownDocument::blockAtSourceOffset(int offset) const
{
    const MarkdownNode* best = nullptr;
    for (const MarkdownNode& node : m_nodes) {
        if (!isBlockNode(node.type) || !node.source.isValid() || !node.source.contains(offset)) {
            continue;
        }
        if (!best || (node.source.end - node.source.start) < (best->source.end - best->source.start)) {
            best = &node;
        }
    }
    return best;
}

QVector<MarkdownNodeId> MarkdownDocument::nodeIdsInSourceSpan(SourceSpan span) const
{
    QVector<MarkdownNodeId> ids;
    if (!span.isValid()) {
        return ids;
    }

    for (const MarkdownNode& node : m_nodes) {
        if (!node.source.isValid()) {
            continue;
        }
        if (node.source.end >= span.start && node.source.start <= span.end) {
            ids.append(node.id);
        }
    }
    return ids;
}

MarkdownNodeId MarkdownDocument::appendNode(const AstNode& astNode, MarkdownNodeId parent, SourceCoordinateMapper& mapper)
{
    if (astNode.isNull()) {
        return 0;
    }

    MarkdownNode node;
    node.id = m_nextId++;
    node.parent = parent;
    node.type = markdownNodeTypeFromCmark(astNode);
    node.sourceRange = astNode.sourceRange();
    node.source = mapper.spanForRange(node.sourceRange);
    node.content = node.source;
    if (node.type == MarkdownNodeType::CodeBlock) {
        const SourceSpan content = codeBlockContentSpan(m_source, node.source);
        if (content.isValid()) {
            node.content = content;
        }
    }
    node.literal = astNode.literal();
    node.url = astNode.url();
    node.title = astNode.title();
    node.fenceInfo = astNode.fenceInfo();
    node.headingLevel = astNode.headingLevel();
    node.listStart = astNode.listStart();
    node.orderedList = node.type == MarkdownNodeType::List && astNode.listType() == CMARK_ORDERED_LIST;
    node.taskList = node.type == MarkdownNodeType::ListItem && astNode.isTasklistItem();
    node.taskChecked = node.taskList && astNode.isTasklistChecked();
    node.tableHeader = node.type == MarkdownNodeType::TableRow && astNode.isTableRowHeader();

    const MarkdownNodeId id = node.id;
    m_nodes.append(std::move(node));

    appendChildren(astNode, id, mapper);
    return id;
}

void MarkdownDocument::appendChildren(const AstNode& astNode, MarkdownNodeId parent, SourceCoordinateMapper& mapper)
{
    AstNode child = astNode.firstChild();
    while (!child.isNull()) {
        const MarkdownNodeId childId = appendNode(child, parent, mapper);
        if (childId != 0) {
            const int parentIndex = indexOfNode(m_nodes, parent);
            const int childIndex = indexOfNode(m_nodes, childId);
            if (parentIndex >= 0) {
                m_nodes[parentIndex].children.append(childId);

                if (child.isInline() && childIndex >= 0) {
                    MarkdownInlineToken token;
                    token.type = markdownNodeTypeFromCmark(child);
                    token.source = m_nodes.at(childIndex).source;
                    token.literal = child.literal();
                    m_nodes[parentIndex].inlineTokens.append(token);
                }
            }
        }
        child = child.next();
    }
}

void MarkdownDocument::applyMathSpans(const QVector<MathSpan>& mathSpans)
{
    if (mathSpans.isEmpty() || m_nodes.isEmpty()) {
        return;
    }

    QVector<MathSpan> spans = mathSpans;
    std::sort(spans.begin(), spans.end(), [](const MathSpan& lhs, const MathSpan& rhs) {
        return lhs.source.start < rhs.source.start;
    });

    for (const MathSpan& span : spans) {
        if (!span.source.isValid()) {
            continue;
        }

        if (span.display) {
            for (MarkdownNode& node : m_nodes) {
                if (node.type == MarkdownNodeType::Paragraph && node.source.isValid()
                    && span.source.start >= node.source.start && span.source.end <= node.source.end) {
                    node.type = MarkdownNodeType::FormulaBlock;
                    node.source = span.source;
                    node.content = span.content;
                    node.literal = span.tex;
                    node.children.clear();
                    node.inlineTokens.clear();
                    break;
                }
            }
            continue;
        }

        for (int textIndex = 0; textIndex < m_nodes.size(); ++textIndex) {
            MarkdownNode& textNode = m_nodes[textIndex];
            if (textNode.type != MarkdownNodeType::Text || !textNode.source.isValid()
                || span.source.start < textNode.source.start || span.source.end > textNode.source.end) {
                continue;
            }

            const MarkdownNodeId parentId = textNode.parent;
            const int parentIndex = indexOfNode(m_nodes, parentId);
            if (parentIndex < 0) {
                break;
            }

            QVector<MarkdownNodeId> replacementChildren;
            const SourceSpan left{textNode.source.start, span.source.start};
            const SourceSpan right{span.source.end, textNode.source.end};
            const MarkdownNodeId originalTextId = textNode.id;
            const SourceRange textRange = textNode.sourceRange;
            if (left.start < left.end) {
                replacementChildren.append(appendTextNode(left, textRange, parentId));
            }
            replacementChildren.append(appendFormulaNode(span, parentId, textRange));
            if (right.start < right.end) {
                replacementChildren.append(appendTextNode(right, textRange, parentId));
            }

            MarkdownNode& parent = m_nodes[parentIndex];
            const int childIndex = parent.children.indexOf(originalTextId);
            if (childIndex >= 0) {
                parent.children.removeAt(childIndex);
                for (int i = 0; i < replacementChildren.size(); ++i) {
                    parent.children.insert(childIndex + i, replacementChildren.at(i));
                }
            }

            MarkdownNode& originalTextNode = m_nodes[textIndex];
            originalTextNode.type = MarkdownNodeType::Unknown;
            originalTextNode.children.clear();
            originalTextNode.inlineTokens.clear();
            break;
        }
    }
}

MarkdownNodeId MarkdownDocument::appendFormulaNode(const MathSpan& span, MarkdownNodeId parent, SourceRange sourceRange)
{
    MarkdownNode node;
    node.id = m_nextId++;
    node.parent = parent;
    node.type = span.display ? MarkdownNodeType::FormulaBlock : MarkdownNodeType::FormulaInline;
    node.source = span.source;
    node.content = span.content;
    node.sourceRange = sourceRange;
    node.literal = span.tex;

    const MarkdownNodeId id = node.id;
    m_nodes.append(std::move(node));
    return id;
}

MarkdownNodeId MarkdownDocument::appendTextNode(SourceSpan source, SourceRange sourceRange, MarkdownNodeId parent)
{
    MarkdownNode node;
    node.id = m_nextId++;
    node.parent = parent;
    node.type = MarkdownNodeType::Text;
    node.source = source;
    node.content = source;
    node.sourceRange = sourceRange;
    node.literal = m_source.mid(source.start, source.end - source.start);

    const MarkdownNodeId id = node.id;
    m_nodes.append(std::move(node));
    return id;
}

void MarkdownDocument::reuseNodeIdsFrom(const MarkdownDocument& previous)
{
    if (previous.isEmpty() || m_nodes.isEmpty()) {
        return;
    }

    QHash<QString, QVector<MarkdownNodeId>> previousIdsByKey;
    for (const MarkdownNode& node : previous.nodes()) {
        previousIdsByKey[identityKey(node)].append(node.id);
    }

    QHash<MarkdownNodeId, MarkdownNodeId> remappedIds;
    QSet<MarkdownNodeId> usedPreviousIds;
    MarkdownNodeId maxId = 0;

    for (const MarkdownNode& node : m_nodes) {
        const QString key = identityKey(node);
        QVector<MarkdownNodeId>& candidates = previousIdsByKey[key];
        MarkdownNodeId reusedId = 0;
        while (!candidates.isEmpty() && reusedId == 0) {
            const MarkdownNodeId candidate = candidates.takeFirst();
            if (!usedPreviousIds.contains(candidate)) {
                reusedId = candidate;
                usedPreviousIds.insert(candidate);
            }
        }

        const MarkdownNodeId newId = reusedId != 0 ? reusedId : node.id;
        remappedIds.insert(node.id, newId);
        maxId = qMax(maxId, newId);
    }

    for (MarkdownNode& node : m_nodes) {
        node.id = remappedIds.value(node.id, node.id);
        node.parent = remappedIds.value(node.parent, node.parent);
        for (MarkdownNodeId& child : node.children) {
            child = remappedIds.value(child, child);
        }
    }

    m_rootId = remappedIds.value(m_rootId, m_rootId);
    m_nextId = maxId + 1;
}

void MarkdownDocument::rebuildIndex()
{
    m_indexById.clear();
    for (int i = 0; i < m_nodes.size(); ++i) {
        m_indexById.insert(m_nodes.at(i).id, i);
    }
}

} // namespace Muffin
