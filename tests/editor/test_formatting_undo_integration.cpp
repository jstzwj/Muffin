#include "core/Document.h"
#include "editor/MarkdownEditEngine.h"
#include "editor/StyleCommandHandler.h"
#include "editor/EditTransaction.h"

#include <QTest>

using namespace Muffin;

class TestFormattingUndoIntegration : public QObject
{
    Q_OBJECT

private:
    void applyCommandAndUndoRedo(
        const QString& initialMarkdown,
        std::function<MarkdownCommandResult(const Document&)> command,
        const QString& expectedAfterMarkdown)
    {
        Document document;
        document.setMarkdown(initialMarkdown);

        MarkdownCommandResult result = command(document);
        QVERIFY2(result.changed, "Command should report a change");
        QVERIFY2(result.transaction.has_value(), "Command should produce an EditTransaction");

        EditTransaction txn = std::move(*result.transaction);
        document.applyMarkdownEdit(std::move(txn),
                                   result.transactionValidation,
                                   QStringLiteral("Test Edit"));
        QCOMPARE(document.markdown(), expectedAfterMarkdown);

        StructuralEditObservation obs = document.lastStructuralEditObservation();
        QCOMPARE(obs.redoStatus, StructuralEditStatus::Ready);

        document.undoStack()->undo();
        QCOMPARE(document.markdown(), initialMarkdown);

        obs = document.lastStructuralEditObservation();
        QCOMPARE(obs.undoStatus, StructuralEditStatus::Ready);
        QVERIFY2(document.lastStructuralUndoApplied(), "Undo should have applied structurally");

        document.undoStack()->redo();
        QCOMPARE(document.markdown(), expectedAfterMarkdown);

        obs = document.lastStructuralEditObservation();
        QCOMPARE(obs.redoStatus, StructuralEditStatus::Ready);
        QVERIFY2(document.lastStructuralRedoApplied(), "Redo should have applied structurally");
    }

private slots:
    void structuralUndoFallsBackGracefully()
    {
        Document document;
        document.setMarkdown(QStringLiteral("Hello"));
        document.applyMarkdownEdit(QStringLiteral("Hello Qt"), 8, QStringLiteral("Type"));

        QCOMPARE(document.markdown(), QStringLiteral("Hello Qt"));

        document.undoStack()->undo();
        QCOMPARE(document.markdown(), QStringLiteral("Hello"));

        StructuralEditObservation observation = document.lastStructuralEditObservation();
        QCOMPARE(observation.undoStatus, StructuralEditStatus::NotApplicable);
    }

    void boldToggleUndoRedo()
    {
        applyCommandAndUndoRedo(
            QStringLiteral("Hello world"),
            [](const Document& doc) {
                return MarkdownEditEngine::applyInlineStyleCommand(
                    doc.markdownDocument(), {0, 5},
                    StyleCommandHandler::InlineStyle::Strong,
                    &MarkdownCommand::toggleBold);
            },
            QStringLiteral("**Hello** world"));
    }

    void italicToggleUndoRedo()
    {
        applyCommandAndUndoRedo(
            QStringLiteral("Hello world"),
            [](const Document& doc) {
                return MarkdownEditEngine::applyInlineStyleCommand(
                    doc.markdownDocument(), {0, 5},
                    StyleCommandHandler::InlineStyle::Emphasis,
                    &MarkdownCommand::toggleItalic);
            },
            QStringLiteral("*Hello* world"));
    }

    void cursorRestoredAfterBoldUndo()
    {
        Document document;
        document.setMarkdown(QStringLiteral("Hello world"));

        MarkdownCommandResult result = MarkdownEditEngine::applyInlineStyleCommand(
            document.markdownDocument(), {0, 5},
            StyleCommandHandler::InlineStyle::Strong,
            &MarkdownCommand::toggleBold);
        QVERIFY(result.changed);
        QVERIFY(result.transaction.has_value());
        const SourceSelection beforeSelection = result.transaction->beforeSelection;

        EditTransaction txn = std::move(*result.transaction);
        document.applyMarkdownEdit(std::move(txn), result.transactionValidation, QStringLiteral("Bold"));
        QCOMPARE(document.markdown(), QStringLiteral("**Hello** world"));

        SourceSelection undoSelection{-1, -1};
        QObject::connect(&document, &Document::sourceSelectionRequested,
                         [&undoSelection](SourceSelection sel) { undoSelection = sel; });

        document.undoStack()->undo();
        QCOMPARE(document.markdown(), QStringLiteral("Hello world"));
        QVERIFY2(undoSelection.start >= 0, "Undo should emit source selection");
        QCOMPARE(undoSelection.start, beforeSelection.start);
    }

    void cursorRestoredAfterBoldRedo()
    {
        Document document;
        document.setMarkdown(QStringLiteral("Hello world"));

        MarkdownCommandResult result = MarkdownEditEngine::applyInlineStyleCommand(
            document.markdownDocument(), {0, 5},
            StyleCommandHandler::InlineStyle::Strong,
            &MarkdownCommand::toggleBold);
        QVERIFY(result.changed);
        QVERIFY(result.transaction.has_value());
        const SourceSelection afterSelection = result.selection;

        EditTransaction txn = std::move(*result.transaction);
        document.applyMarkdownEdit(std::move(txn), result.transactionValidation, QStringLiteral("Bold"));

        document.undoStack()->undo();

        SourceSelection redoSelection{-1, -1};
        QObject::connect(&document, &Document::sourceSelectionRequested,
                         [&redoSelection](SourceSelection sel) { redoSelection = sel; });

        document.undoStack()->redo();
        QCOMPARE(document.markdown(), QStringLiteral("**Hello** world"));
        QVERIFY2(redoSelection.start >= 0, "Redo should emit source selection");
        QCOMPARE(redoSelection.start, afterSelection.start);
    }

    void cursorRestoredAfterHeadingUndo()
    {
        Document document;
        document.setMarkdown(QStringLiteral("Title"));

        MarkdownCommandResult result = MarkdownEditEngine::applyHeadingCommand(
            document.markdownDocument(), {0, 5}, 2);
        QVERIFY(result.changed);
        QVERIFY(result.transaction.has_value());
        const SourceSelection beforeSelection = result.transaction->beforeSelection;

        EditTransaction txn = std::move(*result.transaction);
        document.applyMarkdownEdit(std::move(txn), result.transactionValidation, QStringLiteral("Heading"));
        QCOMPARE(document.markdown(), QStringLiteral("## Title"));

        SourceSelection undoSelection{-1, -1};
        QObject::connect(&document, &Document::sourceSelectionRequested,
                         [&undoSelection](SourceSelection sel) { undoSelection = sel; });

        document.undoStack()->undo();
        QCOMPARE(document.markdown(), QStringLiteral("Title"));
        QVERIFY2(undoSelection.start >= 0, "Undo should emit source selection");
        QCOMPARE(undoSelection.start, beforeSelection.start);
    }

    void cursorRestoredAfterListUndo()
    {
        Document document;
        document.setMarkdown(QStringLiteral("Hello"));

        MarkdownCommandResult result = MarkdownEditEngine::applyListCommand(
            document.markdownDocument(), {0, 5}, MarkdownCommand::ListType::Unordered);
        QVERIFY(result.changed);
        QVERIFY(result.transaction.has_value());
        const SourceSelection beforeSelection = result.transaction->beforeSelection;

        EditTransaction txn = std::move(*result.transaction);
        document.applyMarkdownEdit(std::move(txn), result.transactionValidation, QStringLiteral("List"));
        QCOMPARE(document.markdown(), QStringLiteral("- Hello"));

        SourceSelection undoSelection{-1, -1};
        QObject::connect(&document, &Document::sourceSelectionRequested,
                         [&undoSelection](SourceSelection sel) { undoSelection = sel; });

        document.undoStack()->undo();
        QCOMPARE(document.markdown(), QStringLiteral("Hello"));
        QVERIFY2(undoSelection.start >= 0, "Undo should emit source selection");
        QCOMPARE(undoSelection.start, beforeSelection.start);
    }
};

QTEST_MAIN(TestFormattingUndoIntegration)
#include "test_formatting_undo_integration.moc"
