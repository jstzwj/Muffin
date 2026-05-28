#include "MarkdownPatch.h"

#include <QRegularExpression>

namespace Muffin {

namespace {

bool isHeadingOrParagraph(RenderSpan::Kind kind)
{
    return kind == RenderSpan::Kind::Heading || kind == RenderSpan::Kind::Paragraph;
}

QString headingMarkerAt(const QString& markdown, int sourceOffset)
{
    int lineStart = markdown.lastIndexOf(QChar('\n'), qMax(0, sourceOffset - 1));
    lineStart = lineStart < 0 ? 0 : lineStart + 1;
    const int lineEndIndex = markdown.indexOf(QChar('\n'), lineStart);
    const int lineEnd = lineEndIndex < 0 ? markdown.size() : lineEndIndex;
    const QString line = markdown.mid(lineStart, lineEnd - lineStart);
    const QRegularExpression marker(QStringLiteral("^(#{1,6}[ \\t]+)"));
    const QRegularExpressionMatch match = marker.match(line);
    return match.hasMatch() ? match.captured(1) : QString();
}

int contentStartForBlock(const QString& markdown, const RenderSpan& block)
{
    if (block.kind != RenderSpan::Kind::Heading) {
        return block.source.start;
    }
    return block.source.start + static_cast<int>(headingMarkerAt(markdown, block.source.start).size());
}

PatchResult success(QString text, int cursorSourceOffset)
{
    return {true, true, std::move(text), cursorSourceOffset, {}};
}

PatchResult noChange(const QString& text, int cursorSourceOffset = -1)
{
    return {true, false, text, cursorSourceOffset, {}};
}

PatchResult failure(const QString& text, const QString& error)
{
    return {false, false, text, -1, error};
}

QString normalizeReplacement(QString replacement)
{
    replacement.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    replacement.replace(QChar('\r'), QChar('\n'));
    replacement.replace(QChar::ParagraphSeparator, QChar('\n'));
    return replacement;
}

PatchResult applyPlainReplacement(const QString& markdown, const RenderSourceMap& sourceMap,
                                  int renderedStart, int renderedEnd, QString replacement)
{
    replacement = normalizeReplacement(std::move(replacement));

    const std::optional<SourceSpan> sourceSpan = sourceMap.editableSourceSpanForRenderedRange(renderedStart, renderedEnd);
    if (!sourceSpan || !sourceSpan->isValid()) {
        return failure(markdown, QStringLiteral("This rendered text cannot be edited directly yet."));
    }

    if (sourceSpan->start > markdown.size() || sourceSpan->end > markdown.size()) {
        return failure(markdown, QStringLiteral("Rendered/source mapping is out of date."));
    }

    QString patched = markdown;
    patched.replace(sourceSpan->start, sourceSpan->end - sourceSpan->start, replacement);
    return success(patched, sourceSpan->start + static_cast<int>(replacement.size()));
}

PatchResult applyEnter(const QString& markdown, const RenderSourceMap& sourceMap, int renderedStart, int renderedEnd)
{
    const int renderedPosition = qMin(renderedStart, renderedEnd);
    const std::optional<RenderSpan> editableSpan = sourceMap.editableSpanAtRenderedPosition(renderedPosition);
    const std::optional<RenderSpan> block = sourceMap.blockSpanForRenderedPosition(renderedPosition);
    std::optional<SourceSpan> selectionSpan;
    if (renderedStart != renderedEnd) {
        selectionSpan = sourceMap.editableSourceSpanForRenderedRange(renderedStart, renderedEnd);
    }
    const std::optional<int> sourceOffset = selectionSpan
        ? std::optional<int>(selectionSpan->start)
        : sourceMap.editableSourceInsertionPoint(renderedPosition);
    if (!editableSpan || !block || !sourceOffset || !isHeadingOrParagraph(block->kind)) {
        return failure(markdown, QStringLiteral("Enter is not supported in this rendered block yet."));
    }

    if (*sourceOffset > markdown.size() || (selectionSpan && selectionSpan->end > markdown.size())) {
        return failure(markdown, QStringLiteral("Rendered/source mapping is out of date."));
    }

    QString insertion;
    if (block->kind == RenderSpan::Kind::Heading) {
        const QString marker = headingMarkerAt(markdown, block->source.start);
        if (marker.isEmpty()) {
            return failure(markdown, QStringLiteral("Cannot determine heading level for split."));
        }
        insertion = QChar('\n') + marker;
    } else {
        insertion = QStringLiteral("\n\n");
    }

    QString patched = markdown;
    if (selectionSpan) {
        patched.replace(selectionSpan->start, selectionSpan->end - selectionSpan->start, insertion);
    } else {
        patched.insert(*sourceOffset, insertion);
    }
    return success(patched, *sourceOffset + static_cast<int>(insertion.size()));
}

PatchResult mergeNextBlockIntoPrevious(const QString& markdown, const RenderSpan& previous, const RenderSpan& next)
{
    if (!isHeadingOrParagraph(previous.kind)) {
        return failure(markdown, QStringLiteral("This block cannot merge with the next block yet."));
    }

    if (next.kind == RenderSpan::Kind::Table) {
        return noChange(markdown, previous.source.end);
    }

    if (next.kind == RenderSpan::Kind::FormulaBlock) {
        int removeStart = previous.source.end;
        int removeEnd = next.source.end;
        while (removeEnd < markdown.size() && markdown.at(removeEnd) == QChar('\n')) {
            ++removeEnd;
            if (removeEnd < markdown.size() && markdown.at(removeEnd) == QChar('\n')) {
                ++removeEnd;
            }
            break;
        }
        QString patched = markdown;
        patched.remove(removeStart, removeEnd - removeStart);
        return success(patched, removeStart);
    }

    if (!isHeadingOrParagraph(next.kind)) {
        return failure(markdown, QStringLiteral("This next block cannot be merged yet."));
    }

    const int removeStart = previous.source.end;
    const int removeEnd = contentStartForBlock(markdown, next);
    if (removeStart < 0 || removeEnd < removeStart || removeEnd > markdown.size()) {
        return failure(markdown, QStringLiteral("Rendered/source mapping is out of date."));
    }

    QString patched = markdown;
    patched.remove(removeStart, removeEnd - removeStart);
    return success(patched, removeStart);
}

std::optional<RenderSpan> blockBeforeSourceOffset(const RenderSourceMap& sourceMap, int sourceOffset)
{
    std::optional<RenderSpan> best;
    for (const RenderSpan& span : sourceMap.spans()) {
        if (!span.block || !span.source.isValid()) {
            continue;
        }
        if (span.source.end <= sourceOffset && (!best || span.source.end > best->source.end)) {
            best = span;
        }
    }
    return best;
}

std::optional<RenderSpan> blockAfterSourceOffset(const RenderSourceMap& sourceMap, int sourceOffset)
{
    for (const RenderSpan& span : sourceMap.spans()) {
        if (span.block && span.source.isValid() && span.source.start >= sourceOffset) {
            return span;
        }
    }
    return std::nullopt;
}

PatchResult applyDelete(const QString& markdown, const RenderSourceMap& sourceMap, int renderedPosition)
{
    const std::optional<int> sourceOffset = sourceMap.editableSourceInsertionPoint(renderedPosition, RenderSourceMap::Bias::Backward);
    if (!sourceOffset) {
        return failure(markdown, QStringLiteral("Delete is not supported here yet."));
    }

    const std::optional<RenderSpan> previous = blockBeforeSourceOffset(sourceMap, *sourceOffset + 1);
    if (!previous || *sourceOffset < previous->source.end) {
        return applyPlainReplacement(markdown, sourceMap, renderedPosition, renderedPosition + 1, {});
    }

    const std::optional<RenderSpan> next = blockAfterSourceOffset(sourceMap, previous->source.end);
    if (!next) {
        return noChange(markdown, *sourceOffset);
    }
    return mergeNextBlockIntoPrevious(markdown, *previous, *next);
}

PatchResult applyBackspace(const QString& markdown, const RenderSourceMap& sourceMap, int renderedPosition)
{
    const std::optional<int> sourceOffset = sourceMap.editableSourceInsertionPoint(renderedPosition, RenderSourceMap::Bias::Forward);
    if (!sourceOffset) {
        return failure(markdown, QStringLiteral("Backspace is not supported here yet."));
    }

    const std::optional<RenderSpan> current = blockAfterSourceOffset(sourceMap, *sourceOffset - 1);
    if (!current || *sourceOffset > contentStartForBlock(markdown, *current)) {
        return applyPlainReplacement(markdown, sourceMap, renderedPosition - 1, renderedPosition, {});
    }

    const std::optional<RenderSpan> previous = blockBeforeSourceOffset(sourceMap, current->source.start);
    if (!previous) {
        return noChange(markdown, *sourceOffset);
    }
    return mergeNextBlockIntoPrevious(markdown, *previous, *current);
}

} // namespace

PatchResult MarkdownPatch::applyRenderedEdit(const QString& markdown,
                                             const RenderSourceMap& sourceMap,
                                             const RenderedEdit& edit)
{
    int renderedStart = edit.renderedStart;
    int renderedEnd = edit.renderedEnd;
    if (renderedStart > renderedEnd) {
        std::swap(renderedStart, renderedEnd);
    }

    if (renderedStart < 0 || renderedEnd < 0) {
        return failure(markdown, QStringLiteral("Invalid rendered edit range."));
    }

    switch (edit.operation) {
    case RenderedEditOperation::Backspace:
        return applyBackspace(markdown, sourceMap, renderedStart);
    case RenderedEditOperation::Delete:
        return applyDelete(markdown, sourceMap, renderedStart);
    case RenderedEditOperation::Enter:
        return applyEnter(markdown, sourceMap, renderedStart, renderedEnd);
    case RenderedEditOperation::InsertText:
    case RenderedEditOperation::ReplaceSelection:
    case RenderedEditOperation::Paste:
        return applyPlainReplacement(markdown, sourceMap, renderedStart, renderedEnd, edit.replacement);
    }

    return failure(markdown, QStringLiteral("Unsupported rendered edit."));
}

} // namespace Muffin
