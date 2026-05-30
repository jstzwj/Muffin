#include "MarkdownMathSpanBuilder.h"

#include <algorithm>

namespace Muffin {

namespace {

QString slice(const QString& markdown, SourceSpan span)
{
    if (!span.isValid() || span.start < 0 || span.start > markdown.size()) {
        return {};
    }
    const int end = qMin(span.end, markdown.size());
    if (end < span.start) {
        return {};
    }
    return markdown.mid(span.start, end - span.start);
}

QString texFromSource(const QString& source, bool display)
{
    if (display) {
        QString text = source.trimmed();
        if (text.startsWith(QStringLiteral("$$")) && text.endsWith(QStringLiteral("$$")) && text.size() >= 4) {
            text = text.mid(2, text.size() - 4).trimmed();
        }
        return text;
    }

    if (source.startsWith(QChar('$')) && source.endsWith(QChar('$')) && source.size() >= 2) {
        return source.mid(1, source.size() - 2);
    }
    return source;
}

SourceSpan contentSpanFromSource(SourceSpan source, const QString& sourceText, bool display)
{
    if (!source.isValid()) {
        return {};
    }

    if (!display) {
        return sourceText.startsWith(QChar('$')) && sourceText.endsWith(QChar('$')) && sourceText.size() >= 2
            ? SourceSpan{source.start + 1, source.end - 1}
            : source;
    }

    QString text = sourceText;
    int localStart = 0;
    int localEnd = text.size();
    while (localStart < localEnd && text.at(localStart).isSpace()) {
        ++localStart;
    }
    while (localEnd > localStart && text.at(localEnd - 1).isSpace()) {
        --localEnd;
    }
    if (localEnd - localStart >= 4
        && text.mid(localStart, 2) == QStringLiteral("$$")
        && text.mid(localEnd - 2, 2) == QStringLiteral("$$")) {
        localStart += 2;
        localEnd -= 2;
        while (localStart < localEnd && text.at(localStart).isSpace()) {
            ++localStart;
        }
        while (localEnd > localStart && text.at(localEnd - 1).isSpace()) {
            --localEnd;
        }
    }
    return {source.start + localStart, source.start + localEnd};
}

} // namespace

QVector<MathSpan> MarkdownMathSpanBuilder::build(const MarkdownDocument& document,
                                                 const MarkdownSerializationResult& serialization)
{
    QVector<MathSpan> spans;
    for (const MarkdownNode& node : document.nodes()) {
        if (node.type == MarkdownNodeType::FormulaInline || node.type == MarkdownNodeType::FormulaBlock) {
            spans.append(spanForFormulaNode(node, serialization));
        }
    }
    std::sort(spans.begin(), spans.end(), [](const MathSpan& lhs, const MathSpan& rhs) {
        return lhs.source.start < rhs.source.start;
    });
    return spans;
}

MathSpan MarkdownMathSpanBuilder::spanForFormulaNode(const MarkdownNode& node,
                                                     const MarkdownSerializationResult& serialization)
{
    MathSpan span;
    span.display = node.type == MarkdownNodeType::FormulaBlock;

    const auto it = serialization.nodeSpans.constFind(node.id);
    if (it != serialization.nodeSpans.constEnd()) {
        span.source = it->source;
        span.content = it->content;
    } else {
        span.source = node.source;
        span.content = node.content;
    }

    const QString sourceText = slice(serialization.markdown, span.source);
    span.content = contentSpanFromSource(span.source, sourceText, span.display);
    span.tex = texFromSource(sourceText, span.display);
    if (span.tex.isEmpty()) {
        span.tex = node.literal;
    }
    return span;
}

} // namespace Muffin
