#pragma once

#include "renderer/SyntaxTokenSpan.h"

#include <QVector>

namespace Muffin {

struct SyntaxMarkerPair {
    SyntaxTokenSpan opening;
    SyntaxTokenSpan closing;

    bool isValid() const { return opening.nodeId != 0 && opening.nodeId == closing.nodeId; }
};

class SyntaxTokenIndex {
public:
    explicit SyntaxTokenIndex(QVector<SyntaxTokenSpan> tokens = {});

    QVector<SyntaxTokenSpan> tokensForNode(MarkdownNodeId nodeId) const;
    QVector<SyntaxTokenSpan> tokensForKind(SyntaxTokenSpan::Kind kind) const;
    QVector<SyntaxTokenSpan> tokensIntersecting(SourceSpan source) const;
    QVector<SyntaxTokenSpan> tokensAdjacentToSourceOffset(int sourceOffset) const;
    QVector<SyntaxMarkerPair> markerPairs() const;
    QVector<SyntaxMarkerPair> markerPairsContainingSourceOffset(int sourceOffset) const;
    QVector<SyntaxMarkerPair> markerPairsIntersecting(SourceSpan source) const;

private:
    QVector<SyntaxTokenSpan> m_tokens;
};

} // namespace Muffin
