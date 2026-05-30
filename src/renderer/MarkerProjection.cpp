#include "MarkerProjection.h"

#include <algorithm>

namespace Muffin {

namespace {

bool isClosingMarkerForNodeSpan(const RenderFragment& fragment, const RenderSpan& nodeSpan)
{
    const int markerLength = fragment.source.end - fragment.source.start;
    return fragment.source.start >= nodeSpan.source.end - markerLength;
}

} // namespace

MarkerProjection::MarkerProjection(QVector<RenderFragment> visibleFragments, RenderSourceMap sourceMap)
{
    std::stable_sort(visibleFragments.begin(), visibleFragments.end(), sourceBefore);

    int insertedLength = 0;
    for (const RenderFragment& fragment : visibleFragments) {
        if (!fragment.isMarker() || !fragment.source.isValid()) {
            continue;
        }

        const QString markerText = markerTextForKind(fragment.markerKind);
        if (markerText.isEmpty()) {
            continue;
        }

        std::optional<RenderSpan> nodeSpan = sourceMap.spanForNode(fragment.nodeId);
        if (!nodeSpan || !nodeSpan->hasRenderedRange()) {
            continue;
        }

        const bool closingMarker = isClosingMarkerForNodeSpan(fragment, *nodeSpan);
        const int baseAnchor = closingMarker ? nodeSpan->renderedEnd : nodeSpan->renderedStart;
        const int projectedStart = baseAnchor + insertedLength;
        const int projectedEnd = projectedStart + markerText.size();
        m_markerSpans.append({fragment, markerText, baseAnchor, projectedStart, projectedEnd, !closingMarker, nodeSpan->source});
        insertedLength += markerText.size();
    }
}

int MarkerProjection::projectedLengthForBaseLength(int baseLength) const
{
    int length = qMax(0, baseLength);
    for (const MarkerProjectionSpan& marker : m_markerSpans) {
        length += marker.text.size();
    }
    return length;
}

int MarkerProjection::projectedPositionForBasePosition(int basePosition) const
{
    int projectedPosition = qMax(0, basePosition);
    for (const MarkerProjectionSpan& marker : m_markerSpans) {
        if (marker.baseRenderedAnchor <= basePosition) {
            projectedPosition += marker.text.size();
        }
    }
    return projectedPosition;
}

ProjectedRenderPosition MarkerProjection::resolveProjectedPosition(int projectedPosition) const
{
    ProjectedRenderPosition resolved;
    resolved.projectedPosition = qMax(0, projectedPosition);

    int insertedBefore = 0;
    for (const MarkerProjectionSpan& marker : m_markerSpans) {
        if (resolved.projectedPosition >= marker.projectedStart && resolved.projectedPosition <= marker.projectedEnd) {
            resolved.kind = ProjectedRenderPosition::Kind::Marker;
            resolved.baseRenderedPosition = marker.baseRenderedAnchor;
            resolved.offsetInMarker = qBound(0, resolved.projectedPosition - marker.projectedStart, marker.text.size());
            resolved.marker = marker;
            return resolved;
        }

        if (marker.projectedEnd <= resolved.projectedPosition) {
            insertedBefore += marker.text.size();
        }
    }

    resolved.kind = ProjectedRenderPosition::Kind::Content;
    resolved.baseRenderedPosition = qMax(0, resolved.projectedPosition - insertedBefore);
    return resolved;
}

std::optional<MarkerProjectionSpan> MarkerProjection::markerAtProjectedPosition(int projectedPosition) const
{
    for (const MarkerProjectionSpan& marker : m_markerSpans) {
        if (marker.containsProjectedPosition(projectedPosition)) {
            return marker;
        }
    }
    return std::nullopt;
}

std::optional<MarkerProjectionSpan> MarkerProjection::markerAtBaseAnchor(int baseRenderedPosition, bool leadingMarker) const
{
    for (const MarkerProjectionSpan& marker : m_markerSpans) {
        if (marker.baseRenderedAnchor == baseRenderedPosition && marker.leadingMarker == leadingMarker) {
            return marker;
        }
    }
    return std::nullopt;
}

std::optional<int> MarkerProjection::sourceOffsetForProjectedPosition(int projectedPosition) const
{
    const ProjectedRenderPosition resolved = resolveProjectedPosition(projectedPosition);
    if (resolved.kind == ProjectedRenderPosition::Kind::Marker) {
        if (!resolved.marker.fragment.source.isValid()) {
            return std::nullopt;
        }
        return qBound(resolved.marker.fragment.source.start,
                      resolved.marker.fragment.source.start + resolved.offsetInMarker,
                      resolved.marker.fragment.source.end);
    }

    return std::nullopt;
}

std::optional<SourceSpan> MarkerProjection::sourceSpanForProjectedRange(int projectedStart, int projectedEnd) const
{
    if (projectedStart > projectedEnd) {
        std::swap(projectedStart, projectedEnd);
    }

    const std::optional<int> sourceStart = sourceOffsetForProjectedPosition(projectedStart);
    const std::optional<int> sourceEnd = sourceOffsetForProjectedPosition(projectedEnd);
    if (!sourceStart || !sourceEnd) {
        return std::nullopt;
    }

    return SourceSpan{qMin(*sourceStart, *sourceEnd), qMax(*sourceStart, *sourceEnd)};
}

QString MarkerProjection::markerTextForKind(SyntaxTokenSpan::Kind kind)
{
    switch (kind) {
    case SyntaxTokenSpan::Kind::StrongMarker: return QStringLiteral("**");
    case SyntaxTokenSpan::Kind::InlineCodeMarker: return QStringLiteral("`");
    case SyntaxTokenSpan::Kind::EmphasisMarker: return QStringLiteral("*");
    }
    return {};
}

bool MarkerProjection::sourceBefore(const RenderFragment& lhs, const RenderFragment& rhs)
{
    if (lhs.source.start == rhs.source.start) {
        return lhs.source.end < rhs.source.end;
    }
    return lhs.source.start < rhs.source.start;
}

} // namespace Muffin
