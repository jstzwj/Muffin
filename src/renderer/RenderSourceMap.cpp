#include "RenderSourceMap.h"

#include <QRegularExpression>
#include <algorithm>

namespace Muffin {

SourceCoordinateMapper::SourceCoordinateMapper(QString source)
    : m_source(std::move(source))
{
    m_lineStarts.append(0);
    for (int i = 0; i < m_source.size(); ++i) {
        if (m_source.at(i) == QChar('\n')) {
            m_lineStarts.append(i + 1);
        }
    }
}

int SourceCoordinateMapper::offsetForLineColumn(int line, int column) const
{
    if (line <= 0 || line > m_lineStarts.size()) {
        return -1;
    }

    const int lineStart = m_lineStarts.at(line - 1);
    const int nextLineStart = line < m_lineStarts.size() ? m_lineStarts.at(line) : m_source.size();
    const int lineEnd = nextLineStart > lineStart && m_source.at(nextLineStart - 1) == QChar('\n')
        ? nextLineStart - 1
        : nextLineStart;
    const int offset = lineStart + qMax(0, column - 1);
    return qBound(lineStart, offset, lineEnd);
}

SourceSpan SourceCoordinateMapper::spanForRange(SourceRange range) const
{
    const int start = offsetForLineColumn(range.startLine, range.startColumn);
    const int end = offsetForLineColumn(range.endLine, range.endColumn > 0 ? range.endColumn + 1 : range.endColumn);
    if (start < 0 || end < start) {
        return {};
    }
    return {start, end};
}

SourceSpan SourceCoordinateMapper::lineSpan(int line) const
{
    if (line <= 0 || line > m_lineStarts.size()) {
        return {};
    }

    const int start = m_lineStarts.at(line - 1);
    const int nextLineStart = line < m_lineStarts.size() ? m_lineStarts.at(line) : m_source.size();
    const int end = nextLineStart > start && m_source.at(nextLineStart - 1) == QChar('\n')
        ? nextLineStart - 1
        : nextLineStart;
    return {start, end};
}

SourceSpan SourceCoordinateMapper::headingContentSpan(SourceRange range, int level) const
{
    SourceSpan line = lineSpan(range.startLine);
    if (!line.isValid()) {
        return {};
    }

    const QString text = m_source.mid(line.start, line.end - line.start);
    const QRegularExpression marker(QStringLiteral("^#{1,%1}[ \\t]+").arg(qBound(1, level, 6)));
    const QRegularExpressionMatch match = marker.match(text);
    const int markerLength = match.hasMatch() ? match.capturedLength() : 0;
    SourceSpan block = spanForRange(range);
    if (!block.isValid()) {
        return {};
    }

    const int start = qMin(line.start + markerLength, block.end);
    return {start, block.end};
}

void RenderSourceMap::addSpan(RenderSpan span)
{
    if (!span.hasRenderedRange() || !span.source.isValid()) {
        return;
    }

    m_spans.append(span);
    std::stable_sort(m_spans.begin(), m_spans.end(), [](const RenderSpan& lhs, const RenderSpan& rhs) {
        if (lhs.renderedStart == rhs.renderedStart) {
            return lhs.renderedEnd < rhs.renderedEnd;
        }
        return lhs.renderedStart < rhs.renderedStart;
    });
}

void RenderSourceMap::clear()
{
    m_spans.clear();
}

std::optional<int> RenderSourceMap::sourceOffsetForRenderedPosition(int renderedPos, Bias bias) const
{
    const RenderSpan* span = editableSpanForRenderedPosition(renderedPos, bias);
    if (!span) {
        return std::nullopt;
    }

    const int renderedLength = span->renderedEnd - span->renderedStart;
    const int sourceLength = span->source.end - span->source.start;
    if (renderedLength <= 0 || sourceLength <= 0) {
        return span->source.start;
    }

    const int renderedOffset = qBound(0, renderedPos - span->renderedStart, renderedLength);
    return span->source.start + qMin(renderedOffset, sourceLength);
}

std::optional<SourceSpan> RenderSourceMap::sourceSpanForRenderedRange(int renderedStart, int renderedEnd) const
{
    if (renderedStart > renderedEnd) {
        std::swap(renderedStart, renderedEnd);
    }

    const RenderSpan* span = editableSpanContainingRange(renderedStart, renderedEnd);
    if (!span) {
        return std::nullopt;
    }

    const int renderedLength = span->renderedEnd - span->renderedStart;
    const int sourceLength = span->source.end - span->source.start;
    if (renderedLength <= 0 || sourceLength <= 0) {
        return SourceSpan{span->source.start, span->source.start};
    }

    const int startOffset = qBound(0, renderedStart - span->renderedStart, renderedLength);
    const int endOffset = qBound(0, renderedEnd - span->renderedStart, renderedLength);
    return SourceSpan{
        span->source.start + qMin(startOffset, sourceLength),
        span->source.start + qMin(endOffset, sourceLength)
    };
}

std::optional<int> RenderSourceMap::renderedPositionForSourceOffset(int sourceOffset, Bias bias) const
{
    const RenderSpan* best = nullptr;
    for (const RenderSpan& span : m_spans) {
        if (!span.editable || !span.source.isValid()) {
            continue;
        }
        if (sourceOffset >= span.source.start && sourceOffset <= span.source.end) {
            best = &span;
            break;
        }
        if (bias == Bias::Forward && sourceOffset < span.source.start) {
            best = &span;
            break;
        }
        if (bias == Bias::Backward && sourceOffset > span.source.end) {
            best = &span;
        }
    }

    if (!best) {
        return std::nullopt;
    }

    const int sourceLength = best->source.end - best->source.start;
    const int renderedLength = best->renderedEnd - best->renderedStart;
    if (sourceLength <= 0 || renderedLength <= 0) {
        return best->renderedStart;
    }

    const int sourceDelta = qBound(0, sourceOffset - best->source.start, sourceLength);
    return best->renderedStart + qMin(sourceDelta, renderedLength);
}

bool RenderSourceMap::isEditableRenderedRange(int renderedStart, int renderedEnd) const
{
    return editableSpanContainingRange(qMin(renderedStart, renderedEnd), qMax(renderedStart, renderedEnd)) != nullptr;
}

const RenderSpan* RenderSourceMap::editableSpanForRenderedPosition(int renderedPos, Bias bias) const
{
    const RenderSpan* fallback = nullptr;
    for (const RenderSpan& span : m_spans) {
        if (!span.editable || !span.hasRenderedRange()) {
            continue;
        }
        if (span.containsRenderedPosition(renderedPos)) {
            return &span;
        }
        if (bias == Bias::Forward && renderedPos < span.renderedStart) {
            return &span;
        }
        if (bias == Bias::Backward && renderedPos > span.renderedEnd) {
            fallback = &span;
        }
    }
    return fallback;
}

const RenderSpan* RenderSourceMap::editableSpanContainingRange(int renderedStart, int renderedEnd) const
{
    for (const RenderSpan& span : m_spans) {
        if (span.editable && span.containsRenderedRange(renderedStart, renderedEnd)) {
            return &span;
        }
    }
    return nullptr;
}

} // namespace Muffin
