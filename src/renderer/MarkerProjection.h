#pragma once

#include "renderer/RenderFragment.h"
#include "renderer/RenderSourceMap.h"

#include <QVector>
#include <optional>

namespace Muffin {

struct MarkerProjectionSpan {
    RenderFragment fragment;
    QString text;
    int baseRenderedAnchor = -1;
    int projectedStart = -1;
    int projectedEnd = -1;
    bool leadingMarker = true;
    SourceSpan nodeSource;

    bool containsProjectedPosition(int position) const
    {
        return position >= projectedStart && position <= projectedEnd;
    }
};

struct ProjectedRenderPosition {
    enum class Kind {
        Content,
        Marker
    };

    Kind kind = Kind::Content;
    int baseRenderedPosition = -1;
    int projectedPosition = -1;
    int offsetInMarker = -1;
    MarkerProjectionSpan marker;
};

class MarkerProjection {
public:
    MarkerProjection() = default;
    MarkerProjection(QVector<RenderFragment> visibleFragments, RenderSourceMap sourceMap);

    const QVector<MarkerProjectionSpan>& markerSpans() const { return m_markerSpans; }

    int projectedLengthForBaseLength(int baseLength) const;
    int projectedPositionForBasePosition(int basePosition) const;
    ProjectedRenderPosition resolveProjectedPosition(int projectedPosition) const;
    std::optional<MarkerProjectionSpan> markerAtBaseAnchor(int baseRenderedPosition, bool leadingMarker) const;
    std::optional<MarkerProjectionSpan> markerAtProjectedPosition(int projectedPosition) const;
    std::optional<int> sourceOffsetForProjectedPosition(int projectedPosition) const;
    std::optional<SourceSpan> sourceSpanForProjectedRange(int projectedStart, int projectedEnd) const;

private:
    static QString markerTextForKind(SyntaxTokenSpan::Kind kind);
    static bool sourceBefore(const RenderFragment& lhs, const RenderFragment& rhs);

    QVector<MarkerProjectionSpan> m_markerSpans;
};

} // namespace Muffin
