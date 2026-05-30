#pragma once

#include "Theme.h"
#include <QTextDocument>
#include <QTextCursor>
#include <memory>

struct cmark_node;

namespace Md {

struct InlineFormat {
    bool bold = false;
    bool italic = false;
    bool strikethrough = false;
    bool code = false;
    bool math = false;
    bool link = false;
    int headingLevel = 0;
    bool quote = false;
};

class DocumentRenderer {
public:
    DocumentRenderer();

    void renderDocument(cmark_node *cmarkRoot, QTextDocument *target);
    void setTheme(const Theme &theme);

private:
    Theme m_theme;

    // Block renderers
    void renderBlock(cmark_node *node, QTextCursor &cursor);
    void renderParagraph(cmark_node *node, QTextCursor &cursor);
    void renderHeading(cmark_node *node, QTextCursor &cursor);
    void renderBlockQuote(cmark_node *node, QTextCursor &cursor);
    void renderList(cmark_node *node, QTextCursor &cursor);
    void renderListItems(cmark_node *listNode, QTextCursor &cursor, int depth);
    void renderCodeBlock(cmark_node *node, QTextCursor &cursor);
    void renderHtmlBlock(cmark_node *node, QTextCursor &cursor);
    void renderMathBlock(cmark_node *node, QTextCursor &cursor);
    void renderThematicBreak(cmark_node *node, QTextCursor &cursor);
    void renderTable(cmark_node *node, QTextCursor &cursor);

    // Inline rendering
    void renderInlineSubtree(cmark_node *inlineNode, QTextCursor &cursor,
                             const InlineFormat &fmt);
    void renderInlineLeaf(cmark_node *leafNode, QTextCursor &cursor,
                          const InlineFormat &fmt);

    // Meta-marker rendering
    void renderOpeningMarker(cmark_node *node, QTextCursor &cursor);
    void renderClosingMarker(cmark_node *node, QTextCursor &cursor);
    void renderMetaMarker(const QString &marker, QTextCursor &cursor);

    // Helpers
    QTextCharFormat buildCharFormat(const InlineFormat &fmt) const;
    void renderInlineChildren(cmark_node *parent, QTextCursor &cursor,
                              const InlineFormat &fmt = {});
};

} // namespace Md
