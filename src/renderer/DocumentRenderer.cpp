#include "DocumentRenderer.h"

#include <cmark-gfm.h>
#include <cmark-gfm-extension_api.h>
extern "C" {
#include <strikethrough.h>
#include <table.h>
#include <cmark-gfm-math.h>
}

#include <QTextTable>
#include <QTextTableFormat>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextFrameFormat>
#include <QTextLength>

namespace Md {

DocumentRenderer::DocumentRenderer() {}

void DocumentRenderer::setTheme(const Theme &theme) {
    m_theme = theme;
}

void DocumentRenderer::renderDocument(cmark_node *cmarkRoot, QTextDocument *target) {
    if (!cmarkRoot || !target) return;

    target->clear();
    target->setDefaultFont(m_theme.bodyFont());
    target->setDocumentMargin(0);

    // Set document-wide line height via default stylesheet
    double lineH = m_theme.lineHeight();
    target->setDefaultStyleSheet(
        QString("body { line-height: %1; }").arg(lineH));

    QTextCursor cursor(target);
    QTextCharFormat defaultFmt;
    defaultFmt.setFont(m_theme.bodyFont());
    defaultFmt.setForeground(m_theme.textColor());
    cursor.setCharFormat(defaultFmt);

    // Walk document children (skip the DOCUMENT node itself)
    cmark_node *child = cmark_node_first_child(cmarkRoot);
    while (child) {
        cmark_node *next = cmark_node_next(child);
        renderBlock(child, cursor);
        child = next;
    }

    // Remove the leading empty block
    cursor.movePosition(QTextCursor::Start);
    QTextBlock first = target->begin();
    if (first.isValid() && first.text().isEmpty() && first.next().isValid()) {
        cursor.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor);
        cursor.deleteChar();
    }
}

void DocumentRenderer::renderBlock(cmark_node *node, QTextCursor &cursor) {
    cmark_node_type type = cmark_node_get_type(node);

    switch (type) {
    case CMARK_NODE_PARAGRAPH:
        renderParagraph(node, cursor);
        break;
    case CMARK_NODE_HEADING:
        renderHeading(node, cursor);
        break;
    case CMARK_NODE_BLOCK_QUOTE:
        renderBlockQuote(node, cursor);
        break;
    case CMARK_NODE_LIST:
        renderList(node, cursor);
        break;
    case CMARK_NODE_CODE_BLOCK:
        renderCodeBlock(node, cursor);
        break;
    case CMARK_NODE_HTML_BLOCK:
        renderHtmlBlock(node, cursor);
        break;
    case CMARK_NODE_THEMATIC_BREAK:
        renderThematicBreak(node, cursor);
        break;
    default:
        // Check extension types
        if (CMARK_NODE_TABLE && type == CMARK_NODE_TABLE) {
            renderTable(node, cursor);
        } else if ((CMARK_NODE_MATH_DISPLAY && type == CMARK_NODE_MATH_DISPLAY) ||
                   (CMARK_NODE_MATH_FENCED && type == CMARK_NODE_MATH_FENCED)) {
            renderMathBlock(node, cursor);
        }
        break;
    }
}

// ── Helpers ──

QTextCharFormat DocumentRenderer::buildCharFormat(const InlineFormat &fmt) const {
    QTextCharFormat f;
    if (fmt.headingLevel > 0) {
        f.setFont(m_theme.headingFont());
        f.setFontPointSize(m_theme.bodyFontSize() * m_theme.headingSize(fmt.headingLevel));
        f.setFontWeight(QFont::Bold);
        f.setForeground(m_theme.headingColor());
    } else {
        f.setFont(m_theme.bodyFont());
        f.setFontPointSize(m_theme.bodyFontSize());
        f.setForeground(fmt.quote ? m_theme.quoteForegroundColor() : m_theme.textColor());
    }

    if (fmt.bold) f.setFontWeight(QFont::Bold);
    if (fmt.italic) f.setFontItalic(true);
    if (fmt.strikethrough) f.setFontStrikeOut(true);
    if (fmt.math) {
        f.setFont(m_theme.codeFont());
        f.setForeground(m_theme.metaColor());
    }
    if (fmt.link) {
        f.setForeground(m_theme.linkColor());
        f.setFontUnderline(true);
    }
    if (fmt.code) {
        f.setFont(m_theme.codeFont());
        f.setFontPointSize(m_theme.bodyFontSize() * m_theme.codeFontSizeScale());
        f.setForeground(m_theme.codeForegroundColor());
    }

    return f;
}

void DocumentRenderer::renderInlineChildren(cmark_node *parent, QTextCursor &cursor,
                                            const InlineFormat &fmt) {
    cmark_node *child = cmark_node_first_child(parent);
    while (child) {
        renderInlineSubtree(child, cursor, fmt);
        child = cmark_node_next(child);
    }
}

// ── Block Renderers ──

void DocumentRenderer::renderParagraph(cmark_node *node, QTextCursor &cursor) {
    QTextBlockFormat bf;
    bf.setTopMargin(7);
    bf.setBottomMargin(7);
    bf.setLineHeight(m_theme.lineHeight() * 100.0, QTextBlockFormat::ProportionalHeight);
    cursor.insertBlock(bf);
    InlineFormat fmt;
    cursor.setCharFormat(buildCharFormat(fmt));
    renderInlineChildren(node, cursor, fmt);
}

void DocumentRenderer::renderHeading(cmark_node *node, QTextCursor &cursor) {
    int level = cmark_node_get_heading_level(node);

    QTextBlockFormat bf;
    if (level <= 2) {
        bf.setTopMargin(level == 1 ? 28 : 24);
        bf.setBottomMargin(9);
    } else {
        bf.setTopMargin(18);
        bf.setBottomMargin(7);
    }
    bf.setLineHeight(118, QTextBlockFormat::ProportionalHeight);
    cursor.insertBlock(bf);

    InlineFormat headingFmt;
    headingFmt.headingLevel = level;
    cursor.setCharFormat(buildCharFormat(headingFmt));

    renderInlineChildren(node, cursor, headingFmt);

    if (level <= 2) {
        QTextBlockFormat borderBf;
        borderBf.setTopMargin(2);
        borderBf.setBottomMargin(6);
        cursor.insertBlock(borderBf);
        QTextCharFormat hrFmt;
        hrFmt.setFont(m_theme.bodyFont());
        hrFmt.setFontPointSize(4);
        hrFmt.setForeground(m_theme.headingBorderColor());
        cursor.insertText(QString(QChar(0x2500)).repeated(96), hrFmt);
    }
}

void DocumentRenderer::renderBlockQuote(cmark_node *node, QTextCursor &cursor) {
    cmark_node *child = cmark_node_first_child(node);
    while (child) {
        cmark_node_type childType = cmark_node_get_type(child);
        if (childType == CMARK_NODE_PARAGRAPH) {
            QTextBlockFormat bf;
            bf.setLeftMargin(18);
            bf.setTopMargin(8);
            bf.setBottomMargin(8);
            bf.setRightMargin(10);
            bf.setBackground(QColor(m_theme.blockQuoteBorderColor().red(),
                                    m_theme.blockQuoteBorderColor().green(),
                                    m_theme.blockQuoteBorderColor().blue(), 18));
            bf.setLineHeight(m_theme.lineHeight() * 100.0, QTextBlockFormat::ProportionalHeight);
            cursor.insertBlock(bf);
            InlineFormat quoteFmt;
            quoteFmt.quote = true;
            cursor.setCharFormat(buildCharFormat(quoteFmt));
            renderInlineChildren(child, cursor, quoteFmt);
        } else if (childType == CMARK_NODE_BLOCK_QUOTE) {
            cmark_node *inner = cmark_node_first_child(child);
            while (inner) {
                if (cmark_node_get_type(inner) == CMARK_NODE_PARAGRAPH) {
                    QTextBlockFormat bf;
                    bf.setLeftMargin(36);
                    bf.setTopMargin(6);
                    bf.setBottomMargin(6);
                    bf.setBackground(QColor(m_theme.blockQuoteBorderColor().red(),
                                            m_theme.blockQuoteBorderColor().green(),
                                            m_theme.blockQuoteBorderColor().blue(), 18));
                    bf.setLineHeight(m_theme.lineHeight() * 100.0, QTextBlockFormat::ProportionalHeight);
                    cursor.insertBlock(bf);
                    InlineFormat quoteFmt;
                    quoteFmt.quote = true;
                    cursor.setCharFormat(buildCharFormat(quoteFmt));
                    renderInlineChildren(inner, cursor, quoteFmt);
                }
                inner = cmark_node_next(inner);
            }
        }
        child = cmark_node_next(child);
    }
}

void DocumentRenderer::renderList(cmark_node *node, QTextCursor &cursor) {
    renderListItems(node, cursor, 0);
}

void DocumentRenderer::renderListItems(cmark_node *listNode, QTextCursor &cursor, int depth) {
    cmark_list_type listType = cmark_node_get_list_type(listNode);
    int listStart = cmark_node_get_list_start(listNode);
    int index = (listType == CMARK_ORDERED_LIST) ? listStart : 0;

    cmark_node *item = cmark_node_first_child(listNode);
    while (item) {
        if (cmark_node_get_type(item) != CMARK_NODE_ITEM) {
            item = cmark_node_next(item);
            continue;
        }

        QTextBlockFormat bf;
        bf.setLeftMargin(22.0 * (depth + 1));
        bf.setTopMargin(3);
        bf.setBottomMargin(3);
        bf.setLineHeight(m_theme.lineHeight() * 100.0, QTextBlockFormat::ProportionalHeight);
        cursor.insertBlock(bf);

        // Render marker
        QString marker;
        if (listType == CMARK_BULLET_LIST) {
            marker = QString(QChar(0x2022)) + " ";
        } else {
            cmark_delim_type delim = cmark_node_get_list_delim(listNode);
            marker = QString::number(index) + (delim == CMARK_PERIOD_DELIM ? ". " : ") ");
            index++;
        }
        QTextCharFormat markerFmt;
        markerFmt.setFont(m_theme.bodyFont());
        markerFmt.setFontPointSize(m_theme.bodyFontSize());
        markerFmt.setForeground(m_theme.textColor());
        cursor.insertText(marker, markerFmt);

        // Render item children
        cmark_node *child = cmark_node_first_child(item);
        while (child) {
            cmark_node_type childType = cmark_node_get_type(child);
            if (childType == CMARK_NODE_PARAGRAPH) {
                InlineFormat itemFmt;
                renderInlineChildren(child, cursor, itemFmt);
            } else if (childType == CMARK_NODE_LIST) {
                renderListItems(child, cursor, depth + 1);
            } else if (childType == CMARK_NODE_CODE_BLOCK) {
                renderCodeBlock(child, cursor);
            }
            child = cmark_node_next(child);
        }

        item = cmark_node_next(item);
    }
}

void DocumentRenderer::renderCodeBlock(cmark_node *node, QTextCursor &cursor) {
    QTextBlockFormat bf;
    bf.setTopMargin(10);
    bf.setBottomMargin(10);
    bf.setLeftMargin(0);
    bf.setRightMargin(0);
    bf.setBackground(m_theme.codeBgColor());
    bf.setLineHeight(145, QTextBlockFormat::ProportionalHeight);
    cursor.insertBlock(bf);

    QTextCharFormat fmt;
    fmt.setFont(m_theme.codeFont());
    fmt.setFontPointSize(m_theme.bodyFontSize() * m_theme.codeFontSizeScale());
    fmt.setForeground(m_theme.codeForegroundColor());

    const char *lit = cmark_node_get_literal(node);
    if (lit) {
        QString code = QString::fromUtf8(lit);
        // Replace newlines with QText line breaks
        QStringList lines = code.split('\n');
        for (int i = 0; i < lines.size(); i++) {
            if (i > 0) {
                cursor.insertBlock(bf, fmt);
            }
            cursor.insertText(lines[i], fmt);
        }
    }
}

void DocumentRenderer::renderHtmlBlock(cmark_node *node, QTextCursor &cursor) {
    QTextBlockFormat bf;
    bf.setTopMargin(8);
    bf.setBottomMargin(8);
    bf.setBackground(m_theme.codeBgColor());
    cursor.insertBlock(bf);

    QTextCharFormat fmt;
    fmt.setFont(m_theme.codeFont());
    fmt.setFontPointSize(m_theme.bodyFontSize() * m_theme.codeFontSizeScale());
    fmt.setForeground(m_theme.metaColor());
    cursor.setCharFormat(fmt);

    const char *lit = cmark_node_get_literal(node);
    if (lit) cursor.insertText(QString::fromUtf8(lit), fmt);
}

void DocumentRenderer::renderMathBlock(cmark_node *node, QTextCursor &cursor) {
    QTextBlockFormat bf;
    bf.setTopMargin(12);
    bf.setBottomMargin(12);
    bf.setLeftMargin(0);
    bf.setRightMargin(0);
    bf.setAlignment(Qt::AlignCenter);
    bf.setBackground(m_theme.codeBgColor());
    cursor.insertBlock(bf);

    QTextCharFormat fmt;
    fmt.setFont(m_theme.codeFont());
    fmt.setFontPointSize(m_theme.bodyFontSize() * m_theme.codeFontSizeScale());
    fmt.setForeground(m_theme.metaColor());
    cursor.setCharFormat(fmt);

    // Get math source — try literal first, then walk children
    const char *lit = cmark_node_get_literal(node);
    if (lit) {
        cursor.insertText(QString::fromUtf8(lit), fmt);
    } else {
        cmark_node *child = cmark_node_first_child(node);
        while (child) {
            const char *clit = cmark_node_get_literal(child);
            if (clit) cursor.insertText(QString::fromUtf8(clit), fmt);
            child = cmark_node_next(child);
        }
    }
}

void DocumentRenderer::renderThematicBreak(cmark_node *node, QTextCursor &cursor) {
    Q_UNUSED(node)
    QTextBlockFormat bf;
    bf.setTopMargin(16);
    bf.setBottomMargin(16);
    cursor.insertBlock(bf);
    QTextCharFormat fmt;
    fmt.setFont(m_theme.bodyFont());
    fmt.setFontPointSize(4);
    fmt.setForeground(m_theme.thematicBreakColor());
    cursor.insertText(QString(QChar(0x2500)).repeated(96), fmt);
}

void DocumentRenderer::renderTable(cmark_node *node, QTextCursor &cursor) {
    // Get table dimensions via extension API
    int ncols = 1;
    cmark_syntax_extension *ext = cmark_node_get_syntax_extension(node);
    if (ext) {
        ncols = cmark_gfm_extensions_get_table_columns(node);
    }
    if (ncols <= 0) ncols = 1;

    // Count rows
    int nrows = 0;
    cmark_node *row = cmark_node_first_child(node);
    while (row) {
        if (cmark_node_get_type(row) == CMARK_NODE_TABLE_ROW)
            nrows++;
        row = cmark_node_next(row);
    }
    if (nrows == 0) return;

    QTextBlockFormat beforeTable;
    beforeTable.setTopMargin(12);
    beforeTable.setBottomMargin(2);
    cursor.insertBlock(beforeTable);
    int tableStartPos = cursor.position();

    QTextTable *table = cursor.insertTable(nrows, ncols);
    QTextTableFormat tff = table->format();
    tff.setBorder(1);
    tff.setBorderBrush(m_theme.tableBorderColor());
    tff.setCellPadding(8);
    tff.setCellSpacing(0);
    QVector<QTextLength> widths;
    widths.reserve(ncols);
    for (int i = 0; i < ncols; ++i)
        widths.append(QTextLength(QTextLength::PercentageLength, 100.0 / ncols));
    tff.setColumnWidthConstraints(widths);
    table->setFormat(tff);

    int r = 0;
    row = cmark_node_first_child(node);
    while (row) {
        if (cmark_node_get_type(row) != CMARK_NODE_TABLE_ROW) {
            row = cmark_node_next(row);
            continue;
        }

        bool isHeader = false;
        cmark_syntax_extension *rowExt = cmark_node_get_syntax_extension(row);
        if (rowExt) {
            isHeader = cmark_gfm_extensions_get_table_row_is_header(row);
        }

        int c = 0;
        cmark_node *cell = cmark_node_first_child(row);
        while (cell && c < ncols) {
            if (cmark_node_get_type(cell) == CMARK_NODE_TABLE_CELL) {
                QTextTableCell tableCell = table->cellAt(r, c);
                QTextCursor cellCursor = tableCell.firstCursorPosition();

                QTextCharFormat cellFmt;
                cellFmt.setFont(m_theme.bodyFont());
                cellFmt.setFontPointSize(m_theme.bodyFontSize());
                cellFmt.setForeground(m_theme.textColor());
                if (isHeader) {
                    cellFmt.setFontWeight(QFont::Bold);
                    cellFmt.setBackground(m_theme.tableHeaderBgColor());
                } else if (r % 2 == 1) {
                    cellFmt.setBackground(m_theme.tableAltRowBgColor());
                }
                cellCursor.setCharFormat(cellFmt);

                InlineFormat emptyFmt;
                if (isHeader)
                    emptyFmt.bold = true;
                cmark_node *inlineChild = cmark_node_first_child(cell);
                while (inlineChild) {
                    renderInlineSubtree(inlineChild, cellCursor, emptyFmt);
                    inlineChild = cmark_node_next(inlineChild);
                }
                c++;
            }
            cell = cmark_node_next(cell);
        }
        r++;
        row = cmark_node_next(row);
    }

    // Move cursor past the table
    cursor.setPosition(tableStartPos + 1);
    cursor.movePosition(QTextCursor::End);
    QTextBlockFormat afterTable;
    afterTable.setTopMargin(2);
    afterTable.setBottomMargin(12);
    cursor.insertBlock(afterTable);
}

// ── Inline Rendering ──

void DocumentRenderer::renderInlineSubtree(cmark_node *node, QTextCursor &cursor,
                                            const InlineFormat &fmt) {
    cmark_node_type type = cmark_node_get_type(node);

    // Check for math inline (dynamic type)
    bool isMathInline = (CMARK_NODE_MATH_INLINE && type == CMARK_NODE_MATH_INLINE);

    // Leaf nodes — render directly
    if (type == CMARK_NODE_TEXT || type == CMARK_NODE_SOFTBREAK ||
        type == CMARK_NODE_LINEBREAK || type == CMARK_NODE_CODE ||
        type == CMARK_NODE_HTML_INLINE) {
        renderInlineLeaf(node, cursor, fmt);
        return;
    }

    // Container inline nodes — push format and recurse
    InlineFormat childFmt = fmt;

    switch (type) {
    case CMARK_NODE_STRONG:
        childFmt.bold = true;
        break;
    case CMARK_NODE_EMPH:
        childFmt.italic = true;
        break;
    case CMARK_NODE_LINK:
        childFmt.link = true;
        break;
    default:
        break;
    }

    // Check strikethrough (dynamic type)
    if (CMARK_NODE_STRIKETHROUGH && type == CMARK_NODE_STRIKETHROUGH) {
        childFmt.strikethrough = true;
    }

    if (isMathInline) {
        childFmt.math = true;
    }

    // Recurse into children
    cmark_node *child = cmark_node_first_child(node);
    while (child) {
        renderInlineSubtree(child, cursor, childFmt);
        child = cmark_node_next(child);
    }

}

void DocumentRenderer::renderInlineLeaf(cmark_node *node, QTextCursor &cursor,
                                         const InlineFormat &fmt) {
    cmark_node_type type = cmark_node_get_type(node);

    if (type == CMARK_NODE_SOFTBREAK) {
        cursor.insertText(QString(QChar::Space));
        return;
    }
    if (type == CMARK_NODE_LINEBREAK) {
        cursor.insertText(QString(QChar::LineSeparator));
        return;
    }

    QTextCharFormat charFmt = buildCharFormat(fmt);

    if (type == CMARK_NODE_CODE) {
        charFmt.setFont(m_theme.codeFont());
        charFmt.setFontPointSize(m_theme.bodyFontSize() * m_theme.codeFontSizeScale());
        charFmt.setBackground(m_theme.codeBgColor());
        charFmt.setForeground(m_theme.codeForegroundColor());
    }

    const char *lit = cmark_node_get_literal(node);
    if (lit) {
        cursor.insertText(QString::fromUtf8(lit), charFmt);
    }
}

// ── Meta-Marker Rendering ──

void DocumentRenderer::renderOpeningMarker(cmark_node *node, QTextCursor &cursor) {
    cmark_node_type type = cmark_node_get_type(node);
    QString marker;

    switch (type) {
    case CMARK_NODE_EMPH:
        marker = "*";
        break;
    case CMARK_NODE_STRONG:
        marker = "**";
        break;
    case CMARK_NODE_CODE:
        marker = "`";
        break;
    default:
        break;
    }

    if (CMARK_NODE_STRIKETHROUGH && type == CMARK_NODE_STRIKETHROUGH)
        marker = "~~";

    if (!marker.isEmpty())
        renderMetaMarker(marker, cursor);

    // Link/Image opening markers
    if (type == CMARK_NODE_LINK) {
        renderMetaMarker("[", cursor);
    } else if (type == CMARK_NODE_IMAGE) {
        renderMetaMarker("![", cursor);
    }
}

void DocumentRenderer::renderClosingMarker(cmark_node *node, QTextCursor &cursor) {
    cmark_node_type type = cmark_node_get_type(node);
    QString marker;

    switch (type) {
    case CMARK_NODE_EMPH:
        marker = "*";
        break;
    case CMARK_NODE_STRONG:
        marker = "**";
        break;
    case CMARK_NODE_CODE:
        marker = "`";
        break;
    default:
        break;
    }

    if (CMARK_NODE_STRIKETHROUGH && type == CMARK_NODE_STRIKETHROUGH)
        marker = "~~";

    if (!marker.isEmpty())
        renderMetaMarker(marker, cursor);

    // Link/Image closing markers
    if (type == CMARK_NODE_LINK) {
        const char *url = cmark_node_get_url(node);
        QString urlStr = url ? QString::fromUtf8(url) : QString();
        renderMetaMarker("](" + urlStr + ")", cursor);
    } else if (type == CMARK_NODE_IMAGE) {
        const char *url = cmark_node_get_url(node);
        QString urlStr = url ? QString::fromUtf8(url) : QString();
        renderMetaMarker("](" + urlStr + ")", cursor);
    }
}

void DocumentRenderer::renderMetaMarker(const QString &marker, QTextCursor &cursor) {
    QTextCharFormat fmt;
    fmt.setFont(m_theme.bodyFont());
    fmt.setFontPointSize(m_theme.metaMarkerPointSize());
    fmt.setForeground(m_theme.metaColor());
    cursor.insertText(marker, fmt);
}

} // namespace Md
