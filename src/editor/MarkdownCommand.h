#pragma once

#include <QString>
#include <QtGlobal>

class QPlainTextEdit;

namespace Muffin {

struct SourceSelection {
    int start = 0;
    int end = 0;

    int normalizedStart() const { return qMin(start, end); }
    int normalizedEnd() const { return qMax(start, end); }
    bool isValidFor(const QString& text) const;
};

struct MarkdownCommandResult {
    bool changed = false;
    QString markdown;
    SourceSelection selection;
    QString error;
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
    static MarkdownCommandResult insertLink(const QString& markdown, SourceSelection selection);

    static MarkdownCommandResult applyHeading(const QString& markdown, SourceSelection selection, int level);
    static MarkdownCommandResult applyParagraph(const QString& markdown, SourceSelection selection);
    static MarkdownCommandResult applyQuote(const QString& markdown, SourceSelection selection);
    static MarkdownCommandResult applyList(const QString& markdown, SourceSelection selection, ListType type);

    static void toggleBold(QPlainTextEdit* editor);
    static void toggleItalic(QPlainTextEdit* editor);
    static void toggleUnderline(QPlainTextEdit* editor);
    static void toggleInlineCode(QPlainTextEdit* editor);
    static void insertLink(QPlainTextEdit* editor);

    static void applyHeading(QPlainTextEdit* editor, int level);
    static void applyParagraph(QPlainTextEdit* editor);
    static void applyQuote(QPlainTextEdit* editor);
    static void applyList(QPlainTextEdit* editor, ListType type);

};

} // namespace Muffin
