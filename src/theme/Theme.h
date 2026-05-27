#pragma once
#include <QColor>
#include <QString>
#include <QFont>

namespace Muffin {

enum class ThemePreset {
    Github,
    Newsprint,
    Night,
    Pixyll,
    Whitey
};

struct Theme {
    QColor background;
    QColor foreground;
    QColor chromeBackground;
    QColor pageBackground;
    QColor menuBackground;
    QColor menuForeground;
    QColor statusBackground;
    QColor accentColor;
    QFont bodyFont;
    double lineHeight = 1.6;

    QColor headingColor;
    QColor headingBorderColor;
    double headingSize[6] = {2.0, 1.5, 1.25, 1.0, 0.875, 0.85};

    QColor codeBackground;
    QColor codeForeground;
    QFont codeFont;
    double codeFontSizeScale = 0.85;

    QColor quoteBorderColor;
    QColor quoteForeground;

    QColor tableBorderColor;
    QColor tableHeaderBackground;

    QColor linkColor;

    QColor hrColor;

    static Theme preset(ThemePreset preset);
    static QString displayName(ThemePreset preset);
    static Theme lightTheme();
    static Theme darkTheme();
};

} // namespace Muffin
