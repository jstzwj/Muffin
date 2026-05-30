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

SelectionBookmark EditorSelectionMapper::bookmarkForSourceOffset(const MarkdownDocument& document,
                                                                 int sourceOffset,
                                                                 SelectionBookmark::Affinity affinity)
{
    if (sourceOffset < 0) {
        return {};
    }

    const MarkdownNode* node = document.nodeAtSourceOffset(sourceOffset);
    if (!node) {
        node = document.blockAtSourceOffset(sourceOffset);
    }
    if (!node) {
        return {};
    }

    const SourceSpan source = node->content.isValid() ? node->content : node->source;
    const int offsetInNode = source.isValid() && sourceOffset >= source.start && sourceOffset <= source.end
        ? sourceOffset - source.start
        : -1;
    return {node->id, sourceOffset, offsetInNode, affinity};
}

SelectionRangeBookmark EditorSelectionMapper::bookmarkForSourceSelection(const MarkdownDocument& document, SourceSelection selection)
{
    return {
        bookmarkForSourceOffset(document, selection.start, SelectionBookmark::Affinity::Forward),
        bookmarkForSourceOffset(document, selection.end, SelectionBookmark::Affinity::Backward)
    };
}

std::optional<SourceSelection> EditorSelectionMapper::sourceSelectionForBookmark(const SelectionRangeBookmark& bookmark)
{
    if (!bookmark.isValid()) {
        return std::nullopt;
    }

    return SourceSelection{
        bookmark.anchor.sourceOffset,
        bookmark.focus.sourceOffset
    };
}

std::optional<SourceSelection> EditorSelectionMapper::orderedSourceSelectionForBookmark(const SelectionRangeBookmark& bookmark)
{
    std::optional<SourceSelection> selection = sourceSelectionForBookmark(bookmark);
    if (!selection) {
        return std::nullopt;
    }
    return SourceSelection{selection->normalizedStart(), selection->normalizedEnd()};
}

namespace {

RenderSourceMap::Bias biasForBookmark(const SelectionBookmark& bookmark, RenderSourceMap::Bias fallback)
{
    switch (bookmark.affinity) {
    case SelectionBookmark::Affinity::Backward:
        return RenderSourceMap::Bias::Backward;
    case SelectionBookmark::Affinity::Forward:
        return RenderSourceMap::Bias::Forward;
    case SelectionBookmark::Affinity::None:
        return fallback;
    }
    return fallback;
}

} // namespace

std::optional<int> EditorSelectionMapper::renderedPositionForBookmark(const RenderSourceMap& sourceMap,
                                                                      const SelectionBookmark& bookmark,
                                                                      RenderSourceMap::Bias bias)
{
    if (!bookmark.isValid()) {
        return std::nullopt;
    }

    bias = biasForBookmark(bookmark, bias);
    if (std::optional<RenderSpan> nodeSpan = sourceMap.spanForNode(bookmark.nodeId)) {
        const SourceSpan source = nodeSpan->editSource.isValid() ? nodeSpan->editSource : nodeSpan->source;
        if (source.isValid() && bookmark.offsetInNode >= 0) {
            const int restoredSourceOffset = source.start + qBound(0, bookmark.offsetInNode, source.end - source.start);
            return sourceMap.renderedPositionForSourceOffset(restoredSourceOffset, bias);
        }
        if (source.isValid() && bookmark.sourceOffset >= source.start && bookmark.sourceOffset <= source.end) {
            return sourceMap.renderedPositionForSourceOffset(bookmark.sourceOffset, bias);
        }
        return bias == RenderSourceMap::Bias::Forward ? nodeSpan->renderedStart : nodeSpan->renderedEnd;
    }

    return sourceMap.renderedPositionForSourceOffset(bookmark.sourceOffset, bias);
}

std::optional<SourceSelection> EditorSelectionMapper::renderedSelectionForBookmark(const RenderSourceMap& sourceMap,
                                                                                  const SelectionRangeBookmark& bookmark)
{
    if (!bookmark.isValid()) {
        return std::nullopt;
    }

    const std::optional<int> anchor = renderedPositionForBookmark(sourceMap, bookmark.anchor, RenderSourceMap::Bias::Forward);
    const std::optional<int> focus = renderedPositionForBookmark(sourceMap, bookmark.focus, RenderSourceMap::Bias::Backward);
    if (!anchor || !focus) {
        return std::nullopt;
    }

    return SourceSelection{*anchor, *focus};
}

std::optional<SourceSelection> EditorSelectionMapper::orderedRenderedSelectionForBookmark(const RenderSourceMap& sourceMap,
                                                                                         const SelectionRangeBookmark& bookmark)
{
    std::optional<SourceSelection> selection = renderedSelectionForBookmark(sourceMap, bookmark);
    if (!selection) {
        return std::nullopt;
    }
    return SourceSelection{selection->normalizedStart(), selection->normalizedEnd()};
}

RenderedSelectionRestoreResult EditorSelectionMapper::restoreRenderedSelection(
    const RenderSourceMap& sourceMap,
    const RenderedSelectionRestoreRequest& request)
{
    RenderedSelectionRestoreResult result;
    if (request.rangeBookmark) {
        result.selection = orderedRenderedSelectionForBookmark(sourceMap, *request.rangeBookmark);
        if (result.selection) {
            result.method = RenderedSelectionRestoreMethod::RangeBookmark;
            return result;
        }
    }
    if (request.caretBookmark) {
        result.cursorPosition = renderedPositionForBookmark(sourceMap,
                                                            *request.caretBookmark,
                                                            RenderSourceMap::Bias::Backward);
        if (result.cursorPosition) {
            result.method = RenderedSelectionRestoreMethod::CaretBookmark;
            return result;
        }
    }
    if (request.fallbackSourceOffset) {
        result.cursorPosition = sourceMap.renderedPositionForSourceOffset(*request.fallbackSourceOffset,
                                                                          RenderSourceMap::Bias::Backward);
        if (result.cursorPosition) {
            result.method = RenderedSelectionRestoreMethod::SourceOffset;
        }
    }
    return result;
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
