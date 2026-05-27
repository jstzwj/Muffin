#pragma once

#include <QString>

class QPlainTextEdit;

namespace Muffin {

class MarkdownCommand
{
public:
    enum class ListType {
        Ordered,
        Unordered,
        Task
    };

    static void toggleBold(QPlainTextEdit* editor);
    static void toggleItalic(QPlainTextEdit* editor);
    static void toggleUnderline(QPlainTextEdit* editor);
    static void toggleInlineCode(QPlainTextEdit* editor);
    static void insertLink(QPlainTextEdit* editor);

    static void applyHeading(QPlainTextEdit* editor, int level);
    static void applyParagraph(QPlainTextEdit* editor);
    static void applyQuote(QPlainTextEdit* editor);
    static void applyList(QPlainTextEdit* editor, ListType type);

private:
    static void wrapSelection(QPlainTextEdit* editor, const QString& before, const QString& after);
    static void applyLinePrefix(QPlainTextEdit* editor, const QString& prefix, bool ordered = false);
};

} // namespace Muffin
