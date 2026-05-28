#include "EditorSelectionMapper.h"
#include "parser/SourceCoordinateMapper.h"

#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace Muffin {

SourceSelection EditorSelectionMapper::sourceSelectionForRenderedTarget(const QString& markdown,
                                                                        const RenderedCommandTarget& target,
                                                                        bool preferInlineText)
{
    const SourceCoordinateMapper mapper(markdown);
    SourceSpan span = mapper.spanForRange(target.range);
    if (!span.isValid()) {
        return {};
    }

    if (preferInlineText && !target.selectedText.isEmpty()) {
        const int relative = markdown.mid(span.start, span.end - span.start).indexOf(target.selectedText);
        if (relative >= 0) {
            span = {span.start + relative, span.start + relative + static_cast<int>(target.selectedText.size())};
        }
    }

    return {span.start, span.end};
}

SourceSelection EditorSelectionMapper::sourceSelectionForRenderedTarget(const QString& markdown,
                                                                        const QVector<MarkdownBlock>& blocks,
                                                                        const RenderedCommandTarget& target,
                                                                        bool preferInlineText)
{
    for (const MarkdownBlock& block : blocks) {
        if (block.sourceRange.startLine == target.range.startLine &&
            block.sourceRange.endLine == target.range.endLine) {
            SourceSpan span = preferInlineText && block.content.isValid() ? block.content : block.source;
            if (preferInlineText && !target.selectedText.isEmpty()) {
                const int relative = markdown.mid(span.start, span.end - span.start).indexOf(target.selectedText);
                if (relative >= 0) {
                    span = {span.start + relative, span.start + relative + static_cast<int>(target.selectedText.size())};
                }
            }
            return {span.start, span.end};
        }
    }
    return sourceSelectionForRenderedTarget(markdown, target, preferInlineText);
}

SourceSelection EditorSelectionMapper::sourceSelectionForEditor(const QPlainTextEdit* editor)
{
    if (!editor) {
        return {};
    }

    const QTextCursor cursor = editor->textCursor();
    return {cursor.selectionStart(), cursor.selectionEnd()};
}

bool EditorSelectionMapper::moveSourceCursorToRange(QPlainTextEdit* editor, SourceRange range, bool selectRange)
{
    if (!editor || range.startLine <= 0) {
        return false;
    }

    QTextDocument* sourceDocument = editor->document();
    QTextBlock startBlock = sourceDocument->findBlockByLineNumber(range.startLine - 1);
    if (!startBlock.isValid()) {
        return false;
    }

    const int startPosition = startBlock.position() + qMax(0, range.startColumn - 1);
    QTextCursor cursor(sourceDocument);
    cursor.setPosition(startPosition);

    if (selectRange && range.endLine >= range.startLine) {
        QTextBlock endBlock = sourceDocument->findBlockByLineNumber(range.endLine - 1);
        if (endBlock.isValid()) {
            const int endColumn = range.endColumn > 0 ? range.endColumn : endBlock.length() - 1;
            const int endPosition = endBlock.position() + qMax(0, endColumn);
            cursor.setPosition(qMax(startPosition, endPosition), QTextCursor::KeepAnchor);
        }
    }

    editor->setTextCursor(cursor);
    editor->centerCursor();
    return true;
}

bool EditorSelectionMapper::moveSourceCursorToInlineText(QPlainTextEdit* editor, SourceRange range, const QString& text)
{
    if (!editor || range.startLine <= 0 || text.isEmpty()) {
        return false;
    }

    QTextDocument* sourceDocument = editor->document();
    QTextBlock startBlock = sourceDocument->findBlockByLineNumber(range.startLine - 1);
    QTextBlock endBlock = sourceDocument->findBlockByLineNumber(qMax(range.startLine, range.endLine) - 1);
    if (!startBlock.isValid() || !endBlock.isValid()) {
        return false;
    }

    const int startPosition = startBlock.position();
    const int endPosition = endBlock.position() + endBlock.length() - 1;
    QTextCursor cursor(sourceDocument);
    cursor.setPosition(startPosition);
    cursor.setPosition(endPosition, QTextCursor::KeepAnchor);

    const QString sourceText = cursor.selectedText();
    const QString normalizedNeedle = text.simplified();
    int relativePosition = sourceText.indexOf(text);
    int matchLength = text.size();

    if (relativePosition < 0) {
        relativePosition = sourceText.simplified().indexOf(normalizedNeedle);
        matchLength = normalizedNeedle.size();
    }

    if (relativePosition < 0) {
        return false;
    }

    cursor.clearSelection();
    cursor.setPosition(startPosition + relativePosition);
    cursor.setPosition(startPosition + relativePosition + matchLength, QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor);
    editor->centerCursor();
    return true;
}

} // namespace Muffin
