#include "editor/EditTransactionValidator.h"

#include <QTest>

using namespace Muffin;

namespace {

NodeSnapshot snapshot(MarkdownNodeId nodeId,
                      MarkdownNodeType type = MarkdownNodeType::Text,
                      MarkdownNodeId parentId = 1,
                      const QString& literal = {})
{
    NodeSnapshot result;
    result.nodeId = nodeId;
    result.type = type;
    result.parentId = parentId;
    result.literal = literal;
    return result;
}

} // namespace

class TestEditTransactionValidator : public QObject
{
    Q_OBJECT

private slots:
    void acceptsUpdateLiteral()
    {
        EditTransaction transaction;
        transaction.affectedNodeIds = {2};
        EditOperation operation;
        operation.kind = EditOperationKind::UpdateLiteral;
        operation.nodeId = 2;
        operation.beforeNode = snapshot(2, MarkdownNodeType::Text, 1, QStringLiteral("Hello"));
        operation.afterNode = snapshot(2, MarkdownNodeType::Text, 1, QStringLiteral("Hello Qt"));
        transaction.operations = {operation};

        EditTransactionValidationResult result = EditTransactionValidator::validate(transaction);

        QVERIFY2(result.ok, qPrintable(result.errors.join(QStringLiteral("; "))));
    }

    void acceptsMoveNode()
    {
        EditTransaction transaction;
        transaction.affectedNodeIds = {2};
        EditOperation operation;
        operation.kind = EditOperationKind::MoveNode;
        operation.nodeId = 2;
        operation.beforeNode = snapshot(2, MarkdownNodeType::ListItem, 10);
        operation.afterNode = snapshot(2, MarkdownNodeType::ListItem, 20);
        transaction.operations = {operation};

        EditTransactionValidationResult result = EditTransactionValidator::validate(transaction);

        QVERIFY2(result.ok, qPrintable(result.errors.join(QStringLiteral("; "))));
    }

    void rejectsBadUpdateLiteral()
    {
        EditTransaction transaction;
        transaction.affectedNodeIds = {2};
        EditOperation operation;
        operation.kind = EditOperationKind::UpdateLiteral;
        operation.nodeId = 2;
        operation.beforeNode = snapshot(2, MarkdownNodeType::Text, 1, QStringLiteral("Hello"));
        operation.afterNode = snapshot(2, MarkdownNodeType::Text, 3, QStringLiteral("Hello"));
        transaction.operations = {operation};

        EditTransactionValidationResult result = EditTransactionValidator::validate(transaction);

        QVERIFY(!result.ok);
        QVERIFY(result.errors.join(QStringLiteral("; ")).contains(QStringLiteral("literal did not change")));
    }

    void rejectsDuplicateSnapshots()
    {
        EditTransaction transaction;
        transaction.beforeNodes = {
            snapshot(2, MarkdownNodeType::Text, 1, QStringLiteral("A")),
            snapshot(2, MarkdownNodeType::Text, 1, QStringLiteral("B"))
        };

        EditTransactionValidationResult result = EditTransactionValidator::validate(transaction);

        QVERIFY(!result.ok);
        QVERIFY(result.errors.join(QStringLiteral("; ")).contains(QStringLiteral("Duplicate before node snapshot")));
    }

    void rejectsOperationOutsideAffectedNodes()
    {
        EditTransaction transaction;
        transaction.affectedNodeIds = {3};
        EditOperation operation;
        operation.kind = EditOperationKind::UpdateLiteral;
        operation.nodeId = 2;
        operation.beforeNode = snapshot(2, MarkdownNodeType::Text, 1, QStringLiteral("Hello"));
        operation.afterNode = snapshot(2, MarkdownNodeType::Text, 1, QStringLiteral("Hello Qt"));
        transaction.operations = {operation};

        EditTransactionValidationResult result = EditTransactionValidator::validate(transaction);

        QVERIFY(!result.ok);
        QVERIFY(result.errors.join(QStringLiteral("; ")).contains(QStringLiteral("not affected")));
    }
};

QTEST_MAIN(TestEditTransactionValidator)
#include "test_edit_transaction_validator.moc"
