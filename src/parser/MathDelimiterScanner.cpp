#include "MathDelimiterScanner.h"

#include <algorithm>

namespace Muffin {

namespace {

bool isEscaped(const QString& text, int index)
{
    int backslashes = 0;
    for (int i = index - 1; i >= 0 && text.at(i) == QChar('\\'); --i) {
        ++backslashes;
    }
    return backslashes % 2 == 1;
}

bool isWhitespaceAt(const QString& text, int index)
{
    return index >= 0 && index < text.size() && text.at(index).isSpace();
}

bool overlapsAny(const QVector<SourceSpan>& spans, SourceSpan candidate)
{
    for (const SourceSpan& span : spans) {
        if (candidate.start < span.end && candidate.end > span.start) {
            return true;
        }
    }
    return false;
}

struct LineMap {
    int start = 0;
    int end = 0;
    QVector<int> byteOffsetToUtf16Offset;
};

QVector<LineMap> buildLineMap(const QString& markdown)
{
    QVector<LineMap> lines;
    int lineStart = 0;
    while (lineStart <= markdown.size()) {
        const int newline = markdown.indexOf(QChar('\n'), lineStart);
        const int lineEnd = newline < 0 ? markdown.size() : newline;

        LineMap line;
        line.start = lineStart;
        line.end = lineEnd;
        line.byteOffsetToUtf16Offset.append(lineStart);

        int utf16Offset = lineStart;
        while (utf16Offset < lineEnd) {
            const int nextUtf16Offset = utf16Offset + (markdown.at(utf16Offset).isHighSurrogate()
                && utf16Offset + 1 < lineEnd
                && markdown.at(utf16Offset + 1).isLowSurrogate() ? 2 : 1);
            const int byteLength = QStringView(markdown).mid(utf16Offset, nextUtf16Offset - utf16Offset).toUtf8().size();
            for (int i = 1; i <= byteLength; ++i) {
                line.byteOffsetToUtf16Offset.append(nextUtf16Offset);
            }
            utf16Offset = nextUtf16Offset;
        }

        lines.append(line);
        if (newline < 0) {
            break;
        }
        lineStart = newline + 1;
    }
    return lines;
}

int offsetForLineColumn(const QVector<LineMap>& lines, int line, int column)
{
    if (line <= 0 || line > lines.size()) {
        return -1;
    }

    const LineMap& lineMap = lines.at(line - 1);
    const int byteOffset = qMax(0, column - 1);
    if (byteOffset >= lineMap.byteOffsetToUtf16Offset.size()) {
        return lineMap.end;
    }
    return qBound(lineMap.start, lineMap.byteOffsetToUtf16Offset.at(byteOffset), lineMap.end);
}

SourceSpan spanForRange(const QVector<LineMap>& lines, SourceRange range)
{
    const int start = offsetForLineColumn(lines, range.startLine, range.startColumn);
    const int end = offsetForLineColumn(lines, range.endLine, range.endColumn > 0 ? range.endColumn + 1 : range.endColumn);
    if (start < 0 || end < start) {
        return {};
    }
    return {start, end};
}

void collectProtectedSpans(const AstNode& node, const QVector<LineMap>& lines, QVector<SourceSpan>& spans)
{
    if (node.isNull()) {
        return;
    }

    const cmark_node_type type = node.type();
    if (type == CMARK_NODE_CODE_BLOCK || type == CMARK_NODE_CODE) {
        const SourceSpan span = spanForRange(lines, node.sourceRange());
        if (span.isValid()) {
            spans.append(span);
        }
    }

    AstNode child = node.firstChild();
    while (!child.isNull()) {
        collectProtectedSpans(child, lines, spans);
        child = child.next();
    }
}

QVector<SourceSpan> protectedSpansForTree(const AstTree& tree, const QString& markdown)
{
    QVector<SourceSpan> spans;
    if (!tree.isNull()) {
        collectProtectedSpans(tree.root(), buildLineMap(markdown), spans);
    }
    std::sort(spans.begin(), spans.end(), [](SourceSpan lhs, SourceSpan rhs) {
        return lhs.start < rhs.start;
    });
    return spans;
}

struct LineInfo {
    int start = 0;
    int end = 0;
    int next = 0;
};

LineInfo lineAt(const QString& markdown, int offset)
{
    const int newline = markdown.indexOf(QChar('\n'), offset);
    const int end = newline < 0 ? markdown.size() : newline;
    return {offset, end, newline < 0 ? static_cast<int>(markdown.size()) : newline + 1};
}

void scanDisplayMath(const QString& markdown, const QVector<SourceSpan>& protectedSpans,
                     QVector<MathSpan>& spans, QVector<SourceSpan>& displaySources)
{
    int offset = 0;
    while (offset < markdown.size()) {
        const LineInfo line = lineAt(markdown, offset);
        if (overlapsAny(protectedSpans, {line.start, line.end})) {
            offset = line.next;
            continue;
        }

        const QString text = markdown.mid(line.start, line.end - line.start);
        const QString trimmed = text.trimmed();
        int leading = 0;
        while (leading < text.size() && text.at(leading).isSpace()) {
            ++leading;
        }
        int trailing = 0;
        while (trailing < text.size() - leading && text.at(text.size() - trailing - 1).isSpace()) {
            ++trailing;
        }
        const int trimmedStart = line.start + leading;
        const int trimmedEnd = line.end - trailing;

        if (trimmed.startsWith(QStringLiteral("$$")) && trimmed.endsWith(QStringLiteral("$$")) && trimmed.size() > 4) {
            const int contentStart = trimmedStart + 2;
            const int contentEnd = trimmedEnd - 2;
            MathSpan span{{trimmedStart, trimmedEnd}, {contentStart, contentEnd}, markdown.mid(contentStart, contentEnd - contentStart), true};
            spans.append(span);
            displaySources.append(span.source);
            offset = line.next;
            continue;
        }

        if (trimmed == QStringLiteral("$$")) {
            const int formulaStart = trimmedStart;
            int scan = line.next;
            while (scan < markdown.size()) {
                const LineInfo closeLine = lineAt(markdown, scan);
                if (overlapsAny(protectedSpans, {closeLine.start, closeLine.end})) {
                    scan = closeLine.next;
                    continue;
                }

                const QString closeText = markdown.mid(closeLine.start, closeLine.end - closeLine.start);
                const QString closeTrimmed = closeText.trimmed();
                if (closeTrimmed == QStringLiteral("$$")) {
                    int closeLeading = 0;
                    while (closeLeading < closeText.size() && closeText.at(closeLeading).isSpace()) {
                        ++closeLeading;
                    }
                    const int closeStart = closeLine.start + closeLeading;
                    const int contentStart = line.next;
                    const int contentEnd = closeLine.start > contentStart && markdown.at(closeLine.start - 1) == QChar('\n')
                        ? closeLine.start - 1
                        : closeLine.start;
                    MathSpan span{{formulaStart, closeStart + 2}, {contentStart, contentEnd}, markdown.mid(contentStart, contentEnd - contentStart), true};
                    spans.append(span);
                    displaySources.append(span.source);
                    offset = closeLine.next;
                    break;
                }
                scan = closeLine.next;
            }
            if (scan >= markdown.size()) {
                offset = markdown.size();
            }
            continue;
        }

        offset = line.next;
    }
}

void scanInlineMath(const QString& markdown, const QVector<SourceSpan>& protectedSpans,
                    const QVector<SourceSpan>& displaySources, QVector<MathSpan>& spans)
{
    int i = 0;
    while (i < markdown.size()) {
        if (markdown.at(i) != QChar('$') || isEscaped(markdown, i)) {
            ++i;
            continue;
        }
        if (i + 1 < markdown.size() && markdown.at(i + 1) == QChar('$')) {
            i += 2;
            continue;
        }
        if (overlapsAny(protectedSpans, {i, i + 1}) || overlapsAny(displaySources, {i, i + 1})) {
            ++i;
            continue;
        }
        if (i + 1 >= markdown.size() || isWhitespaceAt(markdown, i + 1) || markdown.at(i + 1).isDigit()) {
            ++i;
            continue;
        }

        int close = i + 1;
        while (close < markdown.size()) {
            if (markdown.at(close) == QChar('\n')) {
                close = -1;
                break;
            }
            if (markdown.at(close) == QChar('$') && !isEscaped(markdown, close)) {
                if (close + 1 < markdown.size() && markdown.at(close + 1) == QChar('$')) {
                    close += 2;
                    continue;
                }
                if (isWhitespaceAt(markdown, close - 1)) {
                    ++close;
                    continue;
                }
                if (close + 1 < markdown.size() && markdown.at(close + 1).isDigit()) {
                    ++close;
                    continue;
                }
                break;
            }
            ++close;
        }

        if (close <= i + 1 || close >= markdown.size()) {
            ++i;
            continue;
        }

        const SourceSpan candidate{i, close + 1};
        if (overlapsAny(protectedSpans, candidate) || overlapsAny(displaySources, candidate)) {
            i = close + 1;
            continue;
        }

        spans.append({candidate, {i + 1, close}, markdown.mid(i + 1, close - i - 1), false});
        i = close + 1;
    }
}

} // namespace

QVector<MathSpan> MathDelimiterScanner::scan(const QString& markdown, const AstTree& tree)
{
    QVector<MathSpan> spans;
    QVector<SourceSpan> displaySources;
    const QVector<SourceSpan> protectedSpans = protectedSpansForTree(tree, markdown);
    scanDisplayMath(markdown, protectedSpans, spans, displaySources);
    scanInlineMath(markdown, protectedSpans, displaySources, spans);
    std::sort(spans.begin(), spans.end(), [](const MathSpan& lhs, const MathSpan& rhs) {
        return lhs.source.start < rhs.source.start;
    });
    return spans;
}

} // namespace Muffin
