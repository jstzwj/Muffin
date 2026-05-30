#pragma once

#include "renderer/RenderFragment.h"
#include "renderer/SyntaxTokenIndex.h"

#include <QVector>

namespace Muffin {

class MarkerFragmentIndex {
public:
    explicit MarkerFragmentIndex(QVector<RenderFragment> fragments = {});

    QVector<RenderFragment> markerFragmentsForNode(MarkdownNodeId nodeId) const;
    QVector<RenderFragment> markerFragmentsForSourceSpan(SourceSpan source) const;
    QVector<RenderFragment> markerFragmentsForPair(const SyntaxMarkerPair& pair) const;
    QVector<RenderFragment> markerFragmentsIntersecting(SourceSpan source) const;

private:
    QVector<RenderFragment> m_fragments;
};

} // namespace Muffin
