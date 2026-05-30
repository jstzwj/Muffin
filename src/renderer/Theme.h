#pragma once

#include <QString>
#include <QFont>
#include <QColor>

namespace Md {

class Theme {
public:
    Theme();

    static Theme preset(int index);
    static int presetCount();

    QString name() const { return m_name; }

    // Fonts
    QFont bodyFont() const { return m_bodyFont; }
    QFont headingFont() const { return m_headingFont; }
    QFont codeFont() const { return m_codeFont; }
    double bodyFontSize() const { return m_bodyFontSize; }
    double codeFontSizeScale() const { return m_codeFontSizeScale; }
    double lineHeight() const { return m_lineHeight; }

    // Text / colors
    QColor foreground() const { return m_foreground; }
    QColor textColor() const { return m_foreground; }
    QColor backgroundColor() const { return m_background; }
    QColor linkColor() const { return m_linkColor; }
    QColor metaColor() const { return m_metaColor; }

    // Heading
    QColor headingColor() const { return m_headingColor; }
    QColor headingBorderColor() const { return m_headingBorderColor; }
    double headingSize(int level) const { return m_headingSize[level - 1]; }
    int metaMarkerPointSize() const { return qMax(1, static_cast<int>(m_bodyFontSize) - 2); }

    // Code
    QColor codeBgColor() const { return m_codeBackground; }
    QColor codeForegroundColor() const { return m_codeForeground; }

    // Blockquote
    QColor blockQuoteBorderColor() const { return m_quoteBorderColor; }
    QColor quoteForegroundColor() const { return m_quoteForeground; }

    // Table
    QColor tableBorderColor() const { return m_tableBorderColor; }
    QColor tableHeaderBgColor() const { return m_tableHeaderBackground; }

    // Thematic break
    QColor thematicBreakColor() const { return m_hrColor; }

    // Chrome / UI
    QColor chromeBackground() const { return m_chromeBackground; }
    QColor pageBackground() const { return m_pageBackground; }
    QColor menuBackground() const { return m_menuBackground; }
    QColor menuForeground() const { return m_menuForeground; }
    QColor statusBackground() const { return m_statusBackground; }

private:
    QString m_name;

    QFont m_bodyFont;
    QFont m_headingFont;
    QFont m_codeFont;
    double m_bodyFontSize = 14.0;
    double m_codeFontSizeScale = 0.9;
    double m_lineHeight = 1.55;

    QColor m_foreground;
    QColor m_background;
    QColor m_linkColor;
    QColor m_metaColor;

    QColor m_headingColor;
    QColor m_headingBorderColor;
    double m_headingSize[6] = {2.0, 1.5, 1.25, 1.0, 0.875, 0.85};

    QColor m_codeBackground;
    QColor m_codeForeground;

    QColor m_quoteBorderColor;
    QColor m_quoteForeground;

    QColor m_tableBorderColor;
    QColor m_tableHeaderBackground;

    QColor m_hrColor;

    // Chrome
    QColor m_chromeBackground;
    QColor m_pageBackground;
    QColor m_menuBackground;
    QColor m_menuForeground;
    QColor m_statusBackground;
};

} // namespace Md
