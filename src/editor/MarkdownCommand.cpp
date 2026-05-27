#include "MarkdownCommand.h"

#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QString>
#include <QTextCursor>

namespace Muffin {

void MarkdownCommand::toggleBold(QPlainTextEdit* editor)
{
    wrapSelection(editor, QStringLiteral("**"), QStringLiteral("**"));
}

void MarkdownCommand::toggleItalic(QPlainTextEdit* editor)
{
    wrapSelection(editor, QStringLiteral("*"), QStringLiteral("*"));
}

void MarkdownCommand::toggleUnderline(QPlainTextEdit* editor)
{
    wrapSelection(editor, QStringLiteral("<u>"), QStringLiteral("</u>"));
}

void MarkdownCommand::toggleInlineCode(QPlainTextEdit* editor)
{
    wrapSelection(editor, QStringLiteral("`"), QStringLiteral("`"));
}

void MarkdownCommand::insertLink(QPlainTextEdit* editor)
{
    wrapSelection(editor, QStringLiteral("["), QStringLiteral("](url)"));
}

void MarkdownCommand::applyHeading(QPlainTextEdit* editor, int level)
{
    if (!editor) {
        return;
    }

    level = qBound(1, level, 6);
    QTextCursor cursor = editor->textCursor();
    cursor.movePosition(QTextCursor::StartOfLine);
    cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
    QString line = cursor.selectedText();
    line.remove(QRegularExpression(QStringLiteral("^#{1,6}\\s+")));
    cursor.insertText(QString(level, QChar('#')) + QStringLiteral(" ") + line);
    editor->setTextCursor(cursor);
}

void MarkdownCommand::applyParagraph(QPlainTextEdit* editor)
{
    if (!editor) {
        return;
    }

    QTextCursor cursor = editor->textCursor();
    cursor.movePosition(QTextCursor::StartOfLine);
    cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
    QString line = cursor.selectedText();
    line.remove(QRegularExpression(QStringLiteral("^#{1,6}\\s+")));
    cursor.insertText(line);
    editor->setTextCursor(cursor);
}

void MarkdownCommand::applyQuote(QPlainTextEdit* editor)
{
    applyLinePrefix(editor, QStringLiteral("> "));
}

void MarkdownCommand::applyList(QPlainTextEdit* editor, ListType type)
{
    switch (type) {
    case ListType::Ordered:
        applyLinePrefix(editor, {}, true);
        break;
    case ListType::Unordered:
        applyLinePrefix(editor, QStringLiteral("- "));
        break;
    case ListType::Task:
        applyLinePrefix(editor, QStringLiteral("- [ ] "));
        break;
    }
}

void MarkdownCommand::wrapSelection(QPlainTextEdit* editor, const QString& before, const QString& after)
{
    if (!editor) {
        return;
    }

    QTextCursor cursor = editor->textCursor();
    const QString selected = cursor.hasSelection() ? cursor.selectedText() : QStringLiteral("text");
    cursor.insertText(before + selected + after);
    editor->setTextCursor(cursor);
}

void MarkdownCommand::applyLinePrefix(QPlainTextEdit* editor, const QString& prefix, bool ordered)
{
    if (!editor) {
        return;
    }

    QTextCursor cursor = editor->textCursor();
    cursor.beginEditBlock();
    const int start = qMin(cursor.selectionStart(), cursor.selectionEnd());
    const int end = qMax(cursor.selectionStart(), cursor.selectionEnd());
    cursor.setPosition(start);
    cursor.movePosition(QTextCursor::StartOfLine);

    int index = 1;
    while (cursor.position() <= end) {
        cursor.insertText(ordered ? QStringLiteral("%1. ").arg(index++) : prefix);
        if (!cursor.movePosition(QTextCursor::Down)) {
            break;
        }
        cursor.movePosition(QTextCursor::StartOfLine);
    }
    cursor.endEditBlock();
    editor->setTextCursor(cursor);
}

} // namespace Muffin
