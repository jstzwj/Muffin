#include "SourceCoordinateMapper.h"

#include <QRegularExpression>

namespace Muffin {

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

int SourceCoordinateMapper::lineForOffset(int offset) const
{
    offset = qBound(0, offset, m_source.size());
    for (int i = 0; i < m_lines.size(); ++i) {
        const LineMap& line = m_lines.at(i);
        if (offset >= line.utf16Start && offset <= line.utf16End) {
            return i + 1;
        }
        if (i + 1 < m_lines.size() && offset < m_lines.at(i + 1).utf16Start) {
            return i + 1;
        }
    }
    return m_lines.isEmpty() ? 0 : m_lines.size();
}

int SourceCoordinateMapper::columnForOffset(int offset) const
{
    const int line = lineForOffset(offset);
    if (line <= 0 || line > m_lines.size()) {
        return 0;
    }

    const LineMap& lineMap = m_lines.at(line - 1);
    const int clampedOffset = qBound(lineMap.utf16Start, offset, lineMap.utf16End);
    int bestColumn = 1;
    for (int byteOffset = 0; byteOffset < lineMap.byteOffsetToUtf16Offset.size(); ++byteOffset) {
        if (lineMap.byteOffsetToUtf16Offset.at(byteOffset) <= clampedOffset) {
            bestColumn = byteOffset + 1;
        } else {
            break;
        }
    }
    return bestColumn;
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

} // namespace Muffin
