#include "Theme.h"

namespace Md {

Theme::Theme() {
    // Initialize with safe defaults (avoid calling preset() which would recurse)
    m_name = "Github";
    m_bodyFont = QFont("Segoe UI", 11);
    m_headingFont = QFont("Segoe UI", 11, QFont::Bold);
    m_codeFont = QFont("Consolas", 10);
    m_bodyFontSize = 14.0;
    m_codeFontSizeScale = 0.9;
    m_lineHeight = 1.55;
    m_foreground = "#24292f";
    m_background = "#ffffff";
    m_linkColor = "#0969da";
    m_metaColor = "#959da5";
    m_headingColor = "#24292f";
    m_headingBorderColor = "#d8dee4";
    m_headingSize[0] = 2.0;
    m_headingSize[1] = 1.5;
    m_headingSize[2] = 1.25;
    m_headingSize[3] = 1.0;
    m_headingSize[4] = 0.875;
    m_headingSize[5] = 0.85;
    m_codeBackground = "#f6f8fa";
    m_codeForeground = "#24292f";
    m_quoteBorderColor = "#d0d7de";
    m_quoteForeground = "#57606a";
    m_tableBorderColor = "#d0d7de";
    m_tableHeaderBackground = "#f6f8fa";
    m_hrColor = "#d8dee4";
    m_chromeBackground = "#ffffff";
    m_pageBackground = "#ffffff";
    m_menuBackground = "#ffffff";
    m_menuForeground = "#24292f";
    m_statusBackground = "#ffffff";
}

Theme Theme::preset(int index) {
    Theme t;
    switch (index) {
    case 0: // Github — already the default
        break;

    case 1: // Newsprint
        t.m_name = "Newsprint";
        t.m_bodyFont = QFont("Georgia", 12);
        t.m_headingFont = QFont("Georgia", 12, QFont::Bold);
        t.m_codeFont = QFont("Consolas", 11);
        t.m_bodyFontSize = 16.0;
        t.m_codeFontSizeScale = 0.9;
        t.m_lineHeight = 1.7;
        t.m_foreground = "#3d3328";
        t.m_background = "#fbf7ef";
        t.m_linkColor = "#8b4f1f";
        t.m_metaColor = "#9a8c74";
        t.m_headingColor = "#2f281f";
        t.m_headingBorderColor = "#c8b99f";
        t.m_codeBackground = "#eee4d4";
        t.m_codeForeground = "#3d3328";
        t.m_quoteBorderColor = "#c8b99f";
        t.m_quoteForeground = "#6f6251";
        t.m_tableBorderColor = "#c8b99f";
        t.m_tableHeaderBackground = "#eee4d4";
        t.m_hrColor = "#e2d7c5";
        t.m_chromeBackground = "#fbf7ef";
        t.m_pageBackground = "#fbf7ef";
        t.m_menuBackground = "#fffaf2";
        t.m_menuForeground = "#3d3328";
        t.m_statusBackground = "#fbf7ef";
        break;

    case 2: // Night
        t.m_name = "Night";
        t.m_foreground = "#d7dde2";
        t.m_background = "#1f2428";
        t.m_linkColor = "#6cb6ff";
        t.m_metaColor = "#768390";
        t.m_headingColor = "#f0f3f6";
        t.m_headingBorderColor = "#444c56";
        t.m_codeBackground = "#2d3339";
        t.m_codeForeground = "#d7dde2";
        t.m_quoteBorderColor = "#57606a";
        t.m_quoteForeground = "#aeb7c0";
        t.m_tableBorderColor = "#444c56";
        t.m_tableHeaderBackground = "#2d3339";
        t.m_hrColor = "#444c56";
        t.m_chromeBackground = "#1f2428";
        t.m_pageBackground = "#1f2428";
        t.m_menuBackground = "#252b30";
        t.m_menuForeground = "#d7dde2";
        t.m_statusBackground = "#1f2428";
        t.m_lineHeight = 1.55;
        break;

    case 3: // Pixyll
        t.m_name = "Pixyll";
        t.m_bodyFont = QFont("Avenir Next", 11);
        t.m_headingFont = QFont("Avenir Next", 11, QFont::Bold);
        t.m_foreground = "#333333";
        t.m_background = "#ffffff";
        t.m_linkColor = "#0076df";
        t.m_metaColor = "#999999";
        t.m_headingColor = "#111111";
        t.m_headingBorderColor = "#0076df";
        t.m_codeBackground = "#f7f7f7";
        t.m_codeForeground = "#333333";
        t.m_quoteBorderColor = "#0076df";
        t.m_quoteForeground = "#666666";
        t.m_tableBorderColor = "#cccccc";
        t.m_tableHeaderBackground = "#f7f7f7";
        t.m_hrColor = "#dddddd";
        t.m_menuForeground = "#333333";
        t.m_lineHeight = 1.65;
        break;

    case 4: // Whitey
        t.m_name = "Whitey";
        t.m_foreground = "#222222";
        t.m_background = "#ffffff";
        t.m_linkColor = "#2f6f9f";
        t.m_metaColor = "#aaaaaa";
        t.m_headingColor = "#222222";
        t.m_headingBorderColor = "#dddddd";
        t.m_codeBackground = "#f4f4f4";
        t.m_codeForeground = "#222222";
        t.m_quoteBorderColor = "#dddddd";
        t.m_quoteForeground = "#777777";
        t.m_tableBorderColor = "#dddddd";
        t.m_tableHeaderBackground = "#f4f4f4";
        t.m_hrColor = "#eeeeee";
        t.m_menuForeground = "#222222";
        t.m_lineHeight = 1.6;
        break;

    default:
        break;
    }
    return t;
}

int Theme::presetCount() {
    return 5;
}

} // namespace Md
