#include "editor/StructuralUndoController.h"
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

EditTransaction listIndentTransaction(const MarkdownDocument& beforeDocument)
{
    const QVector<MarkdownNodeId> items = listItemIds(beforeDocument);
    Q_ASSERT(items.size() >= 2);
    const QVector<MarkdownNodeId> movedItems{items.at(1)};
    const MarkdownDocument afterDocument = MarkdownTransform::demoteListItems(beforeDocument, movedItems);
    MarkdownSerializer serializer;

    EditTransaction transaction;
    transaction.kind = EditTransactionKind::ListIndent;
    transaction.affectedNodeIds = movedItems;
    transaction.beforeMarkdown = QStringLiteral("- A\n- B");
    transaction.afterMarkdown = serializer.serializeDocument(afterDocument);
    for (MarkdownNodeId itemId : movedItems) {
        const MarkdownNode* before = beforeDocument.nodeById(itemId);
        const MarkdownNode* after = afterDocument.nodeById(itemId);
        Q_ASSERT(before);
        Q_ASSERT(after);
        EditOperation operation;
        operation.kind = EditOperationKind::MoveNode;
        operation.nodeId = itemId;
        operation.beforeNode = snapshotForNode(*before);
        operation.afterNode = snapshotForNode(*after);
        transaction.operations.append(operation);
    }
    return transaction;
}

EditTransaction updateLiteralTransaction(const MarkdownDocument& document)
{
    const MarkdownNode* text = firstNodeOfType(document, MarkdownNodeType::Text);
    Q_ASSERT(text);

    EditOperation operation;
    operation.kind = EditOperationKind::UpdateLiteral;
    operation.nodeId = text->id;
    operation.beforeNode = snapshotForNode(*text, QStringLiteral("Hello"));
    operation.afterNode = snapshotForNode(*text, QStringLiteral("Hello Qt"));

    EditTransaction transaction;
    transaction.affectedNodeIds = {text->id};
    transaction.operations = {operation};
    transaction.beforeMarkdown = QStringLiteral("Hello");
    transaction.afterMarkdown = QStringLiteral("Hello Qt");
    return transaction;
}

EditTransaction formulaUpdateLiteralTransaction(const MarkdownDocument& document, MarkdownNodeType type)
{
    const MarkdownNode* formula = firstNodeOfType(document, type);
    Q_ASSERT(formula);

    EditOperation operation;
    operation.kind = EditOperationKind::UpdateLiteral;
    operation.nodeId = formula->id;
    operation.beforeNode = snapshotForNode(*formula, type == MarkdownNodeType::FormulaInline ? QStringLiteral("x") : QStringLiteral("z"));
    operation.afterNode = snapshotForNode(*formula, type == MarkdownNodeType::FormulaInline ? QStringLiteral("$y$") : QStringLiteral("w"));

    EditTransaction transaction;
    transaction.affectedNodeIds = {formula->id};
    transaction.operations = {operation};
    transaction.beforeMarkdown = type == MarkdownNodeType::FormulaInline ? QStringLiteral("$x$") : QStringLiteral("$$\nz\n$$");
    transaction.afterMarkdown = type == MarkdownNodeType::FormulaInline ? QStringLiteral("$y$") : QStringLiteral("$$\nw\n$$");
    return transaction;
}

EditTransactionValidationResult validResult()
{
    EditTransactionValidationResult validation;
    validation.ok = true;
    return validation;
}

} // namespace

class TestStructuralUndoController : public QObject
{
    Q_OBJECT

private slots:
    void appliesReadyUpdateLiteral()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello"));
        EditTransaction transaction = updateLiteralTransaction(parsed.document);

        StructuralUndoDecision decision = StructuralUndoController::evaluate(parsed.document,
                                                                             transaction,
                                                                             validResult(),
                                                                             EditTransactionApplyDirection::Forward,
                                                                             QStringLiteral("Hello Qt"));

        QVERIFY(decision.structurallyApplicable);
        QVERIFY(decision.dryRunResult.has_value());
        QVERIFY(decision.dryRunResult->ok);
        QVERIFY(decision.shouldApply);
    }

    void appliesInlineFormulaUpdateLiteralStructurally()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("$x$"));
        EditTransaction transaction = formulaUpdateLiteralTransaction(parsed.document, MarkdownNodeType::FormulaInline);

        StructuralUndoDecision decision = StructuralUndoController::evaluate(parsed.document,
                                                                             transaction,
                                                                             validResult(),
                                                                             EditTransactionApplyDirection::Forward,
                                                                             QStringLiteral("$y$"));

        QVERIFY(decision.structurallyApplicable);
        QVERIFY(decision.dryRunResult.has_value());
        QVERIFY2(decision.dryRunResult->ok, qPrintable(decision.dryRunResult->errors.join(QStringLiteral("; "))));
        QCOMPARE(decision.dryRunResult->markdown, QStringLiteral("$y$"));
        QVERIFY(decision.shouldApply);
    }

    void doesNotApplyBlockFormulaUpdateLiteralStructurally()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("$$\nz\n$$"));
        EditTransaction transaction = formulaUpdateLiteralTransaction(parsed.document, MarkdownNodeType::FormulaBlock);

        StructuralUndoDecision decision = StructuralUndoController::evaluate(parsed.document,
                                                                             transaction,
                                                                             validResult(),
                                                                             EditTransactionApplyDirection::Forward,
                                                                             QStringLiteral("$$\nw\n$$"));

        QVERIFY(decision.structurallyApplicable);
        QVERIFY(decision.dryRunResult.has_value());
        QVERIFY(!decision.shouldApply);
    }

    void observesSupportedButNotYetApplicableMoveNode()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello"));
        EditTransaction transaction = updateLiteralTransaction(parsed.document);
        transaction.operations.first().kind = EditOperationKind::MoveNode;

        StructuralUndoDecision decision = StructuralUndoController::evaluate(parsed.document,
                                                                             transaction,
                                                                             validResult(),
                                                                             EditTransactionApplyDirection::Forward,
                                                                             QStringLiteral("Hello Qt"));

        QVERIFY(decision.structurallyApplicable);
        QVERIFY(decision.dryRunResult.has_value());
        QVERIFY(!decision.dryRunResult->ok);
        QVERIFY(!decision.shouldApply);
    }

    void appliesReadyListMoveNode()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("- A\n- B"));
        EditTransaction transaction = listIndentTransaction(parsed.document);

        StructuralUndoDecision decision = StructuralUndoController::evaluate(parsed.document,
                                                                             transaction,
                                                                             validResult(),
                                                                             EditTransactionApplyDirection::Forward,
                                                                             transaction.afterMarkdown);

        QVERIFY(decision.structurallyApplicable);
        QVERIFY(decision.dryRunResult.has_value());
        QVERIFY2(decision.dryRunResult->ok, qPrintable(decision.dryRunResult->errors.join(QStringLiteral("; "))));
        QCOMPARE(decision.dryRunResult->markdown, QStringLiteral("- A\n  - B"));
        QVERIFY(decision.shouldApply);
    }

    void rejectsMissingValidation()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello"));
        EditTransaction transaction = updateLiteralTransaction(parsed.document);

        StructuralUndoDecision decision = StructuralUndoController::evaluate(parsed.document,
                                                                             transaction,
                                                                             {},
                                                                             EditTransactionApplyDirection::Forward,
                                                                             QStringLiteral("Hello Qt"));

        QVERIFY(!decision.structurallyApplicable);
        QVERIFY(!decision.dryRunResult.has_value());
        QVERIFY(!decision.shouldApply);
    }

    void rejectsExpectedMarkdownMismatch()
    {
        CmarkParser parser;
        ParseResult parsed = parser.parseDocument(QStringLiteral("Hello"));
        EditTransaction transaction = updateLiteralTransaction(parsed.document);

        StructuralUndoDecision decision = StructuralUndoController::evaluate(parsed.document,
                                                                             transaction,
                                                                             validResult(),
                                                                             EditTransactionApplyDirection::Forward,
                                                                             QStringLiteral("Unexpected"));

        QVERIFY(decision.structurallyApplicable);
        QVERIFY(decision.dryRunResult.has_value());
        QVERIFY(decision.dryRunResult->ok);
        QVERIFY(!decision.shouldApply);
    }
};

QTEST_MAIN(TestStructuralUndoController)
#include "test_structural_undo_controller.moc"
