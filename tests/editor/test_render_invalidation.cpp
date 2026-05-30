#include "editor/RenderInvalidation.h"

#include <QTest>

using namespace Muffin;

namespace {

NodeSnapshot snapshot(MarkdownNodeId nodeId,
                      MarkdownNodeType type,
                      MarkdownNodeId parentId,
                      MarkdownNodeId previousSiblingId = 0,
                      MarkdownNodeId nextSiblingId = 0,
                      QVector<MarkdownNodeId> ancestorIds = {},
                      const QString& literal = {})
{
    NodeSnapshot result;
    result.nodeId = nodeId;
    result.type = type;
    result.parentId = parentId;
    result.previousSiblingId = previousSiblingId;
    result.nextSiblingId = nextSiblingId;
    result.ancestorIds = std::move(ancestorIds);
    result.literal = literal;
    return result;
}

} // namespace

class TestRenderInvalidation : public QObject
{
    Q_OBJECT

private slots:
    void invalidatesParentBlockForInlineTextEdit()
    {
        EditTransaction transaction;
        transaction.kind = EditTransactionKind::RenderedEdit;
        transaction.affectedNodeIds = {3};
        transaction.beforeNodes = {
            snapshot(3, MarkdownNodeType::Text, 2, 0, 0, {2, 1}, QStringLiteral("Hello world"))
        };
        transaction.afterNodes = {
            snapshot(3, MarkdownNodeType::Text, 2, 0, 0, {2, 1}, QStringLiteral("Hello Qt"))
        };

        const QVector<MarkdownNodeId> invalidated = RenderInvalidation::invalidatedNodeIdsForTransaction(transaction);

        QCOMPARE(invalidated, QVector<MarkdownNodeId>{2});
    }

    void invalidatesListItemForInlineTextEditInsideListItemParagraph()
    {
        EditTransaction transaction;
        transaction.kind = EditTransactionKind::RenderedEdit;
        transaction.affectedNodeIds = {4};
        transaction.beforeNodes = {
            snapshot(4, MarkdownNodeType::Text, 3, 0, 0, {3, 2, 1}, QStringLiteral("old"))
        };
        transaction.afterNodes = {
            snapshot(4, MarkdownNodeType::Text, 3, 0, 0, {3, 2, 1}, QStringLiteral("new"))
        };

        const QVector<MarkdownNodeId> invalidated = RenderInvalidation::invalidatedNodeIdsForTransaction(transaction);

        QCOMPARE(invalidated, QVector<MarkdownNodeId>{2});
    }

    void invalidatesBlockQuoteForInlineTextEditInsideBlockQuoteParagraph()
    {
        EditTransaction transaction;
        transaction.kind = EditTransactionKind::RenderedEdit;
        transaction.affectedNodeIds = {4};
        transaction.beforeNodes = {
            snapshot(4, MarkdownNodeType::Text, 3, 0, 0, {3, 2, 1}, QStringLiteral("old"))
        };
        transaction.afterNodes = {
            snapshot(4, MarkdownNodeType::Text, 3, 0, 0, {3, 2, 1}, QStringLiteral("new"))
        };

        const QVector<MarkdownNodeId> invalidated = RenderInvalidation::invalidatedNodeIdsForTransaction(transaction);

        QCOMPARE(invalidated, QVector<MarkdownNodeId>{2});
    }

    void invalidatesOldAndNewListContextForIndent()
    {
        EditTransaction transaction;
        transaction.kind = EditTransactionKind::ListIndent;
        transaction.affectedNodeIds = {20, 30};
        transaction.beforeNodes = {
            snapshot(20, MarkdownNodeType::ListItem, 10, 15, 30, {10, 1}),
            snapshot(30, MarkdownNodeType::ListItem, 10, 20, 0, {10, 1})
        };
        transaction.afterNodes = {
            snapshot(20, MarkdownNodeType::ListItem, 40, 0, 30, {40, 15, 10, 1}),
            snapshot(30, MarkdownNodeType::ListItem, 40, 20, 0, {40, 15, 10, 1})
        };

        const QVector<MarkdownNodeId> invalidated = RenderInvalidation::invalidatedNodeIdsForTransaction(transaction);

        QCOMPARE(invalidated, QVector<MarkdownNodeId>({10, 15, 30, 20, 40}));
    }

    void invalidatesOldAndNewListContextForOutdent()
    {
        EditTransaction transaction;
        transaction.kind = EditTransactionKind::ListOutdent;
        transaction.affectedNodeIds = {20, 30};
        transaction.beforeNodes = {
            snapshot(20, MarkdownNodeType::ListItem, 40, 0, 30, {40, 15, 10, 1}),
            snapshot(30, MarkdownNodeType::ListItem, 40, 20, 0, {40, 15, 10, 1})
        };
        transaction.afterNodes = {
            snapshot(20, MarkdownNodeType::ListItem, 10, 15, 30, {10, 1}),
            snapshot(30, MarkdownNodeType::ListItem, 10, 20, 0, {10, 1})
        };

        const QVector<MarkdownNodeId> invalidated = RenderInvalidation::invalidatedNodeIdsForTransaction(transaction);

        QCOMPARE(invalidated, QVector<MarkdownNodeId>({40, 30, 20, 10, 15}));
    }

    void fallsBackToAffectedNodesWhenSnapshotsAreMissing()
    {
        EditTransaction transaction;
        transaction.affectedNodeIds = {3, 4};

        const QVector<MarkdownNodeId> invalidated = RenderInvalidation::invalidatedNodeIdsForTransaction(transaction);

        QCOMPARE(invalidated, QVector<MarkdownNodeId>({3, 4}));
    }
};

QTEST_MAIN(TestRenderInvalidation)
#include "test_render_invalidation.moc"
