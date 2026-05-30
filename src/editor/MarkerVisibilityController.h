#pragma once

#include "editor/EditorSelectionMapper.h"
#include "renderer/RenderFragment.h"
#include "renderer/SyntaxTokenIndex.h"

#include <QVector>

namespace Muffin {

enum class MarkerVisibilityMode {
    Normal,
    CaretInsideInline,
    SelectionIntersectsMarkerPair,
    PinnedMarkerPair
};

struct PinnedMarker {
    SourceSpan source;
    MarkdownNodeId nodeId = 0;
    SyntaxTokenSpan::Kind kind = SyntaxTokenSpan::Kind::EmphasisMarker;

    bool isValid() const { return source.isValid() && nodeId != 0; }
};

struct MarkerVisibilityState {
    MarkerVisibilityMode mode = MarkerVisibilityMode::Normal;
    QVector<SyntaxMarkerPair> visiblePairs;
    QVector<RenderFragment> visibleFragments;
    PinnedMarker pinnedMarker;

    bool hasVisibleMarkers() const { return !visiblePairs.isEmpty() || !visibleFragments.isEmpty(); }
};

class MarkerVisibilityController {
public:
    static MarkerVisibilityState stateForCaret(const SelectionBookmark& caret,
                                               const SyntaxTokenIndex& syntaxIndex);
    static MarkerVisibilityState stateForSelection(const SelectionRangeBookmark& selection,
                                                   const SyntaxTokenIndex& syntaxIndex);
    static MarkerVisibilityState stateForPinnedMarker(PinnedMarker marker,
                                                      const SyntaxTokenIndex& syntaxIndex);
};

} // namespace Muffin
