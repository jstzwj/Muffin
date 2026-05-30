#include "core/Document.h"
#include "editor/EditorSelectionMapper.h"
#include "model/MarkdownSerializer.h"
#include "model/MarkdownTransform.h"
#include "parser/SourceCoordinateMapper.h"
#include "renderer/RenderConsistency.h"

#include <QSignalSpy>
#include <QTest>

using namespace Muffin;

namespace {

NodeSnapshot textSnapshot(MarkdownNodeId nodeId, MarkdownNodeId parentId, const QString& literal)
{
    NodeSnapshot snapshot;
    snapshot.nodeId = nodeId;
    snapshot.type = MarkdownNodeType::Text;
    snapshot.parentId = parentId;
    snapshot.ancestorIds = {parentId, 1};
    snapshot.literal = literal;
    return snapshot;
}

QVector<MarkdownNodeId> ancestorIdsForNode(const MarkdownDocument& document, const MarkdownNode& node)
{
    QVector<MarkdownNodeId> ancestors;
    MarkdownNodeId parentId = node.parent;
    while (parentId != 0) {
        ancestors.append(parentId);
        const MarkdownNode* parent = document.nodeById(parentId);
        if (!parent) {
            break;
        }
        parentId = parent->parent;
    }
    return ancestors;
}

NodeSnapshot textSnapshotForNode(const MarkdownDocument& document, const MarkdownNode& node, const QString& literal)
{
    NodeSnapshot snapshot;
    snapshot.nodeId = node.id;
    snapshot.type = MarkdownNodeType::Text;
    snapshot.parentId = node.parent;
    snapshot.ancestorIds = ancestorIdsForNode(document, node);
    snapshot.literal = literal;
    return snapshot;
}

EditTransaction structurallyApplicableTransaction()
{
    constexpr MarkdownNodeId paragraphId = 1;
    constexpr MarkdownNodeId textId = 2;

    EditOperation operation;
    operation.kind = EditOperationKind::UpdateLiteral;
    operation.nodeId = textId;
    operation.beforeNode = textSnapshot(textId, paragraphId, QStringLiteral("Hello"));
    operation.afterNode = textSnapshot(textId, paragraphId, QStringLiteral("Hello Qt"));

    EditTransaction transaction;
    transaction.kind = EditTransactionKind::RenderedEdit;
    transaction.affectedNodeIds = {textId};
    transaction.beforeNodes = {operation.beforeNode};
    transaction.afterNodes = {operation.afterNode};
    transaction.operations = {operation};
    transaction.beforeMarkdown = QStringLiteral("Hello");
    transaction.afterMarkdown = QStringLiteral("Hello Qt");
    return transaction;
}

const MarkdownNode* firstNodeOfType(const MarkdownDocument& document, MarkdownNodeType type)
{
    for (const MarkdownNode& node : document.nodes()) {
        if (node.type == type) {
            return &node;
        }
    }
    return nullptr;
}

const MarkdownNode* lastTextNodeWithLiteral(const MarkdownDocument& document, const QString& literal)
{
    const MarkdownNode* result = nullptr;
    for (const MarkdownNode& node : document.nodes()) {
        if (node.type == MarkdownNodeType::Text && node.literal == literal) {
            result = &node;
        }
    }
    return result;
}

EditTransaction structurallyApplicableTransactionForDocument(const Document& document)
{
    const MarkdownNode* paragraph = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::Paragraph);
    const MarkdownNode* text = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::Text);
    Q_ASSERT(paragraph);
    Q_ASSERT(text);

    EditOperation operation;
    operation.kind = EditOperationKind::UpdateLiteral;
    operation.nodeId = text->id;
    operation.beforeNode = textSnapshot(text->id, paragraph->id, QStringLiteral("Hello"));
    operation.afterNode = textSnapshot(text->id, paragraph->id, QStringLiteral("Hello Qt"));

    EditTransaction transaction;
    transaction.kind = EditTransactionKind::RenderedEdit;
    transaction.affectedNodeIds = {text->id};
    transaction.beforeNodes = {operation.beforeNode};
    transaction.afterNodes = {operation.afterNode};
    transaction.operations = {operation};
    transaction.beforeMarkdown = QStringLiteral("Hello");
    transaction.afterMarkdown = QStringLiteral("Hello Qt");
    return transaction;
}

EditTransaction paragraphTextUpdateLiteralTransactionForDocument(const Document& document,
                                                                 const QString& beforeLiteral,
                                                                 const QString& afterLiteral,
                                                                 const QString& beforeMarkdown,
                                                                 const QString& afterMarkdown)
{
    const MarkdownNode* text = nullptr;
    const MarkdownNode* paragraph = nullptr;
    for (const MarkdownNode& node : document.markdownDocument().nodes()) {
        if (node.type == MarkdownNodeType::Text && node.literal == beforeLiteral) {
            text = &node;
            paragraph = document.markdownDocument().nodeById(node.parent);
            break;
        }
    }
    Q_ASSERT(text);
    Q_ASSERT(paragraph);

    EditOperation operation;
    operation.kind = EditOperationKind::UpdateLiteral;
    operation.nodeId = text->id;
    operation.beforeNode = textSnapshotForNode(document.markdownDocument(), *text, beforeLiteral);
    operation.afterNode = textSnapshotForNode(document.markdownDocument(), *text, afterLiteral);

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

EditTransaction headingUpdateLiteralTransactionForDocument(const Document& document,
                                                          const QString& beforeLiteral,
                                                          const QString& afterLiteral)
{
    const MarkdownNode* heading = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::Heading);
    const MarkdownNode* text = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::Text);
    Q_ASSERT(heading);
    Q_ASSERT(text);

    EditOperation operation;
    operation.kind = EditOperationKind::UpdateLiteral;
    operation.nodeId = text->id;
    operation.beforeNode = textSnapshot(text->id, heading->id, beforeLiteral);
    operation.afterNode = textSnapshot(text->id, heading->id, afterLiteral);

    EditTransaction transaction;
    transaction.kind = EditTransactionKind::RenderedEdit;
    transaction.affectedNodeIds = {text->id};
    transaction.beforeNodes = {operation.beforeNode};
    transaction.afterNodes = {operation.afterNode};
    transaction.operations = {operation};
    transaction.beforeMarkdown = QStringLiteral("# ") + beforeLiteral;
    transaction.afterMarkdown = QStringLiteral("# ") + afterLiteral;
    return transaction;
}

EditTransaction headingUpdateLiteralTransactionForDocument(const Document& document,
                                                          const QString& beforeLiteral,
                                                          const QString& afterLiteral,
                                                          const QString& beforeMarkdown,
                                                          const QString& afterMarkdown)
{
    const MarkdownNode* heading = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::Heading);
    const MarkdownNode* text = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::Text);
    Q_ASSERT(heading);
    Q_ASSERT(text);

    EditOperation operation;
    operation.kind = EditOperationKind::UpdateLiteral;
    operation.nodeId = text->id;
    operation.beforeNode = textSnapshotForNode(document.markdownDocument(), *text, beforeLiteral);
    operation.afterNode = textSnapshotForNode(document.markdownDocument(), *text, afterLiteral);

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

NodeSnapshot nodeSnapshot(const MarkdownNode& node)
{
    NodeSnapshot snapshot;
    snapshot.nodeId = node.id;
    snapshot.type = node.type;
    snapshot.source = node.source;
    snapshot.content = node.content;
    snapshot.parentId = node.parent;
    snapshot.previousSiblingId = 0;
    snapshot.nextSiblingId = 0;
    snapshot.childIds = node.children;
    snapshot.literal = node.literal;
    return snapshot;
}

NodeSnapshot nodeSnapshotWithLiteral(const MarkdownNode& node, const QString& literal)
{
    NodeSnapshot snapshot = nodeSnapshot(node);
    snapshot.literal = literal;
    return snapshot;
}

EditTransaction codeBlockUpdateLiteralTransactionForDocument(const Document& document,
                                                            const QString& beforeLiteral,
                                                            const QString& afterLiteral)
{
    const MarkdownNode* code = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::CodeBlock);
    Q_ASSERT(code);

    EditOperation operation;
    operation.kind = EditOperationKind::UpdateLiteral;
    operation.nodeId = code->id;
    operation.beforeNode = nodeSnapshotWithLiteral(*code, beforeLiteral);
    operation.afterNode = nodeSnapshotWithLiteral(*code, afterLiteral);

    EditTransaction transaction;
    transaction.kind = EditTransactionKind::RenderedEdit;
    transaction.affectedNodeIds = {code->id};
    transaction.beforeNodes = {operation.beforeNode};
    transaction.afterNodes = {operation.afterNode};
    transaction.operations = {operation};
    transaction.beforeMarkdown = QStringLiteral("```\n") + beforeLiteral + QStringLiteral("```");
    transaction.afterMarkdown = QStringLiteral("```\n") + afterLiteral + QStringLiteral("```");
    return transaction;
}

EditTransaction codeBlockUpdateLiteralTransactionForDocument(const Document& document,
                                                            const QString& beforeLiteral,
                                                            const QString& afterLiteral,
                                                            const QString& beforeMarkdown,
                                                            const QString& afterMarkdown,
                                                            SourceSelection beforeSelection,
                                                            SourceSelection afterSelection)
{
    const MarkdownNode* code = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::CodeBlock);
    Q_ASSERT(code);

    EditOperation operation;
    operation.kind = EditOperationKind::UpdateLiteral;
    operation.nodeId = code->id;
    operation.beforeNode = nodeSnapshotWithLiteral(*code, beforeLiteral);
    operation.afterNode = nodeSnapshotWithLiteral(*code, afterLiteral);

    EditTransaction transaction;
    transaction.kind = EditTransactionKind::RenderedEdit;
    transaction.affectedNodeIds = {code->id};
    transaction.beforeNodes = {operation.beforeNode};
    transaction.afterNodes = {operation.afterNode};
    transaction.operations = {operation};
    transaction.beforeMarkdown = beforeMarkdown;
    transaction.afterMarkdown = afterMarkdown;
    transaction.beforeSelection = beforeSelection;
    transaction.afterSelection = afterSelection;
    return transaction;
}

EditTransaction listTextUpdateLiteralTransactionForDocument(const Document& document,
                                                           const QString& beforeLiteral,
                                                           const QString& afterLiteral,
                                                           const QString& beforeMarkdown,
                                                           const QString& afterMarkdown)
{
    const MarkdownNode* text = nullptr;
    const MarkdownNode* paragraph = nullptr;
    const MarkdownNode* item = nullptr;
    for (const MarkdownNode& node : document.markdownDocument().nodes()) {
        if (!text && node.type == MarkdownNodeType::Text && node.literal == beforeLiteral) {
            text = &node;
            paragraph = document.markdownDocument().nodeById(node.parent);
            item = paragraph ? document.markdownDocument().nodeById(paragraph->parent) : nullptr;
            break;
        }
    }
    Q_ASSERT(text);
    Q_ASSERT(paragraph);
    Q_ASSERT(item);

    EditOperation operation;
    operation.kind = EditOperationKind::UpdateLiteral;
    operation.nodeId = text->id;
    operation.beforeNode = textSnapshot(text->id, paragraph->id, beforeLiteral);
    operation.beforeNode.ancestorIds = {paragraph->id, item->id, 1};
    operation.afterNode = textSnapshot(text->id, paragraph->id, afterLiteral);
    operation.afterNode.ancestorIds = {paragraph->id, item->id, 1};

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

EditTransaction blockQuoteTextUpdateLiteralTransactionForDocument(const Document& document,
                                                                 const QString& beforeLiteral,
                                                                 const QString& afterLiteral,
                                                                 const QString& beforeMarkdown,
                                                                 const QString& afterMarkdown)
{
    const MarkdownNode* text = nullptr;
    const MarkdownNode* paragraph = nullptr;
    const MarkdownNode* quote = nullptr;
    for (const MarkdownNode& node : document.markdownDocument().nodes()) {
        if (!text && node.type == MarkdownNodeType::Text && node.literal == beforeLiteral) {
            text = &node;
            paragraph = document.markdownDocument().nodeById(node.parent);
            quote = paragraph ? document.markdownDocument().nodeById(paragraph->parent) : nullptr;
            break;
        }
    }
    Q_ASSERT(text);
    Q_ASSERT(paragraph);
    Q_ASSERT(quote);

    EditOperation operation;
    operation.kind = EditOperationKind::UpdateLiteral;
    operation.nodeId = text->id;
    operation.beforeNode = textSnapshot(text->id, paragraph->id, beforeLiteral);
    operation.beforeNode.ancestorIds = {paragraph->id, quote->id, 1};
    operation.afterNode = textSnapshot(text->id, paragraph->id, afterLiteral);
    operation.afterNode.ancestorIds = {paragraph->id, quote->id, 1};

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

EditTransaction blockQuoteLastTextUpdateLiteralTransactionForDocument(const Document& document,
                                                                     const QString& beforeLiteral,
                                                                     const QString& afterLiteral,
                                                                     const QString& beforeMarkdown,
                                                                     const QString& afterMarkdown)
{
    const MarkdownNode* text = lastTextNodeWithLiteral(document.markdownDocument(), beforeLiteral);
    const MarkdownNode* paragraph = text ? document.markdownDocument().nodeById(text->parent) : nullptr;
    const MarkdownNode* quote = paragraph ? document.markdownDocument().nodeById(paragraph->parent) : nullptr;
    Q_ASSERT(text);
    Q_ASSERT(paragraph);
    Q_ASSERT(quote);

    EditOperation operation;
    operation.kind = EditOperationKind::UpdateLiteral;
    operation.nodeId = text->id;
    operation.beforeNode = textSnapshot(text->id, paragraph->id, beforeLiteral);
    operation.beforeNode.ancestorIds = {paragraph->id, quote->id, 1};
    operation.afterNode = textSnapshot(text->id, paragraph->id, afterLiteral);
    operation.afterNode.ancestorIds = {paragraph->id, quote->id, 1};

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
                                    const QVector<MarkdownNodeId>& movedItems)
{
    MarkdownSerializer serializer;
    EditTransaction transaction;
    transaction.kind = kind;
    transaction.affectedNodeIds = movedItems;
    transaction.beforeMarkdown = serializer.serializeDocument(beforeDocument);
    transaction.afterMarkdown = serializer.serializeDocument(afterDocument);
    for (MarkdownNodeId itemId : movedItems) {
        const MarkdownNode* before = beforeDocument.nodeById(itemId);
        const MarkdownNode* after = afterDocument.nodeById(itemId);
        Q_ASSERT(before);
        Q_ASSERT(after);
        EditOperation operation;
        operation.kind = EditOperationKind::MoveNode;
        operation.nodeId = itemId;
        operation.beforeNode = nodeSnapshot(*before);
        operation.afterNode = nodeSnapshot(*after);
        transaction.beforeNodes.append(operation.beforeNode);
        transaction.afterNodes.append(operation.afterNode);
        transaction.operations.append(operation);
    }
    return transaction;
}

EditTransaction formulaUpdateLiteralTransaction(const Document& document,
                                                MarkdownNodeType type,
                                                const QString& replacement = {})
{
    const MarkdownNode* formula = firstNodeOfType(document.markdownDocument(), type);
    Q_ASSERT(formula);
    const QString nextMarkdown = !replacement.isEmpty()
        ? replacement
        : type == MarkdownNodeType::FormulaInline ? QStringLiteral("$y$") : QStringLiteral("w");
    const QString nextLiteral = type == MarkdownNodeType::FormulaInline
        && nextMarkdown.startsWith('$') && nextMarkdown.endsWith('$') && nextMarkdown.size() >= 2
        ? nextMarkdown.mid(1, nextMarkdown.size() - 2)
        : nextMarkdown;

    EditOperation operation;
    operation.kind = EditOperationKind::UpdateLiteral;
    operation.nodeId = formula->id;
    operation.beforeNode = nodeSnapshotWithLiteral(*formula, type == MarkdownNodeType::FormulaInline ? QStringLiteral("x") : QStringLiteral("z"));
    operation.afterNode = nodeSnapshotWithLiteral(*formula, nextLiteral);

    EditTransaction transaction;
    transaction.kind = EditTransactionKind::RenderedEdit;
    transaction.affectedNodeIds = {formula->id};
    transaction.beforeNodes = {operation.beforeNode};
    transaction.afterNodes = {operation.afterNode};
    transaction.operations = {operation};
    transaction.beforeMarkdown = type == MarkdownNodeType::FormulaInline ? QStringLiteral("$x$") : QStringLiteral("$$\nz\n$$");
    transaction.afterMarkdown = type == MarkdownNodeType::FormulaInline
        ? nextMarkdown
        : QStringLiteral("$$\n") + nextLiteral + QStringLiteral("\n$$");
    return transaction;
}

} // namespace

class TestDocument : public QObject
{
    Q_OBJECT

private slots:
    void emitsSourceSelectionForMarkdownEdit()
    {
        qRegisterMetaType<SourceSelection>("SourceSelection");
        Document document;
        document.setMarkdown(QStringLiteral("Hello"));
        QSignalSpy selectionSpy(&document, &Document::sourceSelectionRequested);

        document.applyMarkdownEdit(QStringLiteral("Hello world"), SourceSelection{6, 11}, QStringLiteral("Edit"));

        QCOMPARE(selectionSpy.size(), 1);
        const SourceSelection selection = selectionSpy.takeFirst().at(0).value<SourceSelection>();
        QCOMPARE(selection.start, 6);
        QCOMPARE(selection.end, 11);
    }

    void cursorOffsetEditStillEmitsCursorSignal()
    {
        Document document;
        document.setMarkdown(QStringLiteral("Hello"));
        QSignalSpy cursorSpy(&document, &Document::cursorSourceOffsetRequested);

        document.applyMarkdownEdit(QStringLiteral("Hello world"), 11, QStringLiteral("Edit"));

        QCOMPARE(cursorSpy.size(), 1);
        QCOMPARE(cursorSpy.takeFirst().at(0).toInt(), 11);
    }

    void undoRedoRestoresSourceSelection()
    {
        qRegisterMetaType<SourceSelection>("SourceSelection");
        Document document;
        document.setMarkdown(QStringLiteral("Hello"));
        document.applyMarkdownEdit(QStringLiteral("Hello world"), SourceSelection{6, 11}, QStringLiteral("Edit"));
        QSignalSpy selectionSpy(&document, &Document::sourceSelectionRequested);

        document.undoStack()->undo();
        document.undoStack()->redo();

        QCOMPARE(selectionSpy.size(), 1);
        const SourceSelection redoSelection = selectionSpy.takeFirst().at(0).value<SourceSelection>();
        QCOMPARE(redoSelection.start, 6);
        QCOMPARE(redoSelection.end, 11);
    }

    void applyMarkdownEditStoresEditTransaction()
    {
        qRegisterMetaType<SourceSelection>("SourceSelection");
        Document document;
        document.setMarkdown(QStringLiteral("Hello"));

        EditTransaction transaction;
        transaction.kind = EditTransactionKind::RenderedEdit;
        transaction.beforeSelection = SourceSelection{1, 1};
        transaction.afterSelection = SourceSelection{6, 11};
        transaction.affectedNodeIds = {42};
        transaction.beforeMarkdown = QStringLiteral("Hello");
        transaction.afterMarkdown = QStringLiteral("Hello world");
        transaction.label = QStringLiteral("Rendered Edit");
        EditOperation operation;
        operation.kind = EditOperationKind::UpdateLiteral;
        operation.nodeId = 42;
        transaction.operations = {operation};

        QSignalSpy selectionSpy(&document, &Document::sourceSelectionRequested);
        document.applyMarkdownEdit(transaction, QStringLiteral("Edit"));

        QCOMPARE(document.markdown(), QStringLiteral("Hello world"));
        QCOMPARE(selectionSpy.size(), 1);
        const SourceSelection selection = selectionSpy.takeFirst().at(0).value<SourceSelection>();
        QCOMPARE(selection.start, 6);
        QCOMPARE(selection.end, 11);

        const std::optional<EditTransaction> storedTransaction = document.editTransactionAt(0);
        QVERIFY(storedTransaction.has_value());
        QCOMPARE(storedTransaction->kind, EditTransactionKind::RenderedEdit);
        QCOMPARE(storedTransaction->affectedNodeIds.size(), 1);
        QCOMPARE(storedTransaction->affectedNodeIds.first(), static_cast<MarkdownNodeId>(42));
        QCOMPARE(storedTransaction->operations.size(), 1);
        QCOMPARE(storedTransaction->operations.first().kind, EditOperationKind::UpdateLiteral);
        QCOMPARE(storedTransaction->operations.first().nodeId, static_cast<MarkdownNodeId>(42));
        QCOMPARE(storedTransaction->beforeMarkdown, QStringLiteral("Hello"));
        QCOMPARE(storedTransaction->afterMarkdown, QStringLiteral("Hello world"));
        QVERIFY(!document.editTransactionValidationAt(0).has_value());
        QVERIFY(!document.canApplyEditStructurallyAt(0));

        document.undoStack()->undo();
        QCOMPARE(document.markdown(), QStringLiteral("Hello"));
        document.undoStack()->redo();
        QCOMPARE(document.markdown(), QStringLiteral("Hello world"));
    }

    void applyMarkdownEditStoresTransactionValidation()
    {
        Document document;
        document.setMarkdown(QStringLiteral("Hello"));

        EditTransaction transaction;
        transaction.kind = EditTransactionKind::RenderedEdit;
        transaction.beforeMarkdown = QStringLiteral("Hello");
        transaction.afterMarkdown = QStringLiteral("Hello world");

        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Edit"));

        const std::optional<EditTransactionValidationResult> storedValidation = document.editTransactionValidationAt(0);
        QVERIFY(storedValidation.has_value());
        QVERIFY(storedValidation->ok);
        QVERIFY(storedValidation->errors.isEmpty());
        QCOMPARE(document.markdown(), QStringLiteral("Hello world"));
        document.undoStack()->undo();
        QCOMPARE(document.markdown(), QStringLiteral("Hello"));
    }

    void canApplyEditStructurallyForValidatedTransaction()
    {
        Document document;
        document.setMarkdown(QStringLiteral("Hello"));

        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(structurallyApplicableTransaction(), validation, QStringLiteral("Edit"));

        QVERIFY(document.canApplyEditStructurallyAt(0));
    }

    void cannotApplyEditStructurallyWithoutValidation()
    {
        Document document;
        document.setMarkdown(QStringLiteral("Hello"));

        document.applyMarkdownEdit(structurallyApplicableTransaction(), QStringLiteral("Edit"));

        QVERIFY(!document.canApplyEditStructurallyAt(0));
    }

    void cannotApplyEditStructurallyWithFailedValidation()
    {
        Document document;
        document.setMarkdown(QStringLiteral("Hello"));

        EditTransactionValidationResult validation;
        validation.ok = false;
        validation.errors = {QStringLiteral("invalid operation")};

        document.applyMarkdownEdit(structurallyApplicableTransaction(), validation, QStringLiteral("Edit"));

        QVERIFY(!document.canApplyEditStructurallyAt(0));
    }

    void cannotApplyEditStructurallyWithoutOperations()
    {
        Document document;
        document.setMarkdown(QStringLiteral("Hello"));

        EditTransaction transaction = structurallyApplicableTransaction();
        transaction.operations.clear();
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Edit"));

        QVERIFY(!document.canApplyEditStructurallyAt(0));
    }

    void cannotApplyEditStructurallyForPlainMarkdownEdit()
    {
        Document document;
        document.setMarkdown(QStringLiteral("Hello"));

        document.applyMarkdownEdit(QStringLiteral("Hello world"), 11, QStringLiteral("Edit"));

        QVERIFY(!document.canApplyEditStructurallyAt(0));
    }

    void recordsStructuralRedoDryRunForValidatedTransaction()
    {
        Document document;
        document.setMarkdown(QStringLiteral("Hello"));
        QCOMPARE(document.lastStructuralEditObservation().undoStatus, StructuralEditStatus::NotApplicable);
        QCOMPARE(document.lastStructuralEditObservation().redoStatus, StructuralEditStatus::NotApplicable);
        EditTransaction transaction = structurallyApplicableTransactionForDocument(document);
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Edit"));

        const std::optional<EditTransactionApplyResult>& result = document.lastStructuralRedoDryRunResult();
        QVERIFY(result.has_value());
        QVERIFY2(result->ok, qPrintable(result->errors.join(QStringLiteral("; "))));
        QVERIFY(result->matchesExpectedMarkdown);
        QCOMPARE(result->markdown, QStringLiteral("Hello Qt"));
        QVERIFY(!document.lastStructuralUndoDryRunResult().has_value());
        const StructuralEditObservation observation = document.lastStructuralEditObservation();
        QCOMPARE(observation.undoStatus, StructuralEditStatus::NotApplicable);
        QCOMPARE(observation.redoStatus, StructuralEditStatus::Ready);
        QVERIFY(observation.undoErrors.isEmpty());
        QVERIFY(observation.redoErrors.isEmpty());
        QCOMPARE(document.markdown(), QStringLiteral("Hello Qt"));
    }

    void recordsStructuralUndoDryRunForValidatedTransaction()
    {
        Document document;
        document.setMarkdown(QStringLiteral("Hello"));
        EditTransaction transaction = structurallyApplicableTransactionForDocument(document);
        EditTransactionValidationResult validation;
        validation.ok = true;
        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Edit"));

        document.undoStack()->undo();

        const std::optional<EditTransactionApplyResult>& result = document.lastStructuralUndoDryRunResult();
        QVERIFY(result.has_value());
        QVERIFY2(result->ok, qPrintable(result->errors.join(QStringLiteral("; "))));
        QVERIFY(result->matchesExpectedMarkdown);
        QCOMPARE(result->markdown, QStringLiteral("Hello"));
        const StructuralEditObservation observation = document.lastStructuralEditObservation();
        QCOMPARE(observation.undoStatus, StructuralEditStatus::Ready);
        QCOMPARE(observation.redoStatus, StructuralEditStatus::Ready);
        QVERIFY(observation.undoErrors.isEmpty());
        QVERIFY(observation.redoErrors.isEmpty());
        QCOMPARE(document.markdown(), QStringLiteral("Hello"));
    }

    void clearsStructuralDryRunForPlainMarkdownEdit()
    {
        Document document;
        document.setMarkdown(QStringLiteral("Hello"));
        EditTransaction transaction = structurallyApplicableTransactionForDocument(document);
        EditTransactionValidationResult validation;
        validation.ok = true;
        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Edit"));
        QVERIFY(document.lastStructuralRedoDryRunResult().has_value());
        QVERIFY(document.lastStructuralRedoApplied());
        QVERIFY(!document.lastStructuralUndoApplied());

        document.applyMarkdownEdit(QStringLiteral("Hello Qt!"), 9, QStringLiteral("Edit"));

        QVERIFY(!document.lastStructuralRedoDryRunResult().has_value());
        QVERIFY(!document.lastStructuralRedoApplied());
        document.undoStack()->undo();
        QVERIFY(!document.lastStructuralUndoDryRunResult().has_value());
        QVERIFY(!document.lastStructuralUndoApplied());
        const StructuralEditObservation observation = document.lastStructuralEditObservation();
        QCOMPARE(observation.undoStatus, StructuralEditStatus::NotApplicable);
        QCOMPARE(observation.redoStatus, StructuralEditStatus::NotApplicable);
    }

    void structuralDryRunFailureDoesNotBlockUndoRedo()
    {
        Document document;
        document.setMarkdown(QStringLiteral("Hello"));
        EditTransaction transaction = structurallyApplicableTransactionForDocument(document);
        transaction.operations.first().beforeNode.literal = QStringLiteral("Different");
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Edit"));

        const std::optional<EditTransactionApplyResult>& redoResult = document.lastStructuralRedoDryRunResult();
        QVERIFY(redoResult.has_value());
        QVERIFY(!redoResult->ok);
        StructuralEditObservation observation = document.lastStructuralEditObservation();
        QCOMPARE(observation.redoStatus, StructuralEditStatus::DryRunFailed);
        QVERIFY(!observation.redoErrors.isEmpty());
        QCOMPARE(document.markdown(), QStringLiteral("Hello Qt"));

        document.undoStack()->undo();

        const std::optional<EditTransactionApplyResult>& undoResult = document.lastStructuralUndoDryRunResult();
        QVERIFY(undoResult.has_value());
        QVERIFY(!undoResult->ok);
        observation = document.lastStructuralEditObservation();
        QCOMPARE(observation.undoStatus, StructuralEditStatus::DryRunFailed);
        QCOMPARE(observation.redoStatus, StructuralEditStatus::DryRunFailed);
        QVERIFY(!observation.undoErrors.isEmpty());
        QVERIFY(!observation.redoErrors.isEmpty());
        QCOMPARE(document.markdown(), QStringLiteral("Hello"));
    }

    void appliesReadyUpdateLiteralRedoStructurally()
    {
        Document document;
        document.setMarkdown(QStringLiteral("Hello"));
        const MarkdownNodeId beforeTextId = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::Text)->id;
        EditTransaction transaction = structurallyApplicableTransactionForDocument(document);
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Edit"));

        QVERIFY(document.lastStructuralRedoApplied());
        QVERIFY(!document.lastStructuralUndoApplied());
        QCOMPARE(document.markdown(), QStringLiteral("Hello Qt"));
        const MarkdownNode* text = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::Text);
        QVERIFY(text);
        QCOMPARE(text->id, beforeTextId);
        QCOMPARE(text->literal, QStringLiteral("Hello Qt"));
    }

    void appliesInlineFormulaUpdateLiteralRedoStructurally()
    {
        Document document;
        document.setMarkdown(QStringLiteral("$x$"));
        QCOMPARE(document.mathSpans().size(), 1);
        QCOMPARE(document.mathSpans().first().tex, QStringLiteral("x"));
        const MarkdownNodeId beforeFormulaId = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::FormulaInline)->id;
        EditTransaction transaction = formulaUpdateLiteralTransaction(document, MarkdownNodeType::FormulaInline);
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Edit Formula"));

        QVERIFY(document.lastStructuralRedoApplied());
        const std::optional<EditTransactionApplyResult>& result = document.lastStructuralRedoDryRunResult();
        QVERIFY(result.has_value());
        QVERIFY2(result->ok, qPrintable(result->errors.join(QStringLiteral("; "))));
        QCOMPARE(result->markdown, QStringLiteral("$y$"));
        QCOMPARE(document.markdown(), QStringLiteral("$y$"));
        QCOMPARE(document.mathSpans().size(), 1);
        QCOMPARE(document.mathSpans().first().tex, QStringLiteral("y"));
        const MarkdownNode* formula = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::FormulaInline);
        QVERIFY(formula);
        QCOMPARE(formula->id, beforeFormulaId);
        QCOMPARE(formula->literal, QStringLiteral("y"));
    }

    void appliesInlineFormulaUpdateLiteralUndoStructurally()
    {
        Document document;
        document.setMarkdown(QStringLiteral("$x$"));
        const MarkdownNodeId beforeFormulaId = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::FormulaInline)->id;
        EditTransaction transaction = formulaUpdateLiteralTransaction(document, MarkdownNodeType::FormulaInline);
        EditTransactionValidationResult validation;
        validation.ok = true;
        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Edit Formula"));

        document.undoStack()->undo();

        QVERIFY(document.lastStructuralUndoApplied());
        QCOMPARE(document.markdown(), QStringLiteral("$x$"));
        QCOMPARE(document.mathSpans().size(), 1);
        QCOMPARE(document.mathSpans().first().tex, QStringLiteral("x"));
        const MarkdownNode* formula = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::FormulaInline);
        QVERIFY(formula);
        QCOMPARE(formula->id, beforeFormulaId);
        QCOMPARE(formula->literal, QStringLiteral("x"));
    }

    void structuralInlineFormulaRedoRefreshesNodeSourceSpans()
    {
        Document document;
        document.setMarkdown(QStringLiteral("$x$"));
        EditTransaction transaction = formulaUpdateLiteralTransaction(document,
                                                                      MarkdownNodeType::FormulaInline,
                                                                      QStringLiteral("$y+1$"));
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Edit Formula"));

        QVERIFY(document.lastStructuralRedoApplied());
        QCOMPARE(document.markdown(), QStringLiteral("$y+1$"));
        const MarkdownNode* formula = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::FormulaInline);
        QVERIFY(formula);
        QCOMPARE(document.markdownDocument().source(), QStringLiteral("$y+1$"));
        QCOMPARE(document.markdownDocument().source().mid(formula->source.start, formula->source.end - formula->source.start),
                 QStringLiteral("$y+1$"));
        QCOMPARE(document.markdownDocument().source().mid(formula->content.start, formula->content.end - formula->content.start),
                 QStringLiteral("y+1"));
        QVERIFY(document.sourceMap().spanForNode(formula->id).has_value());
        QCOMPARE(document.sourceMap().spanForNode(formula->id)->source.start, formula->source.start);
        QCOMPARE(document.sourceMap().spanForNode(formula->id)->source.end, formula->source.end);
        SourceCoordinateMapper mapper(document.markdownDocument().source());
        QCOMPARE(mapper.spanForRange(formula->sourceRange).start, formula->source.start);
        QCOMPARE(mapper.spanForRange(formula->sourceRange).end, formula->source.end);
    }

    void structuralInlineFormulaRedoKeepsPartialRenderConsistent()
    {
        Document document;
        document.setMarkdown(QStringLiteral("$x$"));
        const MarkdownNodeId paragraphId = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::Paragraph)->id;
        EditTransaction transaction = formulaUpdateLiteralTransaction(document,
                                                                      MarkdownNodeType::FormulaInline,
                                                                      QStringLiteral("$y+1$"));
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Edit Formula"));

        QVERIFY(document.lastStructuralRedoApplied());
        QCOMPARE(document.lastInvalidatedNodeIds(), QVector<MarkdownNodeId>{paragraphId});
        QCOMPARE(document.lastPartialRenderResult().renderedNodeIds, QVector<MarkdownNodeId>{paragraphId});
        QCOMPARE(document.lastPartialRenderResult().blocks.size(), 1);
        QCOMPARE(document.lastPartialRenderResult().blocks.first().nodeId, paragraphId);
        QCOMPARE(document.lastPartialRenderResult().replacementRanges.size(), 1);
        const PartialReplacementRange replacement = document.lastPartialRenderResult().replacementRanges.first();
        QCOMPARE(replacement.nodeId, paragraphId);
        QVERIFY(replacement.hasRenderedRange());
        QCOMPARE(document.blocks().size(), 1);
        QCOMPARE(replacement.renderedStart, document.blocks().first().renderedStart);
        QCOMPARE(replacement.renderedEnd, document.blocks().first().renderedEnd);
        QVERIFY(replacement.source.start <= document.lastPartialRenderResult().blocks.first().source.start);
        QVERIFY(replacement.source.end <= document.lastPartialRenderResult().blocks.first().source.end);
        QCOMPARE(replacement.kind, RenderSpan::Kind::Paragraph);

        const RenderConsistencyResult consistency = RenderConsistency::comparePartialToFull(
            {nullptr, document.sourceMap(), document.blocks(), document.syntaxTokens()},
            document.lastPartialRenderResult());
        QVERIFY2(consistency.ok, qPrintable(consistency.message));
        QVERIFY2(document.lastPartialPatchPlan().ok, qPrintable(document.lastPartialPatchPlan().errors.join(QStringLiteral("; "))));
        QCOMPARE(document.lastPartialPatchPlan().steps.size(), 1);
        QCOMPARE(document.lastPartialPatchPlan().steps.first().nodeId, paragraphId);
        QCOMPARE(document.lastPartialPatchPlan().steps.first().oldRenderedStart, replacement.renderedStart);
        QCOMPARE(document.lastPartialPatchPlan().steps.first().oldRenderedEnd, replacement.renderedEnd);
        QCOMPARE(document.lastPartialPatchPlan().steps.first().source.start,
                 document.lastPartialRenderResult().blocks.first().source.start);
        QCOMPARE(document.lastPartialPatchPlan().steps.first().source.end,
                 document.lastPartialRenderResult().blocks.first().source.end);
        QVERIFY2(document.lastPartialPatchDryRun().ok, qPrintable(document.lastPartialPatchDryRun().errors.join(QStringLiteral("; "))));
        QVERIFY2(document.lastPartialRenderStateMergeResult().ok,
                 qPrintable(document.lastPartialRenderStateMergeResult().errors.join(QStringLiteral("; "))));
        QVERIFY(document.lastPartialPatchSimulationResult().attempted);
        QVERIFY2(document.lastPartialPatchSimulationResult().ok,
                 qPrintable(document.lastPartialPatchSimulationResult().errors.join(QStringLiteral("; "))));
        QVERIFY(document.lastPartialPatchSimulationResult().appliedToDocument);
    }

    void structuralTextUndoKeepsPartialRenderConsistent()
    {
        Document document;
        document.setMarkdown(QStringLiteral("Hello"));
        const MarkdownNodeId paragraphId = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::Paragraph)->id;
        EditTransaction transaction = structurallyApplicableTransactionForDocument(document);
        EditTransactionValidationResult validation;
        validation.ok = true;
        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Edit"));

        document.undoStack()->undo();

        QVERIFY(document.lastStructuralUndoApplied());
        QCOMPARE(document.lastInvalidatedNodeIds(), QVector<MarkdownNodeId>{paragraphId});
        QCOMPARE(document.lastPartialRenderResult().renderedNodeIds, QVector<MarkdownNodeId>{paragraphId});
        QCOMPARE(document.lastPartialRenderResult().replacementRanges.size(), 1);
        QCOMPARE(document.lastPartialRenderResult().replacementRanges.first().nodeId, paragraphId);
        QVERIFY(document.lastPartialRenderResult().replacementRanges.first().hasRenderedRange());
        const RenderConsistencyResult consistency = RenderConsistency::comparePartialToFull(
            {nullptr, document.sourceMap(), document.blocks(), document.syntaxTokens()},
            document.lastPartialRenderResult());
        QVERIFY2(consistency.ok, qPrintable(consistency.message));
        QVERIFY2(document.lastPartialPatchPlan().ok, qPrintable(document.lastPartialPatchPlan().errors.join(QStringLiteral("; "))));
        QCOMPARE(document.lastPartialPatchPlan().steps.size(), 1);
        QCOMPARE(document.lastPartialPatchPlan().steps.first().nodeId, paragraphId);
        QVERIFY2(document.lastPartialPatchDryRun().ok, qPrintable(document.lastPartialPatchDryRun().errors.join(QStringLiteral("; "))));
        QVERIFY2(document.lastPartialRenderStateMergeResult().ok,
                 qPrintable(document.lastPartialRenderStateMergeResult().errors.join(QStringLiteral("; "))));
        QVERIFY(document.lastPartialPatchSimulationResult().attempted);
        QVERIFY2(document.lastPartialPatchSimulationResult().ok,
                 qPrintable(document.lastPartialPatchSimulationResult().errors.join(QStringLiteral("; "))));
        QVERIFY(document.lastPartialPatchSimulationResult().appliedToDocument);
    }

    void partialPatchRenderingCanBeDisabledForTesting()
    {
        Document document;
        document.setPartialPatchRenderingEnabledForTesting(false);
        document.setMarkdown(QStringLiteral("Hello"));
        EditTransaction transaction = structurallyApplicableTransactionForDocument(document);
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Edit"));

        QVERIFY(document.lastPartialPatchSimulationResult().attempted);
        QVERIFY2(document.lastPartialPatchSimulationResult().ok,
                 qPrintable(document.lastPartialPatchSimulationResult().errors.join(QStringLiteral("; "))));
        QVERIFY(!document.lastPartialPatchSimulationResult().usedFullComparison);
        const IncrementalRenderObservation observation = document.lastIncrementalRenderObservation();
        QVERIFY(observation.attempted);
        QVERIFY(observation.ok);
        QVERIFY(!observation.appliedToDocument);
        QCOMPARE(observation.validationMode, IncrementalRenderValidationMode::TrustLocalChecks);
        QCOMPARE(observation.patchKind, RenderSpan::Kind::Paragraph);
        QVERIFY2(document.lastPartialRenderStateMergeResult().ok,
                 qPrintable(document.lastPartialRenderStateMergeResult().errors.join(QStringLiteral("; "))));
        QVERIFY(!document.lastPartialPatchSimulationResult().appliedToDocument);
        QCOMPARE(document.textDocument()->toPlainText(), QStringLiteral("Hello Qt"));
        QCOMPARE(document.markdown(), QStringLiteral("Hello Qt"));
    }

    void partialPatchRenderingUsesPatchedDocumentByDefault()
    {
        Document document;
        document.setMarkdown(QStringLiteral("Hello"));
        const MarkdownNodeId paragraphId = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::Paragraph)->id;
        EditTransaction transaction = structurallyApplicableTransactionForDocument(document);
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Edit"));

        QVERIFY(document.lastStructuralRedoApplied());
        QCOMPARE(document.lastInvalidatedNodeIds(), QVector<MarkdownNodeId>{paragraphId});
        QVERIFY(document.lastPartialPatchSimulationResult().attempted);
        QVERIFY2(document.lastPartialPatchSimulationResult().ok,
                 qPrintable(document.lastPartialPatchSimulationResult().errors.join(QStringLiteral("; "))));
        QVERIFY(!document.lastPartialPatchSimulationResult().usedFullComparison);
        const IncrementalRenderObservation observation = document.lastIncrementalRenderObservation();
        QVERIFY(observation.attempted);
        QVERIFY(observation.ok);
        QVERIFY(observation.appliedToDocument);
        QCOMPARE(observation.validationMode, IncrementalRenderValidationMode::TrustLocalChecks);
        QCOMPARE(observation.invalidatedNodeIds, QVector<MarkdownNodeId>{paragraphId});
        QCOMPARE(observation.patchKind, RenderSpan::Kind::Paragraph);
        QVERIFY2(document.lastPartialRenderStateMergeResult().ok,
                 qPrintable(document.lastPartialRenderStateMergeResult().errors.join(QStringLiteral("; "))));
        QVERIFY(document.lastPartialPatchSimulationResult().appliedToDocument);
        QCOMPARE(document.blocks().size(), document.lastPartialRenderStateMergeResult().blocks.size());
        QCOMPARE(document.sourceMap().spans().size(), document.lastPartialRenderStateMergeResult().sourceMap.spans().size());
        QCOMPARE(document.syntaxTokens().size(), document.lastPartialRenderStateMergeResult().syntaxTokens.size());
        QCOMPARE(document.textDocument()->toPlainText(), QStringLiteral("Hello Qt"));
        QCOMPARE(document.markdown(), QStringLiteral("Hello Qt"));
        QCOMPARE(document.lastPartialPatchPlan().steps.size(), 1);
        QCOMPARE(document.lastPartialPatchPlan().steps.first().kind, RenderSpan::Kind::Paragraph);
    }

    void autoTrustsParagraphUpdateWhenTextShrinks()
    {
        const QString beforeMarkdown = QStringLiteral("Alpha beta gamma");
        const QString afterMarkdown = QStringLiteral("Alpha");
        Document document;
        document.setMarkdown(beforeMarkdown);
        const MarkdownNodeId paragraphId = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::Paragraph)->id;
        EditTransaction transaction = paragraphTextUpdateLiteralTransactionForDocument(document,
                                                                                      beforeMarkdown,
                                                                                      afterMarkdown,
                                                                                      beforeMarkdown,
                                                                                      afterMarkdown);
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Shrink Paragraph"));

        QVERIFY(document.lastStructuralRedoApplied());
        QCOMPARE(document.lastInvalidatedNodeIds(), QVector<MarkdownNodeId>{paragraphId});
        QVERIFY(document.lastPartialPatchSimulationResult().appliedToDocument);
        QVERIFY(!document.lastPartialPatchSimulationResult().usedFullComparison);
        const IncrementalRenderObservation observation = document.lastIncrementalRenderObservation();
        QCOMPARE(observation.validationMode, IncrementalRenderValidationMode::TrustLocalChecks);
        QCOMPARE(observation.patchKind, RenderSpan::Kind::Paragraph);
        QCOMPARE(document.markdown(), afterMarkdown);
        QCOMPARE(document.textDocument()->toPlainText(), afterMarkdown);
        QCOMPARE(document.blocks().size(), 1);
        QCOMPARE(document.blocks().first().nodeId, paragraphId);
        QVERIFY(document.sourceMap().renderedPositionForSourceOffset(afterMarkdown.size()).has_value());
    }

    void autoFallsBackWhenParagraphUpdateClearsDocument()
    {
        const QString beforeMarkdown = QStringLiteral("Text to clear");
        const QString afterMarkdown;
        Document document;
        document.setMarkdown(beforeMarkdown);
        const MarkdownNodeId paragraphId = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::Paragraph)->id;
        EditTransaction transaction = paragraphTextUpdateLiteralTransactionForDocument(document,
                                                                                      beforeMarkdown,
                                                                                      afterMarkdown,
                                                                                      beforeMarkdown,
                                                                                      afterMarkdown);
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Clear Paragraph"));

        QVERIFY(!document.lastStructuralRedoApplied());
        QCOMPARE(document.lastInvalidatedNodeIds(), QVector<MarkdownNodeId>{paragraphId});
        QVERIFY(!document.lastPartialPatchSimulationResult().appliedToDocument);
        const IncrementalRenderObservation observation = document.lastIncrementalRenderObservation();
        QVERIFY(!observation.attempted);
        QVERIFY(!observation.appliedToDocument);
        QCOMPARE(observation.validationMode, IncrementalRenderValidationMode::Auto);
        QCOMPARE(document.markdown(), afterMarkdown);
        QCOMPARE(document.textDocument()->toPlainText(), afterMarkdown);
        QCOMPARE(document.blocks().size(), 0);
        QCOMPARE(document.syntaxTokens().size(), 0);
    }

    void autoTrustsParagraphInlineMarkerSourceSpans()
    {
        const QString beforeMarkdown = QStringLiteral("**old** *em* `code`");
        const QString afterMarkdown = QStringLiteral("**newer** *em* `code`");
        Document document;
        document.setMarkdown(beforeMarkdown);
        const MarkdownNodeId paragraphId = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::Paragraph)->id;
        EditTransaction transaction = paragraphTextUpdateLiteralTransactionForDocument(document,
                                                                                      QStringLiteral("old"),
                                                                                      QStringLiteral("newer"),
                                                                                      beforeMarkdown,
                                                                                      afterMarkdown);
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Edit Strong Text"));

        QVERIFY(document.lastStructuralRedoApplied());
        QCOMPARE(document.lastInvalidatedNodeIds(), QVector<MarkdownNodeId>{paragraphId});
        QVERIFY(document.lastPartialPatchSimulationResult().appliedToDocument);
        QVERIFY(!document.lastPartialPatchSimulationResult().usedFullComparison);
        const IncrementalRenderObservation observation = document.lastIncrementalRenderObservation();
        QCOMPARE(observation.validationMode, IncrementalRenderValidationMode::TrustLocalChecks);
        QCOMPARE(observation.patchKind, RenderSpan::Kind::Paragraph);
        QCOMPARE(document.markdown(), afterMarkdown);
        QCOMPARE(document.textDocument()->toPlainText(), QStringLiteral("newer em code"));
        QCOMPARE(document.syntaxTokens().size(), 6);
        QCOMPARE(document.syntaxTokens().at(0).kind, SyntaxTokenSpan::Kind::StrongMarker);
        QCOMPARE(document.syntaxTokens().at(0).source.start, 0);
        QCOMPARE(document.syntaxTokens().at(0).source.end, 2);
        QCOMPARE(document.syntaxTokens().at(1).kind, SyntaxTokenSpan::Kind::StrongMarker);
        QCOMPARE(document.syntaxTokens().at(1).source.start, 7);
        QCOMPARE(document.syntaxTokens().at(1).source.end, 9);
        QCOMPARE(document.syntaxTokens().at(2).kind, SyntaxTokenSpan::Kind::EmphasisMarker);
        QCOMPARE(document.syntaxTokens().at(2).source.start, 10);
        QCOMPARE(document.syntaxTokens().at(3).kind, SyntaxTokenSpan::Kind::EmphasisMarker);
        QCOMPARE(document.syntaxTokens().at(3).source.start, 13);
        QCOMPARE(document.syntaxTokens().at(4).kind, SyntaxTokenSpan::Kind::InlineCodeMarker);
        QCOMPARE(document.syntaxTokens().at(4).source.start, 15);
        QCOMPARE(document.syntaxTokens().at(5).kind, SyntaxTokenSpan::Kind::InlineCodeMarker);
        QCOMPARE(document.syntaxTokens().at(5).source.start, 20);
        QCOMPARE(document.renderFragments().size(), 6);
        QCOMPARE(document.renderFragments().at(0).kind, RenderFragment::Kind::Marker);
        QCOMPARE(document.renderFragments().at(0).markerKind, SyntaxTokenSpan::Kind::StrongMarker);
        QCOMPARE(document.renderFragments().at(0).source.start, 0);
        QCOMPARE(document.renderFragments().at(1).source.start, 7);
        QVERIFY(!document.renderFragments().at(0).visible);
        QVERIFY(document.sourceMap().renderedPositionForSourceOffset(afterMarkdown.indexOf(QStringLiteral("newer"))).has_value());
        QVERIFY(document.sourceMap().renderedPositionForSourceOffset(afterMarkdown.indexOf(QStringLiteral("em"))).has_value());
        QVERIFY(document.sourceMap().renderedPositionForSourceOffset(afterMarkdown.indexOf(QStringLiteral("code"))).has_value());
    }

    void markerVisibilityForCaretUsesDocumentSyntaxTokens()
    {
        const QString markdown = QStringLiteral("plain **bold** text");
        Document document;
        document.setMarkdown(markdown);
        const SelectionBookmark caret = EditorSelectionMapper::bookmarkForSourceOffset(
            document.markdownDocument(), markdown.indexOf(QStringLiteral("bold")) + 1);

        const MarkerVisibilityState state = document.markerVisibilityStateForCaret(caret);

        QCOMPARE(state.mode, MarkerVisibilityMode::CaretInsideInline);
        QCOMPARE(state.visiblePairs.size(), 1);
        QCOMPARE(state.visibleFragments.size(), 2);
        QCOMPARE(state.visiblePairs.first().opening.kind, SyntaxTokenSpan::Kind::StrongMarker);
        QCOMPARE(state.visiblePairs.first().opening.source.start, 6);
        QCOMPARE(state.visiblePairs.first().closing.source.start, 12);
        QCOMPARE(state.visibleFragments.first().kind, RenderFragment::Kind::Marker);
        QCOMPARE(state.visibleFragments.first().source.start, 6);
        QCOMPARE(state.visibleFragments.last().source.start, 12);
    }

    void markerVisibilityForCaretReportsNormalOutsideSyntax()
    {
        const QString markdown = QStringLiteral("plain **bold** text");
        Document document;
        document.setMarkdown(markdown);
        const SelectionBookmark caret = EditorSelectionMapper::bookmarkForSourceOffset(
            document.markdownDocument(), markdown.indexOf(QStringLiteral("plain")) + 1);

        const MarkerVisibilityState state = document.markerVisibilityStateForCaret(caret);

        QCOMPARE(state.mode, MarkerVisibilityMode::Normal);
        QVERIFY(!state.hasVisibleMarkers());
        QVERIFY(state.visibleFragments.isEmpty());
    }

    void markerVisibilityForSelectionUsesDocumentSyntaxTokens()
    {
        const QString markdown = QStringLiteral("plain **bold** text");
        Document document;
        document.setMarkdown(markdown);
        const int selectionStart = static_cast<int>(markdown.indexOf(QStringLiteral("bold")));
        const int selectionEnd = static_cast<int>(markdown.indexOf(QStringLiteral("text")));
        const SelectionRangeBookmark selection = EditorSelectionMapper::bookmarkForSourceSelection(
            document.markdownDocument(),
            {selectionStart, selectionEnd});

        const MarkerVisibilityState state = document.markerVisibilityStateForSelection(selection);

        QCOMPARE(state.mode, MarkerVisibilityMode::SelectionIntersectsMarkerPair);
        QCOMPARE(state.visiblePairs.size(), 1);
        QCOMPARE(state.visibleFragments.size(), 2);
        QCOMPARE(state.visiblePairs.first().opening.kind, SyntaxTokenSpan::Kind::StrongMarker);
    }

    void markerVisibilityForPinnedMarkerUsesDocumentFragments()
    {
        const QString markdown = QStringLiteral("plain **bold** text");
        Document document;
        document.setMarkdown(markdown);
        const SyntaxTokenSpan opening = document.syntaxTokens().first();

        const MarkerVisibilityState state =
            document.markerVisibilityStateForPinnedMarker({opening.source, opening.nodeId, opening.kind});

        QCOMPARE(state.mode, MarkerVisibilityMode::PinnedMarkerPair);
        QCOMPARE(state.visiblePairs.size(), 1);
        QCOMPARE(state.visibleFragments.size(), 2);
        QCOMPARE(state.pinnedMarker.source.start, 6);
        QCOMPARE(state.visibleFragments.first().source.start, 6);
        QCOMPARE(state.visibleFragments.last().source.start, 12);
    }

    void partialPatchRenderingCanForceFullComparisonForTesting()
    {
        Document document;
        document.setIncrementalRenderValidationModeForTesting(IncrementalRenderValidationMode::AlwaysFullCompare);
        document.setMarkdown(QStringLiteral("Hello"));
        EditTransaction transaction = structurallyApplicableTransactionForDocument(document);
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Edit"));

        QVERIFY(document.lastPartialPatchSimulationResult().attempted);
        QVERIFY2(document.lastPartialPatchSimulationResult().ok,
                 qPrintable(document.lastPartialPatchSimulationResult().errors.join(QStringLiteral("; "))));
        QVERIFY(document.lastPartialPatchSimulationResult().usedFullComparison);
        QCOMPARE(document.lastIncrementalRenderObservation().validationMode, IncrementalRenderValidationMode::AlwaysFullCompare);
        QVERIFY(document.lastPartialPatchSimulationResult().appliedToDocument);
        QCOMPARE(document.textDocument()->toPlainText(), QStringLiteral("Hello Qt"));
    }

    void partialPatchRenderingCanTrustLocalChecksForTesting()
    {
        Document document;
        document.setIncrementalRenderValidationModeForTesting(IncrementalRenderValidationMode::TrustLocalChecks);
        document.setMarkdown(QStringLiteral("Hello"));
        const MarkdownNodeId paragraphId = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::Paragraph)->id;
        EditTransaction transaction = structurallyApplicableTransactionForDocument(document);
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Edit"));

        QVERIFY(document.lastStructuralRedoApplied());
        QCOMPARE(document.lastInvalidatedNodeIds(), QVector<MarkdownNodeId>{paragraphId});
        QVERIFY(document.lastPartialPatchSimulationResult().attempted);
        QVERIFY2(document.lastPartialPatchSimulationResult().ok,
                 qPrintable(document.lastPartialPatchSimulationResult().errors.join(QStringLiteral("; "))));
        QVERIFY(!document.lastPartialPatchSimulationResult().usedFullComparison);
        QVERIFY2(document.lastPartialRenderStateMergeResult().ok,
                 qPrintable(document.lastPartialRenderStateMergeResult().errors.join(QStringLiteral("; "))));
        QCOMPARE(document.lastIncrementalRenderObservation().validationMode, IncrementalRenderValidationMode::TrustLocalChecks);
        QVERIFY(document.lastPartialPatchSimulationResult().appliedToDocument);
        QCOMPARE(document.textDocument()->toPlainText(), QStringLiteral("Hello Qt"));
        QCOMPARE(document.markdown(), QStringLiteral("Hello Qt"));
        QCOMPARE(document.lastPartialPatchPlan().steps.size(), 1);
        QCOMPARE(document.lastPartialPatchPlan().steps.first().kind, RenderSpan::Kind::Paragraph);
    }

    void partialPatchRenderingAppliesHeadingWhenEnabled()
    {
        Document document;
        document.setPartialPatchRenderingEnabledForTesting(true);
        document.setMarkdown(QStringLiteral("# Old"));
        const MarkdownNodeId headingId = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::Heading)->id;
        EditTransaction transaction = headingUpdateLiteralTransactionForDocument(document,
                                                                                QStringLiteral("Old"),
                                                                                QStringLiteral("New"));
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Edit Heading"));

        QVERIFY(document.lastStructuralRedoApplied());
        QCOMPARE(document.markdown(), QStringLiteral("# New"));
        QCOMPARE(document.lastInvalidatedNodeIds(), QVector<MarkdownNodeId>{headingId});
        QVERIFY(document.lastPartialPatchSimulationResult().attempted);
        QVERIFY2(document.lastPartialPatchSimulationResult().ok,
                 qPrintable(document.lastPartialPatchSimulationResult().errors.join(QStringLiteral("; "))));
        QVERIFY(!document.lastPartialPatchSimulationResult().usedFullComparison);
        QCOMPARE(document.lastIncrementalRenderObservation().validationMode, IncrementalRenderValidationMode::TrustLocalChecks);
        QCOMPARE(document.lastIncrementalRenderObservation().patchKind, RenderSpan::Kind::Heading);
        QVERIFY(document.lastPartialPatchSimulationResult().appliedToDocument);
        QCOMPARE(document.textDocument()->toPlainText(), QStringLiteral("New"));
        QCOMPARE(document.lastPartialPatchPlan().steps.size(), 1);
        QCOMPARE(document.lastPartialPatchPlan().steps.first().kind, RenderSpan::Kind::Heading);
    }

    void selectionBookmarkRestoresRangeAfterPrecedingPartialPatch()
    {
        const QString beforeMarkdown = QStringLiteral("# T\n\nAlpha bold omega");
        const QString afterMarkdown = QStringLiteral("# Title expanded\n\nAlpha bold omega");
        Document document;
        document.setMarkdown(beforeMarkdown);
        const int beforeBoldStart = static_cast<int>(beforeMarkdown.indexOf(QStringLiteral("bold")));
        const int beforeBoldEnd = beforeBoldStart + 4;
        const SelectionRangeBookmark bookmark =
            EditorSelectionMapper::bookmarkForSourceSelection(document.markdownDocument(), {beforeBoldStart, beforeBoldEnd});

        EditTransaction transaction = headingUpdateLiteralTransactionForDocument(document,
                                                                                QStringLiteral("T"),
                                                                                QStringLiteral("Title expanded"),
                                                                                beforeMarkdown,
                                                                                afterMarkdown);
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Expand Heading"));

        QVERIFY(document.lastStructuralRedoApplied());
        QCOMPARE(document.markdown(), afterMarkdown);
        QCOMPARE(document.lastIncrementalRenderObservation().validationMode, IncrementalRenderValidationMode::TrustLocalChecks);
        QCOMPARE(document.lastIncrementalRenderObservation().patchKind, RenderSpan::Kind::Heading);
        QVERIFY(document.lastPartialPatchSimulationResult().appliedToDocument);
        const int afterBoldStart = static_cast<int>(afterMarkdown.indexOf(QStringLiteral("bold")));
        QVERIFY(afterBoldStart > beforeBoldStart);
        QVERIFY(document.sourceMap().renderedPositionForSourceOffset(beforeBoldStart).has_value());

        std::optional<SourceSelection> restored =
            EditorSelectionMapper::orderedRenderedSelectionForBookmark(document.sourceMap(), bookmark);

        QVERIFY(restored.has_value());
        QCOMPARE(restored->start, *document.sourceMap().renderedPositionForSourceOffset(afterBoldStart));
        QCOMPARE(restored->end, *document.sourceMap().renderedPositionForSourceOffset(afterBoldStart + 4));
    }

    void selectionBookmarkRestoresCaretAfterPrecedingPartialPatch()
    {
        const QString beforeMarkdown = QStringLiteral("# T\n\nAlpha bold omega");
        const QString afterMarkdown = QStringLiteral("# Title expanded\n\nAlpha bold omega");
        Document document;
        document.setMarkdown(beforeMarkdown);
        const int beforeCaret = static_cast<int>(beforeMarkdown.indexOf(QStringLiteral("bold"))) + 2;
        const SelectionBookmark bookmark =
            EditorSelectionMapper::bookmarkForSourceOffset(document.markdownDocument(), beforeCaret);

        EditTransaction transaction = headingUpdateLiteralTransactionForDocument(document,
                                                                                QStringLiteral("T"),
                                                                                QStringLiteral("Title expanded"),
                                                                                beforeMarkdown,
                                                                                afterMarkdown);
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Expand Heading"));

        QVERIFY(document.lastStructuralRedoApplied());
        QVERIFY(document.lastPartialPatchSimulationResult().appliedToDocument);
        const int afterCaret = static_cast<int>(afterMarkdown.indexOf(QStringLiteral("bold"))) + 2;

        std::optional<int> restored =
            EditorSelectionMapper::renderedPositionForBookmark(document.sourceMap(), bookmark);

        QVERIFY(restored.has_value());
        QCOMPARE(*restored, *document.sourceMap().renderedPositionForSourceOffset(afterCaret));
    }

    void partialPatchRenderingAppliesCodeBlockWhenEnabled()
    {
        Document document;
        document.setPartialPatchRenderingEnabledForTesting(true);
        document.setMarkdown(QStringLiteral("```\nold\n```"));
        const MarkdownNodeId codeId = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::CodeBlock)->id;
        EditTransaction transaction = codeBlockUpdateLiteralTransactionForDocument(document,
                                                                                  QStringLiteral("old\n"),
                                                                                  QStringLiteral("new\n"));
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Edit Code"));

        QVERIFY(document.lastStructuralRedoApplied());
        QCOMPARE(document.markdown(), QStringLiteral("```\nnew\n```"));
        QCOMPARE(document.lastInvalidatedNodeIds(), QVector<MarkdownNodeId>{codeId});
        QVERIFY(document.lastPartialPatchSimulationResult().attempted);
        QVERIFY2(document.lastPartialPatchSimulationResult().ok,
                 qPrintable(document.lastPartialPatchSimulationResult().errors.join(QStringLiteral("; "))));
        QVERIFY(!document.lastPartialPatchSimulationResult().usedFullComparison);
        QCOMPARE(document.lastIncrementalRenderObservation().validationMode, IncrementalRenderValidationMode::TrustLocalChecks);
        QCOMPARE(document.lastIncrementalRenderObservation().patchKind, RenderSpan::Kind::CodeBlock);
        QVERIFY(document.lastPartialPatchSimulationResult().appliedToDocument);
        QVERIFY(document.textDocument()->toPlainText().contains(QStringLiteral("new")));
        QCOMPARE(document.lastPartialPatchPlan().steps.size(), 1);
        QCOMPARE(document.lastPartialPatchPlan().steps.first().kind, RenderSpan::Kind::CodeBlock);
    }

    void partialPatchRenderingAppliesMiddleListItemWhenEnabled()
    {
        const QString beforeMarkdown = QStringLiteral("- A\n- old\n- C");
        const QString afterMarkdown = QStringLiteral("- A\n- new\n- C");
        Document document;
        document.setPartialPatchRenderingEnabledForTesting(true);
        document.setMarkdown(beforeMarkdown);
        const QVector<MarkdownNodeId> items = listItemIds(document.markdownDocument());
        QCOMPARE(items.size(), 3);
        const MarkdownNodeId itemId = items.at(1);
        EditTransaction transaction = listTextUpdateLiteralTransactionForDocument(document,
                                                                                 QStringLiteral("old"),
                                                                                 QStringLiteral("new"),
                                                                                 beforeMarkdown,
                                                                                 afterMarkdown);
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Edit List Item"));

        QVERIFY(document.lastStructuralRedoApplied());
        QCOMPARE(document.markdown(), afterMarkdown);
        QCOMPARE(document.lastInvalidatedNodeIds(), QVector<MarkdownNodeId>{itemId});
        QVERIFY(document.lastPartialPatchSimulationResult().attempted);
        QVERIFY2(document.lastPartialPatchSimulationResult().ok,
                 qPrintable(document.lastPartialPatchSimulationResult().errors.join(QStringLiteral("; "))));
        QVERIFY(!document.lastPartialPatchSimulationResult().usedFullComparison);
        const IncrementalRenderObservation observation = document.lastIncrementalRenderObservation();
        QCOMPARE(observation.validationMode, IncrementalRenderValidationMode::TrustLocalChecks);
        QCOMPARE(observation.invalidatedNodeIds, QVector<MarkdownNodeId>{itemId});
        QCOMPARE(observation.patchKind, RenderSpan::Kind::List);
        QCOMPARE(document.lastPartialRenderResult().blocks.size(), 1);
        QVERIFY(document.lastPartialPatchSimulationResult().appliedToDocument);
        QCOMPARE(document.textDocument()->toPlainText(), QStringLiteral("\xe2\x80\xa2 A\n\xe2\x80\xa2 new\n\xe2\x80\xa2 C"));
        QCOMPARE(document.lastPartialPatchPlan().steps.size(), 1);
        QCOMPARE(document.lastPartialPatchPlan().steps.first().kind, RenderSpan::Kind::List);
    }

    void partialPatchRenderingAppliesLooseListItemWhenEnabled()
    {
        const QString beforeMarkdown = QStringLiteral("- X\n- A\n\n  old\n- Y");
        const QString afterMarkdown = QStringLiteral("- X\n- A\n\n  new\n- Y");
        Document document;
        document.setPartialPatchRenderingEnabledForTesting(true);
        document.setMarkdown(beforeMarkdown);
        const QVector<MarkdownNodeId> items = listItemIds(document.markdownDocument());
        QCOMPARE(items.size(), 3);
        const MarkdownNodeId itemId = items.at(1);
        EditTransaction transaction = listTextUpdateLiteralTransactionForDocument(document,
                                                                                 QStringLiteral("old"),
                                                                                 QStringLiteral("new"),
                                                                                 beforeMarkdown,
                                                                                 afterMarkdown);
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Edit Loose List Item"));

        QVERIFY(document.lastStructuralRedoApplied());
        QCOMPARE(document.markdown(), afterMarkdown);
        QCOMPARE(document.lastInvalidatedNodeIds(), QVector<MarkdownNodeId>{itemId});
        QVERIFY(document.lastPartialPatchSimulationResult().attempted);
        QVERIFY2(document.lastPartialPatchSimulationResult().ok,
                 qPrintable(document.lastPartialPatchSimulationResult().errors.join(QStringLiteral("; "))));
        QVERIFY(document.lastPartialPatchSimulationResult().usedFullComparison);
        const IncrementalRenderObservation observation = document.lastIncrementalRenderObservation();
        QCOMPARE(observation.validationMode, IncrementalRenderValidationMode::AlwaysFullCompare);
        QCOMPARE(observation.patchKind, RenderSpan::Kind::List);
        QVERIFY(document.lastPartialRenderResult().blocks.size() > 1);
        QVERIFY(document.lastPartialPatchSimulationResult().appliedToDocument);
        QCOMPARE(document.textDocument()->toPlainText(), QStringLiteral("\xe2\x80\xa2 X\n\xe2\x80\xa2 A\nnew\n\xe2\x80\xa2 Y"));
        QCOMPARE(document.lastPartialPatchPlan().steps.size(), 1);
        QCOMPARE(document.lastPartialPatchPlan().steps.first().kind, RenderSpan::Kind::List);
    }

    void partialPatchRenderingAppliesBlockQuoteWhenEnabled()
    {
        const QString beforeMarkdown = QStringLiteral("Before\n\n> old\n\nAfter");
        const QString afterMarkdown = QStringLiteral("Before\n\n> new\n\nAfter");
        Document document;
        document.setPartialPatchRenderingEnabledForTesting(true);
        document.setMarkdown(beforeMarkdown);
        const MarkdownNode* quote = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::BlockQuote);
        QVERIFY(quote);
        const MarkdownNodeId quoteId = quote->id;
        EditTransaction transaction = blockQuoteTextUpdateLiteralTransactionForDocument(document,
                                                                                       QStringLiteral("old"),
                                                                                       QStringLiteral("new"),
                                                                                       beforeMarkdown,
                                                                                       afterMarkdown);
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Edit Quote"));

        QVERIFY(document.lastStructuralRedoApplied());
        QCOMPARE(document.markdown(), afterMarkdown);
        QCOMPARE(document.lastInvalidatedNodeIds(), QVector<MarkdownNodeId>{quoteId});
        QVERIFY(document.lastPartialPatchSimulationResult().attempted);
        QVERIFY2(document.lastPartialPatchSimulationResult().ok,
                 qPrintable(document.lastPartialPatchSimulationResult().errors.join(QStringLiteral("; "))));
        QVERIFY(!document.lastPartialPatchSimulationResult().usedFullComparison);
        const IncrementalRenderObservation observation = document.lastIncrementalRenderObservation();
        QCOMPARE(observation.validationMode, IncrementalRenderValidationMode::TrustLocalChecks);
        QCOMPARE(observation.invalidatedNodeIds, QVector<MarkdownNodeId>{quoteId});
        QCOMPARE(observation.patchKind, RenderSpan::Kind::BlockQuote);
        QCOMPARE(document.lastPartialRenderResult().blocks.size(), 1);
        QVERIFY(document.lastPartialPatchSimulationResult().appliedToDocument);
        QCOMPARE(document.textDocument()->toPlainText(), QStringLiteral("Before\nnew\nAfter"));
        QCOMPARE(document.lastPartialPatchPlan().steps.size(), 1);
        QCOMPARE(document.lastPartialPatchPlan().steps.first().kind, RenderSpan::Kind::BlockQuote);
    }

    void partialPatchRenderingAppliesMultiParagraphBlockQuoteWhenEnabled()
    {
        const QString beforeMarkdown = QStringLiteral("> A\n> \n> B");
        const QString afterMarkdown = QStringLiteral("> A\n> \n> C");
        Document document;
        document.setPartialPatchRenderingEnabledForTesting(true);
        document.setMarkdown(beforeMarkdown);
        const MarkdownNode* quote = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::BlockQuote);
        QVERIFY(quote);
        const MarkdownNodeId quoteId = quote->id;
        EditTransaction transaction = blockQuoteLastTextUpdateLiteralTransactionForDocument(document,
                                                                                           QStringLiteral("B"),
                                                                                           QStringLiteral("C"),
                                                                                           beforeMarkdown,
                                                                                           afterMarkdown);
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Edit Multi Quote"));

        QVERIFY(document.lastStructuralRedoApplied());
        QCOMPARE(document.markdown(), afterMarkdown);
        QCOMPARE(document.lastInvalidatedNodeIds(), QVector<MarkdownNodeId>{quoteId});
        QVERIFY(document.lastPartialPatchSimulationResult().attempted);
        QVERIFY2(document.lastPartialPatchSimulationResult().ok,
                 qPrintable(document.lastPartialPatchSimulationResult().errors.join(QStringLiteral("; "))));
        QVERIFY(document.lastPartialPatchSimulationResult().usedFullComparison);
        const IncrementalRenderObservation observation = document.lastIncrementalRenderObservation();
        QCOMPARE(observation.validationMode, IncrementalRenderValidationMode::AlwaysFullCompare);
        QCOMPARE(observation.patchKind, RenderSpan::Kind::BlockQuote);
        QVERIFY(document.lastPartialRenderResult().blocks.size() > 1);
        QVERIFY(document.lastPartialPatchSimulationResult().appliedToDocument);
        QCOMPARE(document.textDocument()->toPlainText(), QStringLiteral("A\nC"));
        QCOMPARE(document.lastPartialPatchPlan().steps.size(), 1);
        QCOMPARE(document.lastPartialPatchPlan().steps.first().kind, RenderSpan::Kind::BlockQuote);
    }

    void partialPatchRenderingFallsBackForUnsupportedBlockKind()
    {
        Document document;
        document.setPartialPatchRenderingEnabledForTesting(true);
        document.setMarkdown(QStringLiteral("- A\n- B"));
        const QVector<MarkdownNodeId> items = listItemIds(document.markdownDocument());
        QCOMPARE(items.size(), 2);
        const QVector<MarkdownNodeId> movedItems{items.at(1)};
        const MarkdownDocument afterDocument = MarkdownTransform::demoteListItems(document.markdownDocument(), movedItems);
        EditTransaction transaction = listMoveTransaction(document.markdownDocument(), afterDocument, EditTransactionKind::ListIndent, movedItems);
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Indent"));

        QVERIFY(document.lastStructuralRedoApplied());
        QVERIFY(document.lastPartialPatchSimulationResult().attempted);
        QVERIFY(!document.lastPartialPatchSimulationResult().ok);
        QVERIFY(!document.lastPartialPatchSimulationResult().appliedToDocument);
        QVERIFY(!document.lastPartialRenderStateMergeResult().ok);
        QVERIFY(!document.lastPartialPatchSimulationResult().errors.isEmpty());
        QVERIFY(document.textDocument()->toPlainText().contains(QStringLiteral("A")));
        QVERIFY(document.textDocument()->toPlainText().contains(QStringLiteral("B")));
        QCOMPARE(document.markdown(), QStringLiteral("- A\n  - B"));
    }

    void blockFormulaUpdateLiteralFallsBackToFullParse()
    {
        Document document;
        document.setMarkdown(QStringLiteral("$$\nz\n$$"));
        QCOMPARE(document.mathSpans().size(), 1);
        QCOMPARE(document.mathSpans().first().tex, QStringLiteral("z"));
        EditTransaction transaction = formulaUpdateLiteralTransaction(document, MarkdownNodeType::FormulaBlock);
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Edit Formula"));

        QVERIFY(!document.lastStructuralRedoApplied());
        QCOMPARE(document.markdown(), QStringLiteral("$$\nw\n$$"));
        QCOMPARE(document.mathSpans().size(), 1);
        QCOMPARE(document.mathSpans().first().tex, QStringLiteral("w"));
    }

    void appliesReadyUpdateLiteralUndoStructurally()
    {
        Document document;
        document.setMarkdown(QStringLiteral("Hello"));
        const MarkdownNodeId beforeTextId = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::Text)->id;
        EditTransaction transaction = structurallyApplicableTransactionForDocument(document);
        EditTransactionValidationResult validation;
        validation.ok = true;
        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Edit"));

        document.undoStack()->undo();

        QVERIFY(document.lastStructuralUndoApplied());
        QCOMPARE(document.markdown(), QStringLiteral("Hello"));
        const MarkdownNode* text = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::Text);
        QVERIFY(text);
        QCOMPARE(text->id, beforeTextId);
        QCOMPARE(text->literal, QStringLiteral("Hello"));
    }

    void appliesReadyListIndentRedoStructurally()
    {
        Document document;
        document.setMarkdown(QStringLiteral("- A\n- B"));
        const QVector<MarkdownNodeId> items = listItemIds(document.markdownDocument());
        QCOMPARE(items.size(), 2);
        const QVector<MarkdownNodeId> movedItems{items.at(1)};
        const MarkdownDocument afterDocument = MarkdownTransform::demoteListItems(document.markdownDocument(), movedItems);
        EditTransaction transaction = listMoveTransaction(document.markdownDocument(), afterDocument, EditTransactionKind::ListIndent, movedItems);
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Indent"));

        QVERIFY(document.lastStructuralRedoApplied());
        QCOMPARE(document.markdown(), QStringLiteral("- A\n  - B"));
        const MarkdownNode* moved = document.markdownDocument().nodeById(movedItems.first());
        QVERIFY(moved);
        QCOMPARE(moved->id, movedItems.first());
        QVERIFY(!document.blocks().isEmpty());
        QVERIFY(document.sourceMap().spanForNode(movedItems.first()).has_value());
    }

    void appliesReadyListIndentUndoStructurally()
    {
        Document document;
        document.setMarkdown(QStringLiteral("- A\n- B"));
        const QVector<MarkdownNodeId> items = listItemIds(document.markdownDocument());
        const QVector<MarkdownNodeId> movedItems{items.at(1)};
        const MarkdownDocument afterDocument = MarkdownTransform::demoteListItems(document.markdownDocument(), movedItems);
        EditTransaction transaction = listMoveTransaction(document.markdownDocument(), afterDocument, EditTransactionKind::ListIndent, movedItems);
        EditTransactionValidationResult validation;
        validation.ok = true;
        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Indent"));

        document.undoStack()->undo();

        QVERIFY(document.lastStructuralUndoApplied());
        QCOMPARE(document.markdown(), QStringLiteral("- A\n- B"));
        const MarkdownNode* moved = document.markdownDocument().nodeById(movedItems.first());
        QVERIFY(moved);
        QCOMPARE(moved->id, movedItems.first());
        QVERIFY(!document.blocks().isEmpty());
        QVERIFY(document.sourceMap().spanForNode(movedItems.first()).has_value());
    }

    void appliesReadyListOutdentRedoStructurally()
    {
        Document document;
        document.setMarkdown(QStringLiteral("- A\n  - B"));
        const QVector<MarkdownNodeId> items = listItemIds(document.markdownDocument());
        QCOMPARE(items.size(), 2);
        const QVector<MarkdownNodeId> movedItems{items.at(1)};
        const MarkdownDocument afterDocument = MarkdownTransform::promoteListItems(document.markdownDocument(), movedItems);
        EditTransaction transaction = listMoveTransaction(document.markdownDocument(), afterDocument, EditTransactionKind::ListOutdent, movedItems);
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Outdent"));

        QVERIFY(document.lastStructuralRedoApplied());
        QCOMPARE(document.markdown(), QStringLiteral("- A\n- B"));
        const MarkdownNode* moved = document.markdownDocument().nodeById(movedItems.first());
        QVERIFY(moved);
        QCOMPARE(moved->id, movedItems.first());
        QVERIFY(!document.blocks().isEmpty());
        QVERIFY(document.sourceMap().spanForNode(movedItems.first()).has_value());
    }

    void appliesReadyListOutdentUndoStructurally()
    {
        Document document;
        document.setMarkdown(QStringLiteral("- A\n  - B"));
        const QVector<MarkdownNodeId> items = listItemIds(document.markdownDocument());
        const QVector<MarkdownNodeId> movedItems{items.at(1)};
        const MarkdownDocument afterDocument = MarkdownTransform::promoteListItems(document.markdownDocument(), movedItems);
        EditTransaction transaction = listMoveTransaction(document.markdownDocument(), afterDocument, EditTransactionKind::ListOutdent, movedItems);
        EditTransactionValidationResult validation;
        validation.ok = true;
        document.applyMarkdownEdit(transaction, validation, QStringLiteral("Outdent"));

        document.undoStack()->undo();

        QVERIFY(document.lastStructuralUndoApplied());
        QCOMPARE(document.markdown(), QStringLiteral("- A\n  - B"));
        const MarkdownNode* moved = document.markdownDocument().nodeById(movedItems.first());
        QVERIFY(moved);
        QCOMPARE(moved->id, movedItems.first());
        QVERIFY(!document.blocks().isEmpty());
        QVERIFY(document.sourceMap().spanForNode(movedItems.first()).has_value());
    }

    void applyMarkdownEditStoresLastInvalidatedNodeIds()
    {
        Document document;
        document.setMarkdown(QStringLiteral("Hello"));
        const MarkdownNode* paragraph = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::Paragraph);
        const MarkdownNode* text = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::Text);
        QVERIFY(paragraph);
        QVERIFY(text);
        const MarkdownNodeId paragraphId = paragraph->id;
        const MarkdownNodeId textId = text->id;

        EditTransaction transaction;
        transaction.kind = EditTransactionKind::RenderedEdit;
        transaction.beforeSelection = SourceSelection{5, 5};
        transaction.afterSelection = SourceSelection{8, 8};
        transaction.affectedNodeIds = {textId};
        transaction.beforeNodes = {textSnapshot(textId, paragraphId, QStringLiteral("Hello"))};
        transaction.afterNodes = {textSnapshot(textId, paragraphId, QStringLiteral("Hello Qt"))};
        transaction.beforeMarkdown = QStringLiteral("Hello");
        transaction.afterMarkdown = QStringLiteral("Hello Qt");

        document.applyMarkdownEdit(transaction, QStringLiteral("Edit"));

        QCOMPARE(document.lastInvalidatedNodeIds(), QVector<MarkdownNodeId>{paragraphId});
        QCOMPARE(document.lastPartialRenderResult().renderedNodeIds, QVector<MarkdownNodeId>{paragraphId});
        QCOMPARE(document.lastPartialRenderResult().blocks.size(), 1);
        QCOMPARE(document.lastPartialRenderResult().blocks.first().kind, RenderSpan::Kind::Paragraph);
    }

    void nonTransactionParagraphEditUsesBlockReparsePlan()
    {
        Document document;
        document.setMarkdown(QStringLiteral("Hello"));
        const MarkdownNode* paragraph = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::Paragraph);
        QVERIFY(paragraph);
        const MarkdownNodeId paragraphId = paragraph->id;

        document.applyMarkdownEdit(QStringLiteral("Hallo"), SourceSelection{2, 2}, QStringLiteral("Edit"));

        QVERIFY2(document.lastBlockParseRange().canReparseLocally(),
                 qPrintable(document.lastBlockParseRange().reason));
        QCOMPARE(document.lastBlockParseRange().affectedBlockIds, QVector<MarkdownNodeId>{paragraphId});
        QVERIFY(document.lastBlockReparseShadowResult().attempted);
        QVERIFY2(document.lastBlockReparseShadowResult().ok,
                 qPrintable(document.lastBlockReparseShadowResult().errors.join(QStringLiteral("; "))));
        QCOMPARE(document.lastBlockReparseShadowResult().merge.replacedNodeId, paragraphId);
        QVERIFY(document.lastBlockReparseShadowResult().appliedToDocument);
        QCOMPARE(document.lastParseMode(), ParseMode::LocalReparse);
        const BlockLocalReparseObservation observation = document.lastBlockLocalReparseObservation();
        QVERIFY(observation.planned);
        QVERIFY(observation.shadowAttempted);
        QVERIFY(observation.shadowOk);
        QVERIFY(observation.applied);
        QVERIFY(observation.fallbackReason.isEmpty());
        QCOMPARE(observation.affectedBlockIds, QVector<MarkdownNodeId>{paragraphId});
        QCOMPARE(document.lastInvalidatedNodeIds(), QVector<MarkdownNodeId>{paragraphId});
        QCOMPARE(document.lastPartialRenderResult().renderedNodeIds, QVector<MarkdownNodeId>{paragraphId});
        QCOMPARE(document.lastPartialRenderResult().blocks.size(), 1);
        QCOMPARE(document.lastPartialRenderResult().blocks.first().kind, RenderSpan::Kind::Paragraph);
        QCOMPARE(document.markdown(), QStringLiteral("Hallo"));
    }

    void localBlockReparseAppliesParagraphWhenEnabled()
    {
        Document document;
        document.setBlockLocalReparseEnabledForTesting(true);
        document.setMarkdown(QStringLiteral("Hello"));
        const MarkdownNode* paragraph = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::Paragraph);
        QVERIFY(paragraph);
        const MarkdownNodeId paragraphId = paragraph->id;

        document.applyMarkdownEdit(QStringLiteral("Hallo"), SourceSelection{2, 2}, QStringLiteral("Edit"));

        QVERIFY2(document.lastBlockReparseShadowResult().ok,
                 qPrintable(document.lastBlockReparseShadowResult().errors.join(QStringLiteral("; "))));
        QVERIFY(document.lastBlockReparseShadowResult().appliedToDocument);
        QCOMPARE(document.lastParseMode(), ParseMode::LocalReparse);
        const BlockLocalReparseObservation observation = document.lastBlockLocalReparseObservation();
        QVERIFY(observation.planned);
        QVERIFY(observation.shadowOk);
        QVERIFY(observation.applied);
        QVERIFY(observation.fallbackReason.isEmpty());
        const MarkdownNode* currentParagraph = document.markdownDocument().nodeById(paragraphId);
        QVERIFY(currentParagraph);
        QCOMPARE(currentParagraph->type, MarkdownNodeType::Paragraph);
        QVERIFY(!currentParagraph->children.isEmpty());
        const MarkdownNode* text = document.markdownDocument().nodeById(currentParagraph->children.first());
        QVERIFY(text);
        QCOMPARE(text->literal, QStringLiteral("Hallo"));
    }

    void nonTransactionHeadingEditUsesBlockReparseShadowMerge()
    {
        Document document;
        document.setMarkdown(QStringLiteral("# Hello\n\nbody"));
        const MarkdownNode* heading = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::Heading);
        QVERIFY(heading);
        const MarkdownNodeId headingId = heading->id;

        document.applyMarkdownEdit(QStringLiteral("# hello\n\nbody"), SourceSelection{3, 3}, QStringLiteral("Edit"));

        QVERIFY2(document.lastBlockParseRange().canReparseLocally(),
                 qPrintable(document.lastBlockParseRange().reason));
        QCOMPARE(document.lastBlockParseRange().affectedBlockIds, QVector<MarkdownNodeId>{headingId});
        QVERIFY(document.lastBlockReparseShadowResult().attempted);
        QVERIFY2(document.lastBlockReparseShadowResult().ok,
                 qPrintable(document.lastBlockReparseShadowResult().errors.join(QStringLiteral("; "))));
        QCOMPARE(document.lastBlockReparseShadowResult().merge.replacedNodeId, headingId);
        QVERIFY(document.lastBlockReparseShadowResult().appliedToDocument);
        QCOMPARE(document.lastParseMode(), ParseMode::LocalReparse);
        QCOMPARE(document.lastInvalidatedNodeIds(), QVector<MarkdownNodeId>{headingId});
        QCOMPARE(document.markdown(), QStringLiteral("# hello\n\nbody"));
        const MarkdownNode* currentHeading = document.markdownDocument().nodeById(headingId);
        QVERIFY(currentHeading);
        QCOMPARE(currentHeading->type, MarkdownNodeType::Heading);
        QCOMPARE(currentHeading->headingLevel, 1);
    }

    void localBlockReparseAppliesHeadingWhenEnabled()
    {
        Document document;
        document.setBlockLocalReparseEnabledForTesting(true);
        document.setMarkdown(QStringLiteral("# Hello\n\nbody"));
        const MarkdownNode* heading = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::Heading);
        QVERIFY(heading);
        const MarkdownNodeId headingId = heading->id;

        document.applyMarkdownEdit(QStringLiteral("# hello\n\nbody"), SourceSelection{3, 3}, QStringLiteral("Edit"));

        QVERIFY2(document.lastBlockReparseShadowResult().ok,
                 qPrintable(document.lastBlockReparseShadowResult().errors.join(QStringLiteral("; "))));
        QVERIFY(document.lastBlockReparseShadowResult().appliedToDocument);
        QCOMPARE(document.lastParseMode(), ParseMode::LocalReparse);
        const MarkdownNode* currentHeading = document.markdownDocument().nodeById(headingId);
        QVERIFY(currentHeading);
        QCOMPARE(currentHeading->type, MarkdownNodeType::Heading);
        QCOMPARE(currentHeading->headingLevel, 1);
        QVERIFY(!currentHeading->children.isEmpty());
        const MarkdownNode* text = document.markdownDocument().nodeById(currentHeading->children.first());
        QVERIFY(text);
        QCOMPARE(text->literal, QStringLiteral("hello"));
    }

    void localBlockReparseAppliesCodeBlockWhenEnabled()
    {
        Document document;
        document.setBlockLocalReparseEnabledForTesting(true);
        document.setMarkdown(QStringLiteral("```cpp\nint x;\n```"));
        const MarkdownNode* code = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::CodeBlock);
        QVERIFY(code);
        const MarkdownNodeId codeId = code->id;
        const int start = document.markdown().indexOf(QStringLiteral("x"));

        document.applyMarkdownEdit(QStringLiteral("```cpp\nint y;\n```"), SourceSelection{start + 1, start + 1}, QStringLiteral("Edit"));

        const BlockLocalReparseObservation observation = document.lastBlockLocalReparseObservation();
        QVERIFY2(observation.planned, qPrintable(observation.fallbackReason));
        QVERIFY(observation.shadowOk);
        QVERIFY(observation.applied);
        QCOMPARE(document.lastParseMode(), ParseMode::LocalReparse);
        QCOMPARE(document.lastInvalidatedNodeIds(), QVector<MarkdownNodeId>{codeId});
        QCOMPARE(document.lastPartialRenderResult().renderedNodeIds, QVector<MarkdownNodeId>{codeId});
        QCOMPARE(document.lastPartialRenderResult().blocks.size(), 1);
        QCOMPARE(document.lastPartialRenderResult().blocks.first().kind, RenderSpan::Kind::CodeBlock);
        const MarkdownNode* currentCode = document.markdownDocument().nodeById(codeId);
        QVERIFY(currentCode);
        QCOMPARE(currentCode->type, MarkdownNodeType::CodeBlock);
        QCOMPARE(currentCode->fenceInfo, QStringLiteral("cpp"));
        QCOMPARE(currentCode->literal, QStringLiteral("int y;\n"));
    }

    void renderedCodeBlockEditAppliesPartialPatchAndRestoresCursor()
    {
        Document document;
        document.setBlockLocalReparseEnabledForTesting(true);
        document.setPartialPatchRenderingEnabledForTesting(true);
        const QString beforeMarkdown = QStringLiteral("before\n\n```cpp\nold\n```\n\nafter");
        document.setMarkdown(beforeMarkdown);
        const MarkdownNode* code = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::CodeBlock);
        QVERIFY(code);
        const MarkdownNodeId codeId = code->id;
        const int oldOffset = beforeMarkdown.indexOf(QStringLiteral("old"));
        const QString afterMarkdown = QStringLiteral("before\n\n```cpp\nnew\n```\n\nafter");
        const int afterCursor = static_cast<int>(afterMarkdown.indexOf(QStringLiteral("new"))) + 3;
        EditTransaction transaction = codeBlockUpdateLiteralTransactionForDocument(document,
                                                                                  QStringLiteral("old\n"),
                                                                                  QStringLiteral("new\n"),
                                                                                  beforeMarkdown,
                                                                                  afterMarkdown,
                                                                                  {oldOffset, oldOffset + 3},
                                                                                  {afterCursor, afterCursor});
        QCOMPARE(transaction.affectedNodeIds, QVector<MarkdownNodeId>{codeId});
        QCOMPARE(transaction.operations.first().nodeId, codeId);
        EditTransactionValidationResult validation;
        validation.ok = true;

        QSignalSpy cursorSpy(&document, &Document::cursorSourceOffsetRequested);
        document.applyMarkdownEdit(std::move(transaction), validation, QStringLiteral("Edit Code"));

        QCOMPARE(document.markdown(), afterMarkdown);
        QVERIFY(document.lastStructuralRedoApplied());
        QCOMPARE(document.lastInvalidatedNodeIds(), QVector<MarkdownNodeId>{codeId});
        QCOMPARE(document.lastRenderUpdateBatch().nodeIds, QVector<MarkdownNodeId>{codeId});
        QCOMPARE(document.lastRenderUpdateBatch().reasons, QStringList{QStringLiteral("invalidated-nodes")});
        QVERIFY(document.lastBlockParseRange().requiresFullReparse);
        QVERIFY(!document.lastBlockReparseShadowResult().attempted);
        QCOMPARE(document.lastParseMode(), ParseMode::FullParse);
        const BlockLocalReparseObservation blockObservation = document.lastBlockLocalReparseObservation();
        QVERIFY(!blockObservation.planned);
        QVERIFY(!blockObservation.shadowAttempted);
        QVERIFY(!blockObservation.applied);

        const IncrementalRenderObservation renderObservation = document.lastIncrementalRenderObservation();
        QVERIFY(renderObservation.attempted);
        QVERIFY2(renderObservation.ok, qPrintable(renderObservation.errors.join(QStringLiteral("; "))));
        QVERIFY(renderObservation.appliedToDocument);
        QCOMPARE(renderObservation.validationMode, IncrementalRenderValidationMode::TrustLocalChecks);
        QCOMPARE(renderObservation.validationReason, QStringLiteral("single-block-code-block"));
        QCOMPARE(renderObservation.applyDecisionReason, QStringLiteral("partial-patch-allowed"));
        QCOMPARE(renderObservation.invalidatedNodeIds, QVector<MarkdownNodeId>{codeId});
        QCOMPARE(renderObservation.patchKind, RenderSpan::Kind::CodeBlock);
        const QString diagnostics = document.lastRenderDiagnosticsText();
        QVERIFY(diagnostics.contains(QStringLiteral("[parse]")));
        QVERIFY(diagnostics.contains(QStringLiteral("parseMode=FullParse")));
        QVERIFY(diagnostics.contains(QStringLiteral("[incremental-render]")));
        QVERIFY(diagnostics.contains(QStringLiteral("validationReason=single-block-code-block")));
        QVERIFY(diagnostics.contains(QStringLiteral("applyDecisionReason=partial-patch-allowed")));
        QVERIFY(diagnostics.contains(QStringLiteral("patchKind=CodeBlock")));
        QCOMPARE(document.lastRenderDiagnosticsSummary(), QStringLiteral("FP | IR: CodeBlock partial-patch-allowed"));
        QCOMPARE(document.lastPartialRenderResult().renderedNodeIds, QVector<MarkdownNodeId>{codeId});
        QCOMPARE(document.lastPartialRenderResult().blocks.size(), 1);
        QCOMPARE(document.lastPartialRenderResult().blocks.first().kind, RenderSpan::Kind::CodeBlock);
        QCOMPARE(document.lastPartialPatchPlan().steps.size(), 1);
        QCOMPARE(document.lastPartialPatchPlan().steps.first().nodeId, codeId);
        QCOMPARE(document.lastPartialPatchPlan().steps.first().kind, RenderSpan::Kind::CodeBlock);

        const MarkdownNode* currentCode = document.markdownDocument().nodeById(codeId);
        QVERIFY(currentCode);
        QCOMPARE(currentCode->id, codeId);
        QCOMPARE(currentCode->type, MarkdownNodeType::CodeBlock);
        QCOMPARE(currentCode->literal, QStringLiteral("new\n"));

        QCOMPARE(cursorSpy.size(), 1);
        const int expectedCursor = afterCursor;
        QCOMPARE(cursorSpy.takeFirst().at(0).toInt(), expectedCursor);
        const std::optional<int> renderedCursor = document.sourceMap().renderedPositionForSourceOffset(expectedCursor);
        QVERIFY(renderedCursor.has_value());
    }

    void renderedCodeBlockEditFallsBackToFullRenderWhenPartialPatchDisabled()
    {
        Document document;
        document.setPartialPatchRenderingEnabledForTesting(false);
        const QString beforeMarkdown = QStringLiteral("before\n\n```\nold\n```\n\nafter");
        document.setMarkdown(beforeMarkdown);
        const MarkdownNode* code = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::CodeBlock);
        QVERIFY(code);
        const MarkdownNodeId codeId = code->id;
        const int oldOffset = beforeMarkdown.indexOf(QStringLiteral("old"));
        const QString afterMarkdown = QStringLiteral("before\n\n```\nnew\n```\n\nafter");
        const int afterCursor = static_cast<int>(afterMarkdown.indexOf(QStringLiteral("new"))) + 3;
        EditTransaction transaction = codeBlockUpdateLiteralTransactionForDocument(document,
                                                                                  QStringLiteral("old\n"),
                                                                                  QStringLiteral("new\n"),
                                                                                  beforeMarkdown,
                                                                                  afterMarkdown,
                                                                                  {oldOffset, oldOffset + 3},
                                                                                  {afterCursor, afterCursor});
        EditTransactionValidationResult validation;
        validation.ok = true;

        document.applyMarkdownEdit(std::move(transaction), validation, QStringLiteral("Edit Code"));

        QCOMPARE(document.markdown(), afterMarkdown);
        QVERIFY(document.lastStructuralRedoApplied());
        QCOMPARE(document.lastInvalidatedNodeIds(), QVector<MarkdownNodeId>{codeId});
        QCOMPARE(document.lastRenderUpdateBatch().nodeIds, QVector<MarkdownNodeId>{codeId});
        QCOMPARE(document.lastRenderUpdateBatch().reasons, QStringList{QStringLiteral("invalidated-nodes")});
        QVERIFY(document.lastPartialPatchSimulationResult().attempted);
        QVERIFY2(document.lastPartialPatchSimulationResult().ok,
                 qPrintable(document.lastPartialPatchSimulationResult().errors.join(QStringLiteral("; "))));
        QVERIFY(!document.lastPartialPatchSimulationResult().appliedToDocument);
        const IncrementalRenderObservation observation = document.lastIncrementalRenderObservation();
        QVERIFY(observation.attempted);
        QVERIFY(observation.ok);
        QVERIFY(!observation.appliedToDocument);
        QCOMPARE(observation.validationMode, IncrementalRenderValidationMode::TrustLocalChecks);
        QCOMPARE(observation.validationReason, QStringLiteral("single-block-code-block"));
        QCOMPARE(observation.applyDecisionReason, QStringLiteral("partial-patch-disabled"));
        QCOMPARE(observation.patchKind, RenderSpan::Kind::CodeBlock);
        const QString diagnostics = document.lastRenderDiagnosticsText();
        QVERIFY(diagnostics.contains(QStringLiteral("validationReason=single-block-code-block")));
        QVERIFY(diagnostics.contains(QStringLiteral("applyDecisionReason=partial-patch-disabled")));
        QVERIFY(diagnostics.contains(QStringLiteral("appliedToDocument=false")));
        QCOMPARE(document.lastRenderDiagnosticsSummary(), QStringLiteral("FP | IR: CodeBlock partial-patch-disabled"));
        QVERIFY(document.textDocument()->toPlainText().contains(QStringLiteral("new")));
        const MarkdownNode* currentCode = document.markdownDocument().nodeById(codeId);
        QVERIFY(currentCode);
        QCOMPARE(currentCode->literal, QStringLiteral("new\n"));
    }

    void codeBlockFenceEditKeepsFullRenderFallback()
    {
        Document document;
        document.setBlockLocalReparseEnabledForTesting(true);
        document.setMarkdown(QStringLiteral("```cpp\nint x;\n```"));

        document.applyMarkdownEdit(QStringLiteral("```js\nint x;\n```"), SourceSelection{5, 5}, QStringLiteral("Edit"));

        const BlockLocalReparseObservation observation = document.lastBlockLocalReparseObservation();
        QVERIFY(!observation.planned);
        QCOMPARE(observation.fallbackReason, QStringLiteral("Edit touches code block fence."));
        QCOMPARE(document.lastParseMode(), ParseMode::FullParse);
        QCOMPARE(document.lastRenderDiagnosticsSummary(), QStringLiteral("FP | BR: Edit touches code block fence."));
        QVERIFY(!observation.shadowAttempted);
        QVERIFY(!observation.applied);
        QVERIFY(document.lastInvalidatedNodeIds().isEmpty());
    }

    void nonTransactionSimpleListEditUsesLocalReparse()
    {
        Document document;
        document.setMarkdown(QStringLiteral("- item\n"));

        document.applyMarkdownEdit(QStringLiteral("- Item\n"), SourceSelection{3, 3}, QStringLiteral("Edit"));

        QVERIFY2(document.lastBlockParseRange().canReparseLocally(),
                 qPrintable(document.lastBlockParseRange().reason));
        QCOMPARE(document.lastParseMode(), ParseMode::LocalReparse);
        QVERIFY(document.lastBlockReparseShadowResult().appliedToDocument);
        const BlockLocalReparseObservation observation = document.lastBlockLocalReparseObservation();
        QVERIFY(observation.planned);
        QVERIFY(observation.shadowOk);
        QVERIFY(observation.applied);
        QCOMPARE(document.markdown(), QStringLiteral("- Item\n"));
    }

    void nonTransactionLooseListEditKeepsFullRenderFallback()
    {
        Document document;
        document.setMarkdown(QStringLiteral("- A\n\n  B\n"));

        document.applyMarkdownEdit(QStringLiteral("- A\n\n  C\n"), SourceSelection{9, 9}, QStringLiteral("C"));

        QCOMPARE(document.lastParseMode(), ParseMode::FullParse);
        const BlockLocalReparseObservation observation = document.lastBlockLocalReparseObservation();
        QVERIFY(observation.fallbackReason.isEmpty() || !observation.applied);
    }

    void nonTransactionNewlineEditKeepsFullRenderFallback()
    {
        Document document;
        document.setMarkdown(QStringLiteral("Hello"));

        document.applyMarkdownEdit(QStringLiteral("Hello\nWorld"), SourceSelection{11, 11}, QStringLiteral("Edit"));

        QVERIFY(document.lastBlockParseRange().requiresFullReparse);
        QCOMPARE(document.lastBlockParseRange().reason, QStringLiteral("Replacement changes block shape."));
        QVERIFY(!document.lastBlockReparseShadowResult().attempted);
        QVERIFY(!document.lastBlockReparseShadowResult().appliedToDocument);
        QCOMPARE(document.lastParseMode(), ParseMode::FullParse);
        const BlockLocalReparseObservation observation = document.lastBlockLocalReparseObservation();
        QVERIFY(!observation.planned);
        QCOMPARE(observation.fallbackReason, QStringLiteral("Replacement changes block shape."));
        QVERIFY(document.lastInvalidatedNodeIds().isEmpty());
        QVERIFY(document.lastPartialRenderResult().blocks.isEmpty());
        QVERIFY(document.lastPartialPatchPlan().steps.isEmpty());
        QVERIFY(document.lastPartialPatchDryRun().errors.isEmpty());
        QVERIFY(!document.lastPartialPatchSimulationResult().attempted);
    }

    void setMarkdownClearsLastInvalidatedNodeIds()
    {
        Document document;
        document.setMarkdown(QStringLiteral("Hello"));
        const MarkdownNode* paragraph = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::Paragraph);
        const MarkdownNode* text = firstNodeOfType(document.markdownDocument(), MarkdownNodeType::Text);
        QVERIFY(paragraph);
        QVERIFY(text);

        EditTransaction transaction;
        transaction.kind = EditTransactionKind::RenderedEdit;
        transaction.affectedNodeIds = {text->id};
        transaction.beforeNodes = {textSnapshot(text->id, paragraph->id, QStringLiteral("Hello"))};
        transaction.afterNodes = {textSnapshot(text->id, paragraph->id, QStringLiteral("Hello Qt"))};
        transaction.beforeMarkdown = QStringLiteral("Hello");
        transaction.afterMarkdown = QStringLiteral("Hello Qt");
        document.applyMarkdownEdit(transaction, QStringLiteral("Edit"));
        QVERIFY(!document.lastInvalidatedNodeIds().isEmpty());
        QVERIFY(!document.lastPartialRenderResult().blocks.isEmpty());

        document.setMarkdown(QStringLiteral("Reset"));

        QVERIFY(document.lastInvalidatedNodeIds().isEmpty());
        QVERIFY(document.lastBlockParseRange().requiresFullReparse);
        QVERIFY(document.lastBlockParseRange().reason.isEmpty());
        QVERIFY(!document.lastBlockReparseShadowResult().attempted);
        const BlockLocalReparseObservation observation = document.lastBlockLocalReparseObservation();
        QVERIFY(!observation.planned);
        QVERIFY(observation.fallbackReason.isEmpty());
        QVERIFY(document.lastPartialRenderResult().blocks.isEmpty());
        QVERIFY(document.lastPartialPatchPlan().steps.isEmpty());
        QVERIFY(document.lastPartialPatchDryRun().errors.isEmpty());
        QVERIFY(!document.lastPartialPatchSimulationResult().attempted);
    }
};

QTEST_MAIN(TestDocument)
#include "test_document.moc"
