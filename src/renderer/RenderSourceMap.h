#pragma once

#include "parser/AstNode.h"
#include <QString>
#include <QVector>
#include <optional>

namespace Muffin {

struct SourceSpan {
    int start = -1;
    int end = -1;

    bool isValid() const { return start >= 0 && end >= start; }
    bool contains(int offset) const { return offset >= start && offset <= end; }
};

struct RenderSpan {
    enum class Kind {
        Paragraph,
        Heading,
        Text,
        SoftBreak,
        LineBreak,
        Emphasis,
        Strong,
        InlineCode,
        Link,
        Image,
        List,
        CodeBlock,
        Table,
        HtmlBlock,
        ThematicBreak,
        MarkdownSyntax,
        Unsupported
    };

    int renderedStart = -1;
    int renderedEnd = -1;
    SourceSpan source;
    SourceRange sourceRange;
    Kind kind = Kind::Unsupported;
    bool editable = false;

    bool hasRenderedRange() const { return renderedStart >= 0 && renderedEnd >= renderedStart; }
    bool containsRenderedPosition(int position) const { return position >= renderedStart && position <= renderedEnd; }
    bool containsRenderedRange(int start, int end) const { return start >= renderedStart && end <= renderedEnd; }
};

class SourceCoordinateMapper {
public:
    explicit SourceCoordinateMapper(QString source = {});

    int offsetForLineColumn(int line, int column) const;
    SourceSpan spanForRange(SourceRange range) const;
    SourceSpan lineSpan(int line) const;
    SourceSpan headingContentSpan(SourceRange range, int level) const;

private:
    QString m_source;
    QVector<int> m_lineStarts;
};

class RenderSourceMap {
public:
    enum class Bias {
        Backward,
        Forward
    };

    void addSpan(RenderSpan span);
    void clear();

    const QVector<RenderSpan>& spans() const { return m_spans; }

    std::optional<int> sourceOffsetForRenderedPosition(int renderedPos, Bias bias = Bias::Forward) const;
    std::optional<SourceSpan> sourceSpanForRenderedRange(int renderedStart, int renderedEnd) const;
    std::optional<int> renderedPositionForSourceOffset(int sourceOffset, Bias bias = Bias::Forward) const;
    bool isEditableRenderedRange(int renderedStart, int renderedEnd) const;

private:
    const RenderSpan* editableSpanForRenderedPosition(int renderedPos, Bias bias) const;
    const RenderSpan* editableSpanContainingRange(int renderedStart, int renderedEnd) const;

    QVector<RenderSpan> m_spans;
};

} // namespace Muffin
