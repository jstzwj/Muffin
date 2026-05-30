#include "DocumentRenderer.h"
#include "core/MdNode.h"

#include <cmark-gfm.h>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTextBlockFormat>

namespace Md {

DocumentRenderer::DocumentRenderer() {}

void DocumentRenderer::setTheme(const Theme &theme) {
    m_theme = theme;
}

void DocumentRenderer::renderDocument(const MdNode *root, QTextDocument *target) {
    if (!root || !target) return;

    target->clear();
    QTextCursor cursor(target);

    // Set default font
    QTextCharFormat defaultFormat;
    defaultFormat.setFont(m_theme.bodyFont());
    defaultFormat.setForeground(m_theme.textColor());
    cursor.setCharFormat(defaultFormat);

    // Walk AST and render
    cmark_node *cnode = root->cmarkNodePtr();
    cmark_iter *iter = cmark_iter_new(cnode);
    cmark_event_type ev_type;

    while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        cmark_node *node = cmark_iter_get_node(iter);
        cmark_node_type type = cmark_node_get_type(node);

        if (ev_type != CMARK_EVENT_ENTER) continue;
        if (!(type & CMARK_NODE_TYPE_BLOCK)) continue;

        switch (type) {
        case CMARK_NODE_PARAGRAPH: {
            QTextBlockFormat bf;
            bf.setTopMargin(4);
            bf.setBottomMargin(4);
            cursor.insertBlock(bf);
            // Render inline children
            cmark_node *child = cmark_node_first_child(node);
            while (child) {
                const char *lit = cmark_node_get_literal(child);
                if (lit) {
                    cmark_node_type ctype = cmark_node_get_type(child);
                    QTextCharFormat fmt;
                    fmt.setFont(m_theme.bodyFont());
                    if (ctype == CMARK_NODE_STRONG) fmt.setFontWeight(QFont::Bold);
                    else if (ctype == CMARK_NODE_EMPH) fmt.setFontItalic(true);
                    else if (ctype == CMARK_NODE_CODE) {
                        fmt.setFont(m_theme.codeFont());
                        fmt.setBackground(m_theme.codeBgColor());
                    }
                    cursor.insertText(QString::fromUtf8(lit), fmt);
                }
                child = cmark_node_next(child);
            }
            break;
        }
        case CMARK_NODE_HEADING: {
            int level = cmark_node_get_heading_level(node);
            QTextBlockFormat bf;
            bf.setTopMargin(12);
            bf.setBottomMargin(4);
            cursor.insertBlock(bf);
            QTextCharFormat fmt;
            fmt.setFont(m_theme.headingFont());
            int sz = m_theme.bodyFontSize() + (6 - level) * 2;
            fmt.setFontPointSize(sz > 10 ? sz : 10);
            cmark_node *child = cmark_node_first_child(node);
            while (child) {
                const char *lit = cmark_node_get_literal(child);
                if (lit) cursor.insertText(QString::fromUtf8(lit), fmt);
                child = cmark_node_next(child);
            }
            break;
        }
        default:
            break;
        }
    }

    cmark_iter_free(iter);

    // Remove the initial empty block
    cursor.movePosition(QTextCursor::Start);
    cursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor);
    if (cursor.selectedText().trimmed().isEmpty()) {
        cursor.deleteChar();
    }
}

} // namespace Md
