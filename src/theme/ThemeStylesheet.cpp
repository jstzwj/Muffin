#include "ThemeStylesheet.h"

namespace Muffin {

ThemeStylesheet::ThemeStylesheet(const Theme& theme) : m_theme(theme) {}

QTextBlockFormat ThemeStylesheet::bodyBlockFormat() const {
    QTextBlockFormat fmt;
    fmt.setLineHeight(m_theme.lineHeight * 100, QTextBlockFormat::ProportionalHeight);
    fmt.setTopMargin(6);
    fmt.setBottomMargin(6);
    return fmt;
}

QTextCharFormat ThemeStylesheet::bodyCharFormat() const {
    QTextCharFormat fmt;
    fmt.setFont(m_theme.bodyFont);
    fmt.setForeground(m_theme.foreground);
    return fmt;
}

QTextBlockFormat ThemeStylesheet::headingBlockFormat(int level) const {
    QTextBlockFormat fmt;
    double size = m_theme.headingSize[qBound(0, level - 1, 5)];
    int topMargin = level <= 2 ? 20 : static_cast<int>(15.0 / size);
    fmt.setTopMargin(topMargin);
    fmt.setBottomMargin(level <= 2 ? 7 : 5);
    return fmt;
}

QTextCharFormat ThemeStylesheet::headingCharFormat(int level) const {
    QTextCharFormat fmt;
    double scale = m_theme.headingSize[qBound(0, level - 1, 5)];
    QFont font = m_theme.bodyFont;
    font.setPointSizeF(font.pointSizeF() * scale);
    font.setBold(true);
    fmt.setFont(font);
    fmt.setForeground(m_theme.headingColor);
    return fmt;
}

QTextBlockFormat ThemeStylesheet::codeBlockFormat() const {
    QTextBlockFormat fmt;
    fmt.setTopMargin(0);
    fmt.setBottomMargin(0);
    fmt.setLineHeight(145, QTextBlockFormat::ProportionalHeight);
    fmt.setNonBreakableLines(true);
    return fmt;
}

QTextCharFormat ThemeStylesheet::codeBlockCharFormat() const {
    QTextCharFormat fmt;
    QFont font = m_theme.codeFont;
    font.setPointSizeF(font.pointSizeF() * 0.96);
    fmt.setFont(font);
    fmt.setForeground(m_theme.codeForeground);
    return fmt;
}

QTextCharFormat ThemeStylesheet::inlineCodeCharFormat() const {
    QTextCharFormat fmt;
    QFont font = m_theme.codeFont;
    font.setPointSizeF(font.pointSizeF() * m_theme.codeFontSizeScale);
    fmt.setFont(font);
    QColor background = m_theme.codeBackground;
    background.setAlpha(210);
    fmt.setBackground(background);
    fmt.setForeground(m_theme.codeForeground);
    return fmt;
}

QTextCharFormat ThemeStylesheet::boldCharFormat() const {
    QTextCharFormat fmt;
    fmt.setFontWeight(QFont::Bold);
    return fmt;
}

QTextCharFormat ThemeStylesheet::italicCharFormat() const {
    QTextCharFormat fmt;
    fmt.setFontItalic(true);
    return fmt;
}

QTextCharFormat ThemeStylesheet::boldItalicCharFormat() const {
    QTextCharFormat fmt;
    fmt.setFontWeight(QFont::Bold);
    fmt.setFontItalic(true);
    return fmt;
}

QTextCharFormat ThemeStylesheet::linkCharFormat(const QString& url) const {
    QTextCharFormat fmt;
    fmt.setForeground(m_theme.linkColor);
    fmt.setUnderlineStyle(QTextCharFormat::SingleUnderline);
    fmt.setAnchor(true);
    fmt.setAnchorHref(url);
    return fmt;
}

QTextCharFormat ThemeStylesheet::strikethroughCharFormat() const {
    QTextCharFormat fmt;
    fmt.setFontStrikeOut(true);
    return fmt;
}

QTextCharFormat ThemeStylesheet::imageCharFormat() const {
    QTextCharFormat fmt;
    return fmt;
}

QTextBlockFormat ThemeStylesheet::blockquoteBlockFormat() const {
    QTextBlockFormat fmt;
    fmt.setLeftMargin(20);
    fmt.setForeground(m_theme.quoteForeground);
    // Qt doesn't have a native left-border, so we use background tinting
    QColor bg = m_theme.quoteBorderColor;
    bg.setAlpha(40);
    fmt.setBackground(bg);
    return fmt;
}

QTextCharFormat ThemeStylesheet::blockquoteCharFormat() const {
    QTextCharFormat fmt;
    fmt.setForeground(m_theme.quoteForeground);
    return fmt;
}

QTextBlockFormat ThemeStylesheet::listBlockFormat() const {
    QTextBlockFormat fmt;
    fmt.setTopMargin(2);
    fmt.setBottomMargin(2);
    return fmt;
}

QTextBlockFormat ThemeStylesheet::listItemFormat() const {
    QTextBlockFormat fmt;
    fmt.setTopMargin(1);
    fmt.setBottomMargin(1);
    return fmt;
}

QTextTableFormat ThemeStylesheet::tableFormat() const {
    QTextTableFormat fmt;
    fmt.setBorder(0);
    fmt.setCellPadding(0);
    fmt.setCellSpacing(0);
    fmt.setHeaderRowCount(1);
    fmt.setWidth(QTextLength(QTextLength::PercentageLength, 100));
    return fmt;
}

QTextTableCellFormat ThemeStylesheet::tableCellFormat(bool isHeader) const {
    QTextTableCellFormat fmt;
    fmt.setPadding(0);
    fmt.setTopPadding(6);
    fmt.setBottomPadding(6);
    fmt.setLeftPadding(10);
    fmt.setRightPadding(10);
    fmt.setBorder(1);
    fmt.setBorderBrush(m_theme.tableBorderColor);
    fmt.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
    if (isHeader) {
        fmt.setBackground(m_theme.tableHeaderBackground);
    }
    return fmt;
}

QTextCharFormat ThemeStylesheet::tableHeaderCharFormat() const {
    QTextCharFormat fmt;
    fmt.setBackground(m_theme.tableHeaderBackground);
    fmt.setFontWeight(QFont::Bold);
    return fmt;
}

QTextBlockFormat ThemeStylesheet::tableCellBlockFormat(bool isHeader) const {
    QTextBlockFormat fmt;
    fmt.setTopMargin(2);
    fmt.setBottomMargin(2);
    fmt.setLeftMargin(0);
    fmt.setRightMargin(0);
    if (isHeader) {
        fmt.setBackground(m_theme.tableHeaderBackground);
    }
    return fmt;
}

QTextBlockFormat ThemeStylesheet::thematicBreakFormat() const {
    QTextBlockFormat fmt;
    fmt.setTopMargin(16);
    fmt.setBottomMargin(16);
    fmt.setLineHeight(1, QTextBlockFormat::FixedHeight);
    return fmt;
}

} // namespace Muffin
