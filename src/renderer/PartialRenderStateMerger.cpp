#include "PartialRenderStateMerger.h"

#include <algorithm>

namespace Muffin {

namespace {

bool renderedOverlaps(int start, int end, int rangeStart, int rangeEnd)
{
    return start < rangeEnd && end > rangeStart;
}

bool sourceOverlaps(SourceSpan span, SourceSpan range)
{
    return span.isValid() && range.isValid() && span.start < range.end && span.end > range.start;
}

SourceSpan removedSourceSpanForStep(const QVector<MarkdownBlock>& blocks, const PartialDocumentPatchStep& step)
{
    SourceSpan source;
    for (const MarkdownBlock& block : blocks) {
        if (!renderedOverlaps(block.effectiveReplacementRenderedStart(),
                              block.effectiveReplacementRenderedEnd(),
                              step.oldRenderedStart,
                              step.oldRenderedEnd)) {
            continue;
        }
        if (!block.source.isValid()) {
            continue;
        }
        if (!source.isValid()) {
            source = block.source;
        } else {
            source.start = qMin(source.start, block.source.start);
            source.end = qMax(source.end, block.source.end);
        }
    }
    return source;
}

RenderSpan shiftedRenderSpan(RenderSpan span, const PartialDocumentPatchStep& step, SourceSpan removedSource)
{
    const int renderedDelta = (step.newRenderedEnd - step.newRenderedStart) - (step.oldRenderedEnd - step.oldRenderedStart);
    const int sourceDelta = step.source.isValid() && removedSource.isValid()
        ? (step.source.end - step.source.start) - (removedSource.end - removedSource.start)
        : 0;

    span.renderedStart += renderedDelta;
    span.renderedEnd += renderedDelta;
    if (sourceDelta != 0 && removedSource.isValid() && span.source.start >= removedSource.end) {
        span.source.start += sourceDelta;
        span.source.end += sourceDelta;
    }
    if (sourceDelta != 0 && removedSource.isValid() && span.editSource.isValid() && span.editSource.start >= removedSource.end) {
        span.editSource.start += sourceDelta;
        span.editSource.end += sourceDelta;
    }
    return span;
}

RenderSpan insertedRenderSpan(RenderSpan span, const PartialDocumentPatchStep& step)
{
    const int renderedOffset = step.oldRenderedStart - step.newRenderedStart;
    span.renderedStart += renderedOffset;
    span.renderedEnd += renderedOffset;
    return span;
}

MarkdownBlock shiftedBlock(MarkdownBlock block, const PartialDocumentPatchStep& step, SourceSpan removedSource)
{
    const int renderedDelta = (step.newRenderedEnd - step.newRenderedStart) - (step.oldRenderedEnd - step.oldRenderedStart);
    const int sourceDelta = step.source.isValid() && removedSource.isValid()
        ? (step.source.end - step.source.start) - (removedSource.end - removedSource.start)
        : 0;

    block.renderedStart += renderedDelta;
    block.renderedEnd += renderedDelta;
    block.replacementRenderedStart += renderedDelta;
    block.replacementRenderedEnd += renderedDelta;
    if (sourceDelta != 0 && removedSource.isValid() && block.source.start >= removedSource.end) {
        block.source.start += sourceDelta;
        block.source.end += sourceDelta;
    }
    if (sourceDelta != 0 && removedSource.isValid() && block.content.isValid() && block.content.start >= removedSource.end) {
        block.content.start += sourceDelta;
        block.content.end += sourceDelta;
    }
    return block;
}

MarkdownBlock insertedBlock(MarkdownBlock block, const PartialDocumentPatchStep& step)
{
    const int renderedOffset = step.oldRenderedStart - step.newRenderedStart;
    block.renderedStart += renderedOffset;
    block.renderedEnd += renderedOffset;
    block.replacementRenderedStart += renderedOffset;
    block.replacementRenderedEnd += renderedOffset;
    return block;
}

SyntaxTokenSpan shiftedToken(SyntaxTokenSpan token, const PartialDocumentPatchStep& step, SourceSpan removedSource)
{
    const int sourceDelta = step.source.isValid() && removedSource.isValid()
        ? (step.source.end - step.source.start) - (removedSource.end - removedSource.start)
        : 0;
    if (sourceDelta != 0 && removedSource.isValid() && token.source.start >= removedSource.end) {
        token.source.start += sourceDelta;
        token.source.end += sourceDelta;
    }
    return token;
}

RenderFragment shiftedFragment(RenderFragment fragment, const PartialDocumentPatchStep& step, SourceSpan removedSource)
{
    const int sourceDelta = step.source.isValid() && removedSource.isValid()
        ? (step.source.end - step.source.start) - (removedSource.end - removedSource.start)
        : 0;
    if (sourceDelta != 0 && removedSource.isValid() && fragment.source.start >= removedSource.end) {
        fragment.source.start += sourceDelta;
        fragment.source.end += sourceDelta;
    }
    return fragment;
}

} // namespace

PartialRenderStateMergeResult PartialRenderStateMerger::merge(const RenderSourceMap& previousSourceMap,
                                                              const QVector<MarkdownBlock>& previousBlocks,
                                                              const QVector<SyntaxTokenSpan>& previousSyntaxTokens,
                                                              const QVector<RenderFragment>& previousFragments,
                                                              const PartialRenderResult& partial,
                                                              const PartialDocumentPatchPlan& plan)
{
    PartialRenderStateMergeResult result;

    if (!plan.ok) {
        result.errors.append(QStringLiteral("Partial render state merge requires a valid patch plan."));
        result.errors.append(plan.errors);
        return result;
    }
    if (plan.steps.size() != 1) {
        result.errors.append(QStringLiteral("Partial render state merge requires exactly one replacement step."));
        return result;
    }

    const PartialDocumentPatchStep& step = plan.steps.first();
    const SourceSpan removedSource = removedSourceSpanForStep(previousBlocks, step);
    if (!removedSource.isValid()) {
        result.errors.append(QStringLiteral("Partial render state merge could not find the removed source span."));
        return result;
    }

    for (const RenderSpan& span : previousSourceMap.spans()) {
        if (renderedOverlaps(span.renderedStart, span.renderedEnd, step.oldRenderedStart, step.oldRenderedEnd)) {
            continue;
        }
        if (span.renderedStart >= step.oldRenderedEnd) {
            result.sourceMap.addSpan(shiftedRenderSpan(span, step, removedSource));
        } else {
            result.sourceMap.addSpan(span);
        }
    }
    for (const RenderSpan& span : partial.sourceMap.spans()) {
        if (renderedOverlaps(span.renderedStart, span.renderedEnd, step.newRenderedStart, step.newRenderedEnd)) {
            result.sourceMap.addSpan(insertedRenderSpan(span, step));
        }
    }

    for (const MarkdownBlock& block : previousBlocks) {
        if (renderedOverlaps(block.effectiveReplacementRenderedStart(),
                             block.effectiveReplacementRenderedEnd(),
                             step.oldRenderedStart,
                             step.oldRenderedEnd)) {
            continue;
        }
        if (block.effectiveReplacementRenderedStart() >= step.oldRenderedEnd) {
            result.blocks.append(shiftedBlock(block, step, removedSource));
        } else {
            result.blocks.append(block);
        }
    }
    for (const MarkdownBlock& block : partial.blocks) {
        if (renderedOverlaps(block.effectiveReplacementRenderedStart(),
                             block.effectiveReplacementRenderedEnd(),
                             step.newRenderedStart,
                             step.newRenderedEnd)) {
            result.blocks.append(insertedBlock(block, step));
        }
    }
    std::stable_sort(result.blocks.begin(), result.blocks.end(), [](const MarkdownBlock& lhs, const MarkdownBlock& rhs) {
        return lhs.renderedStart < rhs.renderedStart;
    });

    for (const SyntaxTokenSpan& token : previousSyntaxTokens) {
        if (sourceOverlaps(token.source, removedSource)) {
            continue;
        }
        if (token.source.isValid() && token.source.start >= removedSource.end) {
            result.syntaxTokens.append(shiftedToken(token, step, removedSource));
        } else {
            result.syntaxTokens.append(token);
        }
    }
    for (const SyntaxTokenSpan& token : partial.syntaxTokens) {
        if (sourceOverlaps(token.source, step.source)) {
            result.syntaxTokens.append(token);
        }
    }
    std::stable_sort(result.syntaxTokens.begin(), result.syntaxTokens.end(), [](const SyntaxTokenSpan& lhs, const SyntaxTokenSpan& rhs) {
        return lhs.source.start < rhs.source.start;
    });

    for (const RenderFragment& fragment : previousFragments) {
        if (sourceOverlaps(fragment.source, removedSource)) {
            continue;
        }
        if (fragment.source.isValid() && fragment.source.start >= removedSource.end) {
            result.fragments.append(shiftedFragment(fragment, step, removedSource));
        } else {
            result.fragments.append(fragment);
        }
    }
    for (const RenderFragment& fragment : partial.fragments) {
        if (sourceOverlaps(fragment.source, step.source)) {
            result.fragments.append(fragment);
        }
    }
    std::stable_sort(result.fragments.begin(), result.fragments.end(), [](const RenderFragment& lhs, const RenderFragment& rhs) {
        return lhs.source.start < rhs.source.start;
    });

    result.ok = true;
    return result;
}

} // namespace Muffin
