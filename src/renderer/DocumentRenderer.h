#pragma once
#include "parser/AstTree.h"
#include "theme/ThemeStylesheet.h"
#include <QTextDocument>
#include <QTextLength>
#include <QVector>
#include <memory>

class QTextTable;

namespace Muffin {

class DocumentRenderer {
public:
    explicit DocumentRenderer(const ThemeStylesheet& stylesheet);

    std::unique_ptr<QTextDocument> render(const AstTree& tree);

private:
    void renderNode(QTextCursor& cursor, const AstNode& node);
    void renderDocument(QTextCursor& cursor, const AstNode& node);
    void renderHeading(QTextCursor& cursor, const AstNode& node);
    void renderParagraph(QTextCursor& cursor, const AstNode& node);
    void renderBlockQuote(QTextCursor& cursor, const AstNode& node);
    void renderCodeBlock(QTextCursor& cursor, const AstNode& node);
    void renderList(QTextCursor& cursor, const AstNode& node);
    void renderItem(QTextCursor& cursor, const AstNode& node, int index);
    void renderThematicBreak(QTextCursor& cursor);
    void renderHtmlBlock(QTextCursor& cursor, const AstNode& node);

    void renderInlineChildren(QTextCursor& cursor, const AstNode& node);
    void renderText(QTextCursor& cursor, const AstNode& node);
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

    const ThemeStylesheet& m_ss;
};

} // namespace Muffin
