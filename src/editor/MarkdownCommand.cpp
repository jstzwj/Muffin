#include "MarkdownCommand.h"

#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QString>
#include <QTextCursor>
#include <functional>

namespace Muffin {

namespace {

MarkdownCommandResult unchanged(const QString& markdown, SourceSelection selection)
{
    return {false, markdown, selection, {}};
}

MarkdownCommandResult replaceRange(QString markdown, SourceSelection selection, const QString& replacement)
{
    const int start = selection.normalizedStart();
    const int end = selection.normalizedEnd();
    if (start < 0 || end < start || end > markdown.size()) {
        return {false, markdown, selection, QStringLiteral("Invalid source selection.")};
    }
    markdown.replace(start, end - start, replacement);
    const int cursor = start + replacement.size();
    return {true, markdown, {cursor, cursor}, {}};
}

MarkdownCommandResult wrapSourceSelection(const QString& markdown, SourceSelection selection,
                                          const QString& before, const QString& after)
{
    if (!selection.isValidFor(markdown)) {
        return {false, markdown, selection, QStringLiteral("Invalid source selection.")};
    }
    const int start = selection.normalizedStart();
    const int end = selection.normalizedEnd();
    const QString selected = start == end ? QStringLiteral("text") : markdown.mid(start, end - start);
    return replaceRange(markdown, selection, before + selected + after);
}

SourceSelection lineSelectionForOffset(const QString& markdown, int offset)
{
    offset = qBound(0, offset, markdown.size());
    int lineStart = markdown.lastIndexOf(QChar('\n'), qMax(0, offset - 1));
    lineStart = lineStart < 0 ? 0 : lineStart + 1;
    const int lineEndIndex = markdown.indexOf(QChar('\n'), offset);
    const int lineEnd = lineEndIndex < 0 ? markdown.size() : lineEndIndex;
    return {lineStart, lineEnd};
}

MarkdownCommandResult applyLineTransform(const QString& markdown, SourceSelection selection,
                                         const std::function<QString(QString, int)>& transform)
{
    if (!selection.isValidFor(markdown)) {
        return {false, markdown, selection, QStringLiteral("Invalid source selection.")};
    }
    const int selectionStart = selection.normalizedStart();
    const int selectionEnd = selection.normalizedEnd();
    SourceSelection lineSelection = lineSelectionForOffset(markdown, selectionStart);
    if (selectionEnd > selectionStart) {
        const SourceSelection endLine = lineSelectionForOffset(markdown, qMax(selectionStart, selectionEnd - 1));
        lineSelection.end = endLine.end;
    }

    QString block = markdown.mid(lineSelection.start, lineSelection.end - lineSelection.start);
    const QStringList lines = block.split(QChar('\n'));
    QStringList transformed;
    transformed.reserve(lines.size());
    for (int i = 0; i < lines.size(); ++i) {
        transformed.append(transform(lines.at(i), i + 1));
    }
    return replaceRange(markdown, lineSelection, transformed.join(QChar('\n')));
}

void applyResultToEditor(QPlainTextEdit* editor, const MarkdownCommandResult& result)
{
    if (!editor || !result.changed) {
        return;
    }
    QTextCursor cursor = editor->textCursor();
    editor->setPlainText(result.markdown);
    cursor = editor->textCursor();
    cursor.setPosition(qBound(0, result.selection.normalizedStart(), result.markdown.size()));
    editor->setTextCursor(cursor);
}

SourceSelection editorSelection(QPlainTextEdit* editor)
{
    QTextCursor cursor = editor->textCursor();
    return {cursor.selectionStart(), cursor.selectionEnd()};
}

} // namespace

bool SourceSelection::isValidFor(const QString& text) const
{
    return start >= 0 && end >= 0 && start <= text.size() && end <= text.size();
}

MarkdownCommandResult MarkdownCommand::toggleBold(const QString& markdown, SourceSelection selection)
{
    return wrapSourceSelection(markdown, selection, QStringLiteral("**"), QStringLiteral("**"));
}

MarkdownCommandResult MarkdownCommand::toggleItalic(const QString& markdown, SourceSelection selection)
{
    return wrapSourceSelection(markdown, selection, QStringLiteral("*"), QStringLiteral("*"));
}

MarkdownCommandResult MarkdownCommand::toggleUnderline(const QString& markdown, SourceSelection selection)
{
    return wrapSourceSelection(markdown, selection, QStringLiteral("<u>"), QStringLiteral("</u>"));
}

MarkdownCommandResult MarkdownCommand::toggleInlineCode(const QString& markdown, SourceSelection selection)
{
    return wrapSourceSelection(markdown, selection, QStringLiteral("`"), QStringLiteral("`"));
}

MarkdownCommandResult MarkdownCommand::insertLink(const QString& markdown, SourceSelection selection)
{
    return wrapSourceSelection(markdown, selection, QStringLiteral("["), QStringLiteral("](url)"));
}

MarkdownCommandResult MarkdownCommand::applyHeading(const QString& markdown, SourceSelection selection, int level)
{
    level = qBound(1, level, 6);
    return applyLineTransform(markdown, selection, [level](QString line, int) {
        line.remove(QRegularExpression(QStringLiteral("^#{1,6}\\s+")));
        return QString(level, QChar('#')) + QStringLiteral(" ") + line;
    });
}

MarkdownCommandResult MarkdownCommand::applyParagraph(const QString& markdown, SourceSelection selection)
{
    return applyLineTransform(markdown, selection, [](QString line, int) {
        line.remove(QRegularExpression(QStringLiteral("^#{1,6}\\s+")));
        return line;
    });
}

MarkdownCommandResult MarkdownCommand::applyQuote(const QString& markdown, SourceSelection selection)
{
    return applyLineTransform(markdown, selection, [](const QString& line, int) {
        return QStringLiteral("> ") + line;
    });
}

MarkdownCommandResult MarkdownCommand::applyList(const QString& markdown, SourceSelection selection, ListType type)
{
    return applyLineTransform(markdown, selection, [type](const QString& line, int index) {
        switch (type) {
        case ListType::Ordered:
            return QStringLiteral("%1. ").arg(index) + line;
        case ListType::Unordered:
            return QStringLiteral("- ") + line;
        case ListType::Task:
            return QStringLiteral("- [ ] ") + line;
        }
        return line;
    });
}

void MarkdownCommand::toggleBold(QPlainTextEdit* editor)
{
    if (!editor) return;
    applyResultToEditor(editor, toggleBold(editor->toPlainText(), editorSelection(editor)));
}

void MarkdownCommand::toggleItalic(QPlainTextEdit* editor)
{
    if (!editor) return;
    applyResultToEditor(editor, toggleItalic(editor->toPlainText(), editorSelection(editor)));
}

void MarkdownCommand::toggleUnderline(QPlainTextEdit* editor)
{
    if (!editor) return;
    applyResultToEditor(editor, toggleUnderline(editor->toPlainText(), editorSelection(editor)));
}

void MarkdownCommand::toggleInlineCode(QPlainTextEdit* editor)
{
    if (!editor) return;
    applyResultToEditor(editor, toggleInlineCode(editor->toPlainText(), editorSelection(editor)));
}

void MarkdownCommand::insertLink(QPlainTextEdit* editor)
{
    if (!editor) return;
    applyResultToEditor(editor, insertLink(editor->toPlainText(), editorSelection(editor)));
}

void MarkdownCommand::applyHeading(QPlainTextEdit* editor, int level)
{
    if (!editor) return;
    applyResultToEditor(editor, applyHeading(editor->toPlainText(), editorSelection(editor), level));
}

void MarkdownCommand::applyParagraph(QPlainTextEdit* editor)
{
    if (!editor) return;
    applyResultToEditor(editor, applyParagraph(editor->toPlainText(), editorSelection(editor)));
}

void MarkdownCommand::applyQuote(QPlainTextEdit* editor)
{
    if (!editor) return;
    applyResultToEditor(editor, applyQuote(editor->toPlainText(), editorSelection(editor)));
}

void MarkdownCommand::applyList(QPlainTextEdit* editor, ListType type)
{
    if (!editor) return;
    applyResultToEditor(editor, applyList(editor->toPlainText(), editorSelection(editor), type));
}


} // namespace Muffin
