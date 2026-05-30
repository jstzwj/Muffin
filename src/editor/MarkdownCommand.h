#pragma once

#include "editor/EditTransaction.h"
#include "editor/SourceSelection.h"

#include <QMetaType>
#include <QString>

#include <optional>

class QPlainTextEdit;

namespace Muffin {

struct MarkdownCommandResult {
    bool changed = false;
    QString markdown;
    SourceSelection selection;
    QString error;
    std::optional<EditTransaction> transaction;
    std::optional<EditTransactionValidationResult> transactionValidation;
};

class MarkdownCommand
{
public:
    enum class ListType {
        Ordered,
        Unordered,
        Task
    };

    static MarkdownCommandResult toggleBold(const QString& markdown, SourceSelection selection);
    static MarkdownCommandResult toggleItalic(const QString& markdown, SourceSelection selection);
    static MarkdownCommandResult toggleUnderline(const QString& markdown, SourceSelection selection);
    static MarkdownCommandResult toggleInlineCode(const QString& markdown, SourceSelection selection);
    static MarkdownCommandResult toggleStrikethrough(const QString& markdown, SourceSelection selection);
    static MarkdownCommandResult insertLink(const QString& markdown, SourceSelection selection);

    static MarkdownCommandResult applyHeading(const QString& markdown, SourceSelection selection, int level);
    static MarkdownCommandResult applyParagraph(const QString& markdown, SourceSelection selection);
    static MarkdownCommandResult applyQuote(const QString& markdown, SourceSelection selection);
    static MarkdownCommandResult applyList(const QString& markdown, SourceSelection selection, ListType type);

    static void toggleBold(QPlainTextEdit* editor);
    static void toggleItalic(QPlainTextEdit* editor);
    static void toggleUnderline(QPlainTextEdit* editor);
    static void toggleInlineCode(QPlainTextEdit* editor);
    static void toggleStrikethrough(QPlainTextEdit* editor);
    static void insertLink(QPlainTextEdit* editor);

    static void applyHeading(QPlainTextEdit* editor, int level);
    static void applyParagraph(QPlainTextEdit* editor);
    static void applyQuote(QPlainTextEdit* editor);
    static void applyList(QPlainTextEdit* editor, ListType type);

};

} // namespace Muffin

Q_DECLARE_METATYPE(Muffin::SourceSelection)
