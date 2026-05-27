#pragma once
#include "Theme.h"
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextTableCellFormat>
#include <QTextTableFormat>

namespace Muffin {

class ThemeStylesheet {
public:
    explicit ThemeStylesheet(const Theme& theme);

    QTextBlockFormat bodyBlockFormat() const;
    QTextCharFormat bodyCharFormat() const;

    QTextBlockFormat headingBlockFormat(int level) const;
    QTextCharFormat headingCharFormat(int level) const;

    QTextBlockFormat codeBlockFormat() const;
    QTextCharFormat codeBlockCharFormat() const;

    QTextCharFormat inlineCodeCharFormat() const;
    QTextCharFormat boldCharFormat() const;
    QTextCharFormat italicCharFormat() const;
    QTextCharFormat boldItalicCharFormat() const;
    QTextCharFormat linkCharFormat(const QString& url) const;
    QTextCharFormat strikethroughCharFormat() const;
    QTextCharFormat imageCharFormat() const;

    QTextBlockFormat blockquoteBlockFormat() const;
    QTextCharFormat blockquoteCharFormat() const;

    QTextBlockFormat listBlockFormat() const;
    QTextBlockFormat listItemFormat() const;

    QTextTableFormat tableFormat() const;
    QTextTableCellFormat tableCellFormat(bool isHeader = false) const;
    QTextCharFormat tableHeaderCharFormat() const;
    QTextBlockFormat tableCellBlockFormat(bool isHeader = false) const;

    QTextBlockFormat thematicBreakFormat() const;

    const Theme& theme() const { return m_theme; }

private:
    Theme m_theme;
};

} // namespace Muffin
