#include "MarkerVisibilityController.h"

namespace Muffin {

namespace {

bool sameSourceSpan(SourceSpan lhs, SourceSpan rhs)
{
    return lhs.isValid() && rhs.isValid() && lhs.start == rhs.start && lhs.end == rhs.end;
}

} // namespace

MarkerVisibilityState MarkerVisibilityController::stateForCaret(const SelectionBookmark& caret,
                                                                const SyntaxTokenIndex& syntaxIndex)
{
    MarkerVisibilityState state;
    if (!caret.isValid()) {
        return state;
    }

    state.visiblePairs = syntaxIndex.markerPairsContainingSourceOffset(caret.sourceOffset);
    if (!state.visiblePairs.isEmpty()) {
        state.mode = MarkerVisibilityMode::CaretInsideInline;
    }
    return state;
}

MarkerVisibilityState MarkerVisibilityController::stateForSelection(const SelectionRangeBookmark& selection,
                                                                    const SyntaxTokenIndex& syntaxIndex)
{
    MarkerVisibilityState state;
    if (!selection.isValid()) {
        return state;
    }

    if (selection.isCollapsed()) {
        return stateForCaret(selection.focus, syntaxIndex);
    }

    const int start = qMin(selection.anchor.sourceOffset, selection.focus.sourceOffset);
    const int end = qMax(selection.anchor.sourceOffset, selection.focus.sourceOffset);
    state.visiblePairs = syntaxIndex.markerPairsIntersecting({start, end});
    if (!state.visiblePairs.isEmpty()) {
        state.mode = MarkerVisibilityMode::SelectionIntersectsMarkerPair;
    }
    return state;
}

MarkerVisibilityState MarkerVisibilityController::stateForPinnedMarker(PinnedMarker marker,
                                                                       const SyntaxTokenIndex& syntaxIndex)
{
    MarkerVisibilityState state;
    if (!marker.isValid()) {
        return state;
    }

    for (const SyntaxMarkerPair& pair : syntaxIndex.markerPairs()) {
        if (pair.opening.nodeId != marker.nodeId || pair.opening.kind != marker.kind) {
            continue;
        }
        if (sameSourceSpan(pair.opening.source, marker.source) ||
            sameSourceSpan(pair.closing.source, marker.source)) {
            state.mode = MarkerVisibilityMode::PinnedMarkerPair;
            state.pinnedMarker = marker;
            state.visiblePairs.append(pair);
            return state;
        }
    }
    return state;
}

} // namespace Muffin
