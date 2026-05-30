#include "MarkerFragmentIndex.h"

#include <algorithm>

namespace Muffin {

namespace {

bool sameSourceSpan(SourceSpan lhs, SourceSpan rhs)
{
    return lhs.isValid() && rhs.isValid() && lhs.start == rhs.start && lhs.end == rhs.end;
}

bool intersects(SourceSpan lhs, SourceSpan rhs)
{
    return lhs.isValid() && rhs.isValid() && lhs.start < rhs.end && rhs.start < lhs.end;
}

} // namespace

MarkerFragmentIndex::MarkerFragmentIndex(QVector<RenderFragment> fragments)
{
    for (const RenderFragment& fragment : fragments) {
        if (fragment.isMarker()) {
            m_fragments.append(fragment);
        }
    }
    std::stable_sort(m_fragments.begin(), m_fragments.end(), [](const RenderFragment& lhs, const RenderFragment& rhs) {
        if (lhs.source.start == rhs.source.start) {
            return lhs.source.end < rhs.source.end;
        }
        return lhs.source.start < rhs.source.start;
    });
}

QVector<RenderFragment> MarkerFragmentIndex::markerFragmentsForNode(MarkdownNodeId nodeId) const
{
    QVector<RenderFragment> result;
    for (const RenderFragment& fragment : m_fragments) {
        if (fragment.nodeId == nodeId) {
            result.append(fragment);
        }
    }
    return result;
}

QVector<RenderFragment> MarkerFragmentIndex::markerFragmentsForSourceSpan(SourceSpan source) const
{
    QVector<RenderFragment> result;
    for (const RenderFragment& fragment : m_fragments) {
        if (sameSourceSpan(fragment.source, source)) {
            result.append(fragment);
        }
    }
    return result;
}

QVector<RenderFragment> MarkerFragmentIndex::markerFragmentsForPair(const SyntaxMarkerPair& pair) const
{
    QVector<RenderFragment> result;
    for (const RenderFragment& fragment : m_fragments) {
        if (fragment.nodeId != pair.opening.nodeId || fragment.markerKind != pair.opening.kind) {
            continue;
        }
        if (sameSourceSpan(fragment.source, pair.opening.source) || sameSourceSpan(fragment.source, pair.closing.source)) {
            result.append(fragment);
        }
    }
    return result;
}

QVector<RenderFragment> MarkerFragmentIndex::markerFragmentsIntersecting(SourceSpan source) const
{
    QVector<RenderFragment> result;
    for (const RenderFragment& fragment : m_fragments) {
        if (intersects(fragment.source, source)) {
            result.append(fragment);
        }
    }
    return result;
}

} // namespace Muffin
