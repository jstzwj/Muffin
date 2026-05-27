#include "MarkdownPatch.h"

namespace Muffin {

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
        return {false, markdown, -1, QStringLiteral("Invalid rendered edit range.")};
    }

    if (edit.replacement.contains(QChar('\n')) || edit.replacement.contains(QChar::ParagraphSeparator)) {
        return {false, markdown, -1, QStringLiteral("Multiline edits are not supported in rendered mode yet.")};
    }

    const std::optional<SourceSpan> sourceSpan = sourceMap.sourceSpanForRenderedRange(renderedStart, renderedEnd);
    if (!sourceSpan || !sourceSpan->isValid()) {
        return {false, markdown, -1, QStringLiteral("This rendered text cannot be edited directly yet.")};
    }

    if (sourceSpan->start > markdown.size() || sourceSpan->end > markdown.size()) {
        return {false, markdown, -1, QStringLiteral("Rendered/source mapping is out of date.")};
    }

    QString patched = markdown;
    patched.replace(sourceSpan->start, sourceSpan->end - sourceSpan->start, edit.replacement);
    return {true, patched, sourceSpan->start + static_cast<int>(edit.replacement.size()), {}};
}

} // namespace Muffin
