#include "MarkerSelectionMapper.h"

#include <QtGlobal>

namespace Muffin {

namespace {

bool sameMarkerSource(const MarkerProjectionSpan& lhs, const MarkerProjectionSpan& rhs)
{
    return lhs.fragment.source.start == rhs.fragment.source.start
        && lhs.fragment.source.end == rhs.fragment.source.end;
}

bool sameMarkerNode(const MarkerProjectionSpan& lhs, const MarkerProjectionSpan& rhs)
{
    return lhs.fragment.nodeId != 0
        && lhs.fragment.nodeId == rhs.fragment.nodeId
        && lhs.fragment.markerKind == rhs.fragment.markerKind;
}

bool selectsWholeMarkerPair(const MarkerProjectionSpan& startMarker,
                            const MarkerProjectionSpan& endMarker,
                            int projectedStart,
                            int projectedEnd)
{
    return sameMarkerNode(startMarker, endMarker)
        && startMarker.leadingMarker
        && !endMarker.leadingMarker
        && startMarker.nodeSource.isValid()
        && projectedStart == startMarker.projectedStart
        && projectedEnd == endMarker.projectedEnd;
}

} // namespace

std::optional<MarkerSelection> MarkerSelectionMapper::selectionForProjectedRange(const MarkerProjection& projection,
                                                                                 int projectedStart,
                                                                                 int projectedEnd)
{
    if (projectedStart > projectedEnd) {
        std::swap(projectedStart, projectedEnd);
    }
    if (projectedStart == projectedEnd) {
        return std::nullopt;
    }

    const std::optional<MarkerProjectionSpan> startMarker = projection.markerAtProjectedPosition(projectedStart);
    const std::optional<MarkerProjectionSpan> endMarker = projection.markerAtProjectedPosition(projectedEnd);
    if (!startMarker || !endMarker) {
        return std::nullopt;
    }

    if (selectsWholeMarkerPair(*startMarker, *endMarker, projectedStart, projectedEnd)) {
        return MarkerSelection{MarkerSelection::Kind::MarkerPairWithContent,
                               startMarker->nodeSource,
                               projectedStart,
                               projectedEnd,
                               *startMarker};
    }

    if (!sameMarkerSource(*startMarker, *endMarker)) {
        return std::nullopt;
    }

    const std::optional<SourceSpan> source = projection.sourceSpanForProjectedRange(projectedStart, projectedEnd);
    if (!source || !source->isValid() || source->start == source->end) {
        return std::nullopt;
    }

    return MarkerSelection{MarkerSelection::Kind::SingleMarker,
                           *source,
                           projectedStart,
                           projectedEnd,
                           *startMarker};
}

} // namespace Muffin
