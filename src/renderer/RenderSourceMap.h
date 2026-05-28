#pragma once

#include "parser/AstNode.h"
#include "parser/SourceSpan.h"
#include <QString>
#include <QVector>
#include <optional>

namespace Muffin {

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
        FormulaInline,
        FormulaBlock,
        Table,
        HtmlBlock,
        ThematicBreak,
        MarkdownSyntax,
        Unsupported
    };

    enum class EditPolicy {
        None,
        LinearText,
        NonLinearText,
        BlockContent,
        Atomic
    };

    int renderedStart = -1;
    int renderedEnd = -1;
    SourceSpan source;
    SourceRange sourceRange;
    Kind kind = Kind::Unsupported;
    bool editable = false;
    bool block = false;
    SourceSpan editSource;
    EditPolicy editPolicy = EditPolicy::None;

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
    struct LineMap {
        int utf16Start = 0;
        int utf16End = 0;
        QVector<int> byteOffsetToUtf16Offset;
    };

    QString m_source;
    QVector<LineMap> m_lines;
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
    std::optional<SourceSpan> editableSourceSpanForRenderedRange(int renderedStart, int renderedEnd) const;
    std::optional<int> editableSourceInsertionPoint(int renderedPos, Bias bias = Bias::Forward) const;
    std::optional<SourceSpan> editableSourceSpanForRenderedPosition(int renderedPos) const;
    std::optional<RenderSpan> editableSpanAtRenderedPosition(int renderedPos, Bias bias = Bias::Forward) const;
    std::optional<RenderSpan> blockSpanForRenderedPosition(int renderedPos, Bias bias = Bias::Forward) const;
    std::optional<RenderSpan> blockBeforeRenderedPosition(int renderedPos) const;
    std::optional<RenderSpan> blockAfterRenderedPosition(int renderedPos) const;
    std::optional<int> renderedPositionForSourceOffset(int sourceOffset, Bias bias = Bias::Forward) const;
    bool isEditableRenderedRange(int renderedStart, int renderedEnd) const;

private:
    const RenderSpan* editableSpanForRenderedPosition(int renderedPos, Bias bias) const;
    const RenderSpan* editableSpanContainingRange(int renderedStart, int renderedEnd) const;
    const RenderSpan* blockSpanForRenderedPositionPtr(int renderedPos, Bias bias) const;

    QVector<RenderSpan> m_spans;
};

} // namespace Muffin
