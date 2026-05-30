#pragma once
#include "parser/MathSpan.h"
#include "model/MarkdownDocument.h"
#include "renderer/PartialRenderResult.h"
#include "RenderSourceMap.h"
#include "renderer/MarkdownBlock.h"
#include "renderer/RenderFragment.h"
#include "renderer/SyntaxTokenSpan.h"
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
    QVector<MarkdownBlock> blocks;
    QVector<SyntaxTokenSpan> syntaxTokens;
    QVector<RenderFragment> fragments;
};

class DocumentRenderer {
public:
    explicit DocumentRenderer(const ThemeStylesheet& stylesheet);

    RenderResult render(const MarkdownDocument& document, const QVector<MathSpan>& mathSpans = {});
    PartialRenderResult renderPartial(const MarkdownDocument& document,
                                      const QVector<MarkdownNodeId>& nodeIds,
                                      const QVector<MathSpan>& mathSpans = {});

private:
    void resetState(const MarkdownDocument& document, const QVector<MathSpan>& mathSpans);
    std::unique_ptr<QTextDocument> createTextDocument() const;
    MarkdownNodeId renderableNodeIdForPartial(const MarkdownDocument& document, MarkdownNodeId nodeId) const;
    void renderModelNode(QTextCursor& cursor, MarkdownNodeId nodeId);
    void renderModelDocument(QTextCursor& cursor, const MarkdownNode& node);
    void renderModelHeading(QTextCursor& cursor, const MarkdownNode& node);
    void renderModelParagraph(QTextCursor& cursor, const MarkdownNode& node);
    void renderModelFormulaBlock(QTextCursor& cursor, const MarkdownNode& node);
    void renderModelBlockQuote(QTextCursor& cursor, const MarkdownNode& node);
    void renderModelCodeBlock(QTextCursor& cursor, const MarkdownNode& node);
    void renderModelList(QTextCursor& cursor, const MarkdownNode& node);
    void renderModelItem(QTextCursor& cursor, const MarkdownNode& node, int number);
    void renderModelThematicBreak(QTextCursor& cursor, const MarkdownNode& node);
    void renderModelHtmlBlock(QTextCursor& cursor, const MarkdownNode& node);
    void renderModelInlineChildren(QTextCursor& cursor, const MarkdownNode& node);
    void renderModelText(QTextCursor& cursor, const MarkdownNode& node);
    void renderModelTextChunk(QTextCursor& cursor, const QString& text, SourceSpan source, SourceRange sourceRange, MarkdownNodeId nodeId);
    void renderModelEmph(QTextCursor& cursor, const MarkdownNode& node);
    void renderModelStrong(QTextCursor& cursor, const MarkdownNode& node);
    void renderModelInlineCode(QTextCursor& cursor, const MarkdownNode& node);
    void renderModelFormulaInline(QTextCursor& cursor, const MarkdownNode& node);
    void renderModelLink(QTextCursor& cursor, const MarkdownNode& node);
    void renderModelImage(QTextCursor& cursor, const MarkdownNode& node);
    void renderModelStrikethrough(QTextCursor& cursor, const MarkdownNode& node);
    void renderModelTable(QTextCursor& cursor, const MarkdownNode& node);
    void renderModelTableRow(QTextTable* table, int row, const MarkdownNode& rowNode, bool isHeader);
    void renderModelTableCell(QTextTable* table, int row, int col, const MarkdownNode& cellNode, bool isHeader);
    QVector<QTextLength> tableColumnConstraints(const MarkdownNode& node, int cols) const;
    int tableCellTextLength(const MarkdownNode& cellNode) const;

    void insertBlockForNode(QTextCursor& cursor, const QTextBlockFormat& blockFormat, const QTextCharFormat& charFormat);

    void renderInlineFormula(QTextCursor& cursor, const MathSpan& span, SourceRange sourceRange, MarkdownNodeId nodeId);
    void renderSoftBreak(QTextCursor& cursor);
    void renderLineBreak(QTextCursor& cursor);

    void beginEditableBlock(const MarkdownNode& node, RenderSpan::Kind kind, SourceSpan sourceSpan);
    void endEditableBlock();
    SourceSpan nextInlineSourceSpan(const QString& literal);
    void recordSyntaxMarkers(SourceSpan source, int markerLength, SyntaxTokenSpan::Kind kind, MarkdownNodeId nodeId);
    void recordSpan(int renderedStart, int renderedEnd, SourceSpan source, SourceRange sourceRange,
                    RenderSpan::Kind kind, bool editable, bool block = false,
                    RenderSpan::EditPolicy editPolicy = RenderSpan::EditPolicy::None,
                    SourceSpan editSource = {}, MarkdownNodeId nodeId = 0);
    void recordBlock(int renderedStart, int renderedEnd, SourceSpan source, SourceSpan content,
                     SourceRange sourceRange, RenderSpan::Kind kind, bool editable, MarkdownNodeId nodeId = 0,
                     int replacementRenderedStart = -1, int replacementRenderedEnd = -1);

    const ThemeStylesheet& m_ss;
    QString m_source;
    const MarkdownDocument* m_model = nullptr;
    QVector<MathSpan> m_mathSpans;
    SourceCoordinateMapper m_sourceMapper;
    RenderSourceMap m_sourceMap;
    QVector<MarkdownBlock> m_blocks;
    QVector<SyntaxTokenSpan> m_syntaxTokens;
    QVector<RenderFragment> m_fragments;
    bool m_inEditableBlock = false;
    int m_nonPlainInlineDepth = 0;
    SourceSpan m_currentEditableSourceSpan;
    int m_currentSourceSearchOffset = -1;
    SourceRange m_currentBlockRange;
    RenderSpan::Kind m_currentBlockKind = RenderSpan::Kind::Unsupported;
};

} // namespace Muffin
