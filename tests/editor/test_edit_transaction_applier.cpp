#include "editor/EditTransactionApplier.h"
#include "model/MarkdownSerializer.h"
#include "model/MarkdownTransform.h"
#include "parser/CmarkParser.h"

#include <QTest>

using namespace Muffin;

namespace {

const MarkdownNode* firstNodeOfType(const MarkdownDocument& document, MarkdownNodeType type)
{
    for (const MarkdownNode& node : document.nodes()) {
        if (node.type == type) {
            return &node;
        }
    }
    return nullptr;
}

NodeSnapshot snapshotForNode(const MarkdownNode& node, const QString& literal)
{
    NodeSnapshot snapshot;
    snapshot.nodeId = node.id;
    snapshot.type = node.type;
    snapshot.source = node.source;
    snapshot.content = node.content;
    snapshot.parentId = node.parent;
    snapshot.literal = literal;
    return snapshot;
}

NodeSnapshot snapshotForNode(const MarkdownNode& node)
{
    return snapshotForNode(node, node.literal);
}

QVector<MarkdownNodeId> listItemIds(const MarkdownDocument& document)
{
    QVector<MarkdownNodeId> ids;
    for (const MarkdownNode& node : document.nodes()) {
        if (node.type == MarkdownNodeType::ListItem) {
            ids.append(node.id);
        }
    }
    return ids;
}

EditTransaction listMoveTransaction(const MarkdownDocument& beforeDocument,
                                    const MarkdownDocument& afterDocument,
                                    EditTransactionKind kind,
                                    const QVector<MarkdownNodeId>& itemIds,
                                    const QString& beforeMarkdown,
                                    const QString& afterMarkdown)
{
    EditTransaction transaction;
    transaction.kind = kind;
    transaction.affectedNodeIds = itemIds;
    transaction.beforeMarkdown = beforeMarkdown;
    transaction.afterMarkdown = afterMarkdown;
    for (MarkdownNodeId itemId : itemIds) {
        const MarkdownNode* before = beforeDocument.nodeById(itemId);
        const MarkdownNode* after = afterDocument.nodeById(itemId);
        Q_ASSERT(before);
        Q_ASSERT(after);
        EditOperation operation;
        operation.kind = EditOperationKind::MoveNode;
        operation.nodeId = itemId;
        operation.beforeNode = snapshotForNode(*before);
        operation.afterNode = snapshotForNode(*after);
        transaction.beforeNodes.append(operation.beforeNode);
        transaction.afterNodes.append(operation.afterNode);
        transaction.operations.append(operation);
    }
    return transaction;
}

EditTransaction updateLiteralTransaction(const MarkdownDocument& document,
                                         const QString& beforeLiteral,
                                         const QString& afterLiteral,
                                         const QString& beforeMarkdown,
                                         const QString& afterMarkdown)
{
    const MarkdownNode* text = firstNodeOfType(document, MarkdownNodeType::Text);
    Q_ASSERT(text);

    EditOperation operation;
    operation.kind = EditOperationKind::UpdateLiteral;
    operation.nodeId = text->id;
    operation.beforeNode = snapshotForNode(*text, beforeLiteral);
    operation.afterNode = snapshotForNode(*text, afterLiteral);

    EditTransaction transaction;
    transaction.kind = EditTransactionKind::RenderedEdit;
    transaction.affectedNodeIds = {text->id};
    transaction.beforeNodes = {operation.beforeNode};
    transaction.afterNodes = {operation.afterNode};
    transaction.operations = {operation};
    transaction.beforeMarkdown = beforeMarkdown;
    transaction.afterMarkdown = afterMarkdown;
    return transaction;
}

} // namespace

class TestEditTransactionApplier : public QObject
{
    Q_OBJECT

private slots:
    void appliesUpdateLiteralForward()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello"));
        EditTransaction transaction = updateLiteralTransaction(parsed.document,
                                                               QStringLiteral("Hello"),
                                                               QStringLiteral("Hello Qt"),
                                                               QStringLiteral("Hello"),
                                                               QStringLiteral("Hello Qt"));

        EditTransactionApplyResult result = EditTransactionApplier::applyForwardDryRun(parsed.document, transaction);

        QVERIFY2(result.ok, qPrintable(result.errors.join(QStringLiteral("; "))));
        QVERIFY(result.matchesExpectedMarkdown);
        QCOMPARE(result.markdown, QStringLiteral("Hello Qt"));
    }

    void appliesUpdateLiteralReverse()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello"));
        EditTransaction transaction = updateLiteralTransaction(parsed.document,
                                                               QStringLiteral("Hello"),
                                                               QStringLiteral("Hello Qt"),
                                                               QStringLiteral("Hello"),
                                                               QStringLiteral("Hello Qt"));
        EditTransactionApplyResult forward = EditTransactionApplier::applyForwardDryRun(parsed.document, transaction);
        QVERIFY2(forward.ok, qPrintable(forward.errors.join(QStringLiteral("; "))));

        EditTransactionApplyResult reverse = EditTransactionApplier::applyReverseDryRun(forward.document, transaction);

        QVERIFY2(reverse.ok, qPrintable(reverse.errors.join(QStringLiteral("; "))));
        QVERIFY(reverse.matchesExpectedMarkdown);
        QCOMPARE(reverse.markdown, QStringLiteral("Hello"));
    }

    void rejectsLiteralMismatch()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello"));
        EditTransaction transaction = updateLiteralTransaction(parsed.document,
                                                               QStringLiteral("Different"),
                                                               QStringLiteral("Hello Qt"),
                                                               QStringLiteral("Hello"),
                                                               QStringLiteral("Hello Qt"));

        EditTransactionApplyResult result = EditTransactionApplier::applyForwardDryRun(parsed.document, transaction);

        QVERIFY(!result.ok);
        QVERIFY(result.errors.join(QStringLiteral("; ")).contains(QStringLiteral("literal mismatch")));
    }

    void rejectsInsertNodeWithExistingCurrentSnapshot()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello"));
        EditTransaction transaction = updateLiteralTransaction(parsed.document,
                                                               QStringLiteral("Hello"),
                                                               QStringLiteral("Hello Qt"),
                                                               QStringLiteral("Hello"),
                                                               QStringLiteral("Hello Qt"));
        transaction.operations.first().kind = EditOperationKind::InsertNode;

        EditTransactionApplyResult result = EditTransactionApplier::applyForwardDryRun(parsed.document, transaction);

        QVERIFY(!result.ok);
    }

    void appliesListIndentMoveNodeForward()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- A\n- B\n- C"));
        const QVector<MarkdownNodeId> items = listItemIds(parsed.document);
        QCOMPARE(items.size(), 3);
        const QVector<MarkdownNodeId> movedItems{items.at(1), items.at(2)};
        const MarkdownDocument afterDocument = MarkdownTransform::demoteListItems(parsed.document, movedItems);
        MarkdownSerializer serializer;
        const QString afterMarkdown = serializer.serializeDocument(afterDocument);
        EditTransaction transaction = listMoveTransaction(parsed.document,
                                                          afterDocument,
                                                          EditTransactionKind::ListIndent,
                                                          movedItems,
                                                          QStringLiteral("- A\n- B\n- C"),
                                                          afterMarkdown);

        EditTransactionApplyResult result = EditTransactionApplier::applyForwardDryRun(parsed.document, transaction);

        QVERIFY2(result.ok, qPrintable(result.errors.join(QStringLiteral("; "))));
        QVERIFY(result.matchesExpectedMarkdown);
        QCOMPARE(result.markdown, QStringLiteral("- A\n  - B\n  - C"));
    }

    void appliesListIndentMoveNodeReverse()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- A\n- B\n- C"));
        const QVector<MarkdownNodeId> items = listItemIds(parsed.document);
        const QVector<MarkdownNodeId> movedItems{items.at(1), items.at(2)};
        const MarkdownDocument afterDocument = MarkdownTransform::demoteListItems(parsed.document, movedItems);
        MarkdownSerializer serializer;
        EditTransaction transaction = listMoveTransaction(parsed.document,
                                                          afterDocument,
                                                          EditTransactionKind::ListIndent,
                                                          movedItems,
                                                          QStringLiteral("- A\n- B\n- C"),
                                                          serializer.serializeDocument(afterDocument));

        EditTransactionApplyResult result = EditTransactionApplier::applyReverseDryRun(afterDocument, transaction);

        QVERIFY2(result.ok, qPrintable(result.errors.join(QStringLiteral("; "))));
        QVERIFY(result.matchesExpectedMarkdown);
        QCOMPARE(result.markdown, QStringLiteral("- A\n- B\n- C"));
    }

    void appliesListOutdentMoveNodeForward()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- A\n  - B\n  - C"));
        const QVector<MarkdownNodeId> items = listItemIds(parsed.document);
        QCOMPARE(items.size(), 3);
        const QVector<MarkdownNodeId> movedItems{items.at(1), items.at(2)};
        const MarkdownDocument afterDocument = MarkdownTransform::promoteListItems(parsed.document, movedItems);
        MarkdownSerializer serializer;
        EditTransaction transaction = listMoveTransaction(parsed.document,
                                                          afterDocument,
                                                          EditTransactionKind::ListOutdent,
                                                          movedItems,
                                                          QStringLiteral("- A\n  - B\n  - C"),
                                                          serializer.serializeDocument(afterDocument));

        EditTransactionApplyResult result = EditTransactionApplier::applyForwardDryRun(parsed.document, transaction);

        QVERIFY2(result.ok, qPrintable(result.errors.join(QStringLiteral("; "))));
        QVERIFY(result.matchesExpectedMarkdown);
        QCOMPARE(result.markdown, QStringLiteral("- A\n- B\n- C"));
    }

    void rejectsMoveNodeForUnsupportedTransactionKind()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- A\n- B"));
        const QVector<MarkdownNodeId> items = listItemIds(parsed.document);
        const QVector<MarkdownNodeId> movedItems{items.at(1)};
        const MarkdownDocument afterDocument = MarkdownTransform::demoteListItems(parsed.document, movedItems);
        MarkdownSerializer serializer;
        EditTransaction transaction = listMoveTransaction(parsed.document,
                                                          afterDocument,
                                                          EditTransactionKind::RenderedEdit,
                                                          movedItems,
                                                          QStringLiteral("- A\n- B"),
                                                          serializer.serializeDocument(afterDocument));

        EditTransactionApplyResult result = EditTransactionApplier::applyForwardDryRun(parsed.document, transaction);

        QVERIFY(!result.ok);
    }

    void rejectsSerializedMarkdownMismatch()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello"));
        EditTransaction transaction = updateLiteralTransaction(parsed.document,
                                                               QStringLiteral("Hello"),
                                                               QStringLiteral("Hello Qt"),
                                                               QStringLiteral("Hello"),
                                                               QStringLiteral("Unexpected"));

        EditTransactionApplyResult result = EditTransactionApplier::applyForwardDryRun(parsed.document, transaction);

        QVERIFY(!result.ok);
        QVERIFY(!result.matchesExpectedMarkdown);
        QVERIFY(result.errors.join(QStringLiteral("; ")).contains(QStringLiteral("serialized markdown mismatch")));
    }
};

QTEST_MAIN(TestEditTransactionApplier)
#include "test_edit_transaction_applier.moc"
