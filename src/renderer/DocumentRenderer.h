#pragma once
#include "parser/AstTree.h"
#include "parser/MathSpan.h"
#include "RenderSourceMap.h"
#include "theme/ThemeStylesheet.h"
#include <QTextDocument>
#include <QTextLength>
#include <QString>
#include <QVector>
#include <memory>

class QTextTable;

namespace Muffin {

struct RenderResult {
    std::unique_ptr<QTextDocument> document;
    RenderSourceMap sourceMap;
};

class DocumentRenderer {
public:
    explicit DocumentRenderer(const ThemeStylesheet& stylesheet);

    RenderResult render(const AstTree& tree, const QString& source, const QVector<MathSpan>& mathSpans = {});

private:
    void renderNode(QTextCursor& cursor, const AstNode& node);
    void renderDocument(QTextCursor& cursor, const AstNode& node);
    void renderHeading(QTextCursor& cursor, const AstNode& node);
    void renderParagraph(QTextCursor& cursor, const AstNode& node);
    void renderFormulaBlock(QTextCursor& cursor, const AstNode& node, const MathSpan& span);
    void renderBlockQuote(QTextCursor& cursor, const AstNode& node);
    void renderCodeBlock(QTextCursor& cursor, const AstNode& node);
    void renderList(QTextCursor& cursor, const AstNode& node);
    void renderItem(QTextCursor& cursor, const AstNode& node, int index);
    void renderThematicBreak(QTextCursor& cursor, const AstNode& node);
    void renderHtmlBlock(QTextCursor& cursor, const AstNode& node);
    void insertBlockForNode(QTextCursor& cursor, const QTextBlockFormat& blockFormat, const QTextCharFormat& charFormat);
    void attachSourceRange(QTextCursor& cursor, const AstNode& node);

    void renderInlineChildren(QTextCursor& cursor, const AstNode& node);
    void renderText(QTextCursor& cursor, const AstNode& node);
    void renderTextChunk(QTextCursor& cursor, const QString& text, SourceSpan source, SourceRange sourceRange);
    void renderInlineFormula(QTextCursor& cursor, const MathSpan& span, SourceRange sourceRange);
    void renderSoftBreak(QTextCursor& cursor);
    void renderLineBreak(QTextCursor& cursor);
    void renderEmph(QTextCursor& cursor, const AstNode& node);
    void renderStrong(QTextCursor& cursor, const AstNode& node);
    void renderInlineCode(QTextCursor& cursor, const AstNode& node);
    void renderLink(QTextCursor& cursor, const AstNode& node);
    void renderImage(QTextCursor& cursor, const AstNode& node);
    void renderStrikethrough(QTextCursor& cursor, const AstNode& node);

    void renderTable(QTextCursor& cursor, const AstNode& node);
    QVector<QTextLength> tableColumnConstraints(const AstNode& node, int cols) const;
    int tableCellTextLength(const AstNode& cellNode) const;
    void renderTableRow(QTextTable* table, int row, const AstNode& rowNode, bool isHeader);
    void renderTableCell(QTextTable* table, int row, int col, const AstNode& cellNode, bool isHeader);

    void beginEditableBlock(const AstNode& node, RenderSpan::Kind kind, SourceSpan sourceSpan);
    void endEditableBlock();
    SourceSpan sourceSpanForNode(const AstNode& node) const;
    const MathSpan* displayMathSpanFor(SourceSpan source) const;
    QVector<MathSpan> inlineMathSpansIn(SourceSpan source) const;
    SourceSpan nextInlineSourceSpan(const QString& literal);
    void recordSpan(int renderedStart, int renderedEnd, SourceSpan source, SourceRange sourceRange,
                    RenderSpan::Kind kind, bool editable, bool block = false,
                    RenderSpan::EditPolicy editPolicy = RenderSpan::EditPolicy::None,
                    SourceSpan editSource = {});

    const ThemeStylesheet& m_ss;
    QString m_source;
    QVector<MathSpan> m_mathSpans;
    SourceCoordinateMapper m_sourceMapper;
    RenderSourceMap m_sourceMap;
    bool m_inEditableBlock = false;
    int m_nonPlainInlineDepth = 0;
    SourceSpan m_currentEditableSourceSpan;
    int m_currentSourceSearchOffset = -1;
    SourceRange m_currentBlockRange;
    RenderSpan::Kind m_currentBlockKind = RenderSpan::Kind::Unsupported;
};

} // namespace Muffin
