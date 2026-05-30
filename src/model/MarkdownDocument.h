#pragma once

#include "model/MarkdownNode.h"
#include "parser/AstTree.h"
#include "parser/MathSpan.h"
#include "parser/SourceCoordinateMapper.h"

#include <QHash>
#include <QString>
#include <QVector>

namespace Muffin {

class MarkdownTransform;
class MarkdownSourceSpanUpdater;
class StyleCommandHandler;
class HeadingCommandHandler;
class ListCommandHandler;
class QuoteCommandHandler;
class QuoteCommandHandlerPrivate;
class BlockDocumentMerger;

class MarkdownDocument {
public:
    MarkdownDocument() = default;

    static MarkdownDocument fromAst(const AstTree& tree, const QString& source, const QVector<MathSpan>& mathSpans = {});
    static MarkdownDocument fromAst(const AstTree& tree, const QString& source, const QVector<MathSpan>& mathSpans,
                                    const MarkdownDocument& previous);

    bool isEmpty() const { return m_nodes.isEmpty(); }
    const QString& source() const { return m_source; }
    MarkdownNodeId rootId() const { return m_rootId; }
    const QVector<MarkdownNode>& nodes() const { return m_nodes; }

    const MarkdownNode* nodeById(MarkdownNodeId id) const;
    const MarkdownNode* nodeAtSourceOffset(int offset) const;
    const MarkdownNode* blockAtSourceOffset(int offset) const;
    QVector<MarkdownNodeId> nodeIdsInSourceSpan(SourceSpan span) const;

private:
    friend class MarkdownTransform;
    friend class MarkdownSourceSpanUpdater;
    friend class StyleCommandHandler;
    friend class HeadingCommandHandler;
    friend class ListCommandHandler;
    friend class QuoteCommandHandler;
    friend class QuoteCommandHandlerPrivate;
    friend class BlockDocumentMerger;

    MarkdownNodeId appendNode(const AstNode& astNode, MarkdownNodeId parent, SourceCoordinateMapper& mapper);
    void appendChildren(const AstNode& astNode, MarkdownNodeId parent, SourceCoordinateMapper& mapper);
    void applyMathSpans(const QVector<MathSpan>& mathSpans);
    MarkdownNodeId appendFormulaNode(const MathSpan& span, MarkdownNodeId parent, SourceRange sourceRange);
    MarkdownNodeId appendTextNode(SourceSpan source, SourceRange sourceRange, MarkdownNodeId parent);
    void reuseNodeIdsFrom(const MarkdownDocument& previous);
    void rebuildIndex();

    QString m_source;
    MarkdownNodeId m_rootId = 0;
    MarkdownNodeId m_nextId = 1;
    QVector<MarkdownNode> m_nodes;
    QHash<MarkdownNodeId, int> m_indexById;
};

} // namespace Muffin
