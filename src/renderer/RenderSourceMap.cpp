#include "RenderSourceMap.h"

#include <QRegularExpression>
#include <algorithm>

namespace Muffin {

namespace {

SourceSpan editableSourceForSpan(const RenderSpan& span)
{
    return span.editSource.isValid() ? span.editSource : span.source;
}

bool canMapInterior(const RenderSpan& span)
{
    return span.editPolicy == RenderSpan::EditPolicy::LinearText
        || span.editPolicy == RenderSpan::EditPolicy::BlockContent;
}

bool canReplaceAcross(const RenderSpan& span)
{
    return span.editable && span.editPolicy == RenderSpan::EditPolicy::LinearText;
}

} // namespace

SourceCoordinateMapper::SourceCoordinateMapper(QString source)
    : m_source(std::move(source))
{
    int lineStart = 0;
    while (lineStart <= m_source.size()) {
        const int newline = m_source.indexOf(QChar('\n'), lineStart);
        const int lineEnd = newline < 0 ? m_source.size() : newline;

        LineMap line;
        line.utf16Start = lineStart;
        line.utf16End = lineEnd;
        line.byteOffsetToUtf16Offset.append(lineStart);

        int utf16Offset = lineStart;
        while (utf16Offset < lineEnd) {
            const int nextUtf16Offset = utf16Offset + (m_source.at(utf16Offset).isHighSurrogate()
                && utf16Offset + 1 < lineEnd
                && m_source.at(utf16Offset + 1).isLowSurrogate() ? 2 : 1);
            const int byteLength = QStringView(m_source).mid(utf16Offset, nextUtf16Offset - utf16Offset).toUtf8().size();
            for (int i = 1; i <= byteLength; ++i) {
                line.byteOffsetToUtf16Offset.append(nextUtf16Offset);
            }
            utf16Offset = nextUtf16Offset;
        }

        m_lines.append(line);
        if (newline < 0) {
            break;
        }
        lineStart = newline + 1;
    }
}

int SourceCoordinateMapper::offsetForLineColumn(int line, int column) const
{
    if (line <= 0 || line > m_lines.size()) {
        return -1;
    }

    const LineMap& lineMap = m_lines.at(line - 1);
    const int byteOffset = qMax(0, column - 1);
    if (byteOffset >= lineMap.byteOffsetToUtf16Offset.size()) {
        return lineMap.utf16End;
    }
    return qBound(lineMap.utf16Start, lineMap.byteOffsetToUtf16Offset.at(byteOffset), lineMap.utf16End);
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
    if (line <= 0 || line > m_lines.size()) {
        return {};
    }

    const LineMap& lineMap = m_lines.at(line - 1);
    return {lineMap.utf16Start, lineMap.utf16End};
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
    if (!span.editSource.isValid()) {
        span.editSource = span.source;
    }
    if (span.editable && span.editPolicy == RenderSpan::EditPolicy::None) {
        span.editPolicy = RenderSpan::EditPolicy::LinearText;
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

    const SourceSpan source = editableSourceForSpan(*span);
    const int renderedLength = span->renderedEnd - span->renderedStart;
    const int sourceLength = source.end - source.start;
    if (renderedLength <= 0 || sourceLength <= 0) {
        return source.start;
    }
    if (!canMapInterior(*span)) {
        if (renderedPos <= span->renderedStart) {
            return source.start;
        }
        if (renderedPos >= span->renderedEnd) {
            return source.end;
        }
        return std::nullopt;
    }

    const int renderedOffset = qBound(0, renderedPos - span->renderedStart, renderedLength);
    return source.start + qMin(renderedOffset, sourceLength);
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

    const SourceSpan source = editableSourceForSpan(*span);
    const int renderedLength = span->renderedEnd - span->renderedStart;
    const int sourceLength = source.end - source.start;
    if (renderedLength <= 0 || sourceLength <= 0) {
        return SourceSpan{source.start, source.start};
    }
    if (!canMapInterior(*span)) {
        if (renderedStart == span->renderedStart && renderedEnd == span->renderedEnd) {
            return source;
        }
        return std::nullopt;
    }

    const int startOffset = qBound(0, renderedStart - span->renderedStart, renderedLength);
    const int endOffset = qBound(0, renderedEnd - span->renderedStart, renderedLength);
    return SourceSpan{
        source.start + qMin(startOffset, sourceLength),
        source.start + qMin(endOffset, sourceLength)
    };
}

std::optional<SourceSpan> RenderSourceMap::editableSourceSpanForRenderedRange(int renderedStart, int renderedEnd) const
{
    if (renderedStart > renderedEnd) {
        std::swap(renderedStart, renderedEnd);
    }

    if (renderedStart == renderedEnd) {
        const std::optional<int> insertion = editableSourceInsertionPoint(renderedStart);
        if (!insertion) {
            return std::nullopt;
        }
        return SourceSpan{*insertion, *insertion};
    }

    std::optional<SourceSpan> combined;
    int coveredUntil = renderedStart;
    for (const RenderSpan& span : m_spans) {
        if (!span.editable || span.renderedEnd <= renderedStart || span.renderedStart >= renderedEnd) {
            continue;
        }
        if (!canReplaceAcross(span)) {
            return std::nullopt;
        }
        const int overlapStart = qMax(renderedStart, span.renderedStart);
        const int overlapEnd = qMin(renderedEnd, span.renderedEnd);
        if (overlapStart > coveredUntil) {
            return std::nullopt;
        }

        const std::optional<SourceSpan> part = sourceSpanForRenderedRange(overlapStart, overlapEnd);
        if (!part || !part->isValid()) {
            return std::nullopt;
        }
        if (!combined) {
            combined = *part;
        } else if (part->start < combined->end) {
            return std::nullopt;
        } else {
            combined->end = part->end;
        }
        coveredUntil = qMax(coveredUntil, overlapEnd);
    }

    if (!combined || coveredUntil < renderedEnd) {
        return std::nullopt;
    }
    return combined;
}

std::optional<int> RenderSourceMap::editableSourceInsertionPoint(int renderedPos, Bias bias) const
{
    return sourceOffsetForRenderedPosition(renderedPos, bias);
}

std::optional<SourceSpan> RenderSourceMap::editableSourceSpanForRenderedPosition(int renderedPos) const
{
    const RenderSpan* span = editableSpanForRenderedPosition(renderedPos, Bias::Forward);
    if (!span || !span->containsRenderedPosition(renderedPos)) {
        return std::nullopt;
    }
    return editableSourceForSpan(*span);
}

std::optional<RenderSpan> RenderSourceMap::editableSpanAtRenderedPosition(int renderedPos, Bias bias) const
{
    const RenderSpan* span = editableSpanForRenderedPosition(renderedPos, bias);
    if (!span) {
        return std::nullopt;
    }
    return *span;
}

std::optional<RenderSpan> RenderSourceMap::blockSpanForRenderedPosition(int renderedPos, Bias bias) const
{
    const RenderSpan* span = blockSpanForRenderedPositionPtr(renderedPos, bias);
    if (!span) {
        return std::nullopt;
    }
    return *span;
}

std::optional<RenderSpan> RenderSourceMap::blockBeforeRenderedPosition(int renderedPos) const
{
    const RenderSpan* best = nullptr;
    for (const RenderSpan& span : m_spans) {
        if (!span.block || !span.hasRenderedRange()) {
            continue;
        }
        if (span.renderedEnd <= renderedPos) {
            best = &span;
        }
    }
    if (!best) {
        return std::nullopt;
    }
    return *best;
}

std::optional<RenderSpan> RenderSourceMap::blockAfterRenderedPosition(int renderedPos) const
{
    for (const RenderSpan& span : m_spans) {
        if (span.block && span.hasRenderedRange() && span.renderedStart >= renderedPos) {
            return span;
        }
    }
    return std::nullopt;
}

std::optional<int> RenderSourceMap::renderedPositionForSourceOffset(int sourceOffset, Bias bias) const
{
    const RenderSpan* best = nullptr;
    for (const RenderSpan& span : m_spans) {
        if (!span.editable || !span.source.isValid()) {
            continue;
        }
        const SourceSpan source = editableSourceForSpan(span);
        if (sourceOffset >= source.start && sourceOffset <= source.end) {
            best = &span;
            break;
        }
        if (bias == Bias::Forward && sourceOffset < source.start) {
            best = &span;
            break;
        }
        if (bias == Bias::Backward && sourceOffset > source.end) {
            best = &span;
        }
    }

    if (!best) {
        return std::nullopt;
    }

    const SourceSpan source = editableSourceForSpan(*best);
    const int sourceLength = source.end - source.start;
    const int renderedLength = best->renderedEnd - best->renderedStart;
    if (sourceLength <= 0 || renderedLength <= 0) {
        return best->renderedStart;
    }
    if (!canMapInterior(*best)) {
        return sourceOffset <= source.start ? best->renderedStart : best->renderedEnd;
    }

    const int sourceDelta = qBound(0, sourceOffset - source.start, sourceLength);
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

const RenderSpan* RenderSourceMap::blockSpanForRenderedPositionPtr(int renderedPos, Bias bias) const
{
    const RenderSpan* fallback = nullptr;
    for (const RenderSpan& span : m_spans) {
        if (!span.block || !span.hasRenderedRange()) {
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

} // namespace Muffin
