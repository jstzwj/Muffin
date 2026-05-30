#include "SyntaxTokenIndex.h"

#include <algorithm>

namespace Muffin {

namespace {

bool intersects(SourceSpan lhs, SourceSpan rhs)
{
    return lhs.isValid() && rhs.isValid() && lhs.start < rhs.end && rhs.start < lhs.end;
}

} // namespace

SyntaxTokenIndex::SyntaxTokenIndex(QVector<SyntaxTokenSpan> tokens)
    : m_tokens(std::move(tokens))
{
    std::stable_sort(m_tokens.begin(), m_tokens.end(), [](const SyntaxTokenSpan& lhs, const SyntaxTokenSpan& rhs) {
        if (lhs.source.start == rhs.source.start) {
            return lhs.source.end < rhs.source.end;
        }
        return lhs.source.start < rhs.source.start;
    });
}

QVector<SyntaxTokenSpan> SyntaxTokenIndex::tokensForNode(MarkdownNodeId nodeId) const
{
    QVector<SyntaxTokenSpan> result;
    for (const SyntaxTokenSpan& token : m_tokens) {
        if (token.nodeId == nodeId) {
            result.append(token);
        }
    }
    return result;
}

QVector<SyntaxTokenSpan> SyntaxTokenIndex::tokensForKind(SyntaxTokenSpan::Kind kind) const
{
    QVector<SyntaxTokenSpan> result;
    for (const SyntaxTokenSpan& token : m_tokens) {
        if (token.kind == kind) {
            result.append(token);
        }
    }
    return result;
}

QVector<SyntaxTokenSpan> SyntaxTokenIndex::tokensIntersecting(SourceSpan source) const
{
    QVector<SyntaxTokenSpan> result;
    for (const SyntaxTokenSpan& token : m_tokens) {
        if (intersects(token.source, source)) {
            result.append(token);
        }
    }
    return result;
}

QVector<SyntaxTokenSpan> SyntaxTokenIndex::tokensAdjacentToSourceOffset(int sourceOffset) const
{
    QVector<SyntaxTokenSpan> result;
    for (const SyntaxTokenSpan& token : m_tokens) {
        if (token.source.end == sourceOffset || token.source.start == sourceOffset) {
            result.append(token);
        }
    }
    return result;
}

QVector<SyntaxMarkerPair> SyntaxTokenIndex::markerPairs() const
{
    QVector<SyntaxMarkerPair> result;
    for (int i = 0; i < m_tokens.size(); ++i) {
        const SyntaxTokenSpan& opening = m_tokens.at(i);
        if (!opening.source.isValid() || opening.nodeId == 0) {
            continue;
        }
        for (int j = i + 1; j < m_tokens.size(); ++j) {
            const SyntaxTokenSpan& closing = m_tokens.at(j);
            if (closing.nodeId != opening.nodeId || closing.kind != opening.kind) {
                continue;
            }
            result.append({opening, closing});
            break;
        }
    }
    return result;
}

QVector<SyntaxMarkerPair> SyntaxTokenIndex::markerPairsContainingSourceOffset(int sourceOffset) const
{
    QVector<SyntaxMarkerPair> result;
    for (const SyntaxMarkerPair& pair : markerPairs()) {
        if (sourceOffset >= pair.opening.source.end && sourceOffset <= pair.closing.source.start) {
            result.append(pair);
        }
    }
    return result;
}

QVector<SyntaxMarkerPair> SyntaxTokenIndex::markerPairsIntersecting(SourceSpan source) const
{
    QVector<SyntaxMarkerPair> result;
    if (!source.isValid()) {
        return result;
    }

    for (const SyntaxMarkerPair& pair : markerPairs()) {
        const SourceSpan pairSource{pair.opening.source.start, pair.closing.source.end};
        if (intersects(pairSource, source)) {
            result.append(pair);
        }
    }
    return result;
}

} // namespace Muffin
