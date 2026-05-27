#include "Theme.h"

namespace Muffin {

namespace {

Theme baseTheme()
{
    Theme t;
    t.background = QColor("#ffffff");
    t.foreground = QColor("#24292f");
    t.chromeBackground = QColor("#ffffff");
    t.pageBackground = QColor("#ffffff");
    t.menuBackground = QColor("#ffffff");
    t.menuForeground = QColor("#24292f");
    t.statusBackground = QColor("#ffffff");
    t.accentColor = QColor("#0969da");
    t.bodyFont = QFont("Segoe UI", 11);
    t.lineHeight = 1.55;

    t.headingColor = QColor("#24292f");
    t.headingBorderColor = QColor("#d8dee4");

    t.codeBackground = QColor("#f6f8fa");
    t.codeForeground = QColor("#24292f");
    t.codeFont = QFont("Consolas", 10);
    t.codeFontSizeScale = 0.9;

    t.quoteBorderColor = QColor("#d0d7de");
    t.quoteForeground = QColor("#57606a");

    t.tableBorderColor = QColor("#d0d7de");
    t.tableHeaderBackground = QColor("#f6f8fa");

    t.linkColor = QColor("#0969da");
    t.hrColor = QColor("#d8dee4");
    return t;
}

} // namespace

Theme Theme::preset(ThemePreset preset) {
    Theme t = baseTheme();

    switch (preset) {
    case ThemePreset::Github:
        break;
    case ThemePreset::Newsprint:
        t.background = QColor("#fbf7ef");
        t.pageBackground = QColor("#fbf7ef");
        t.chromeBackground = QColor("#fbf7ef");
        t.menuBackground = QColor("#fffaf2");
        t.statusBackground = QColor("#fbf7ef");
        t.foreground = QColor("#3d3328");
        t.headingColor = QColor("#2f281f");
        t.headingBorderColor = QColor("#e2d7c5");
        t.codeBackground = QColor("#eee4d4");
        t.codeForeground = QColor("#3d3328");
        t.quoteBorderColor = QColor("#c8b99f");
        t.quoteForeground = QColor("#6f6251");
        t.tableBorderColor = QColor("#d8cab6");
        t.tableHeaderBackground = QColor("#f0e6d6");
        t.linkColor = QColor("#8b4f1f");
        t.hrColor = QColor("#e2d7c5");
        t.bodyFont = QFont("Georgia", 12);
        t.lineHeight = 1.7;
        break;
    case ThemePreset::Night:
        t.background = QColor("#1f2428");
        t.pageBackground = QColor("#1f2428");
        t.chromeBackground = QColor("#1f2428");
        t.menuBackground = QColor("#252b30");
        t.menuForeground = QColor("#d7dde2");
        t.statusBackground = QColor("#1f2428");
        t.foreground = QColor("#d7dde2");
        t.headingColor = QColor("#f0f3f6");
        t.headingBorderColor = QColor("#3b4249");
        t.codeBackground = QColor("#2d3339");
        t.codeForeground = QColor("#d7dde2");
        t.quoteBorderColor = QColor("#57606a");
        t.quoteForeground = QColor("#aeb7c0");
        t.tableBorderColor = QColor("#444c56");
        t.tableHeaderBackground = QColor("#2d3339");
        t.linkColor = QColor("#6cb6ff");
        t.hrColor = QColor("#3b4249");
        break;
    case ThemePreset::Pixyll:
        t.background = QColor("#ffffff");
        t.pageBackground = QColor("#ffffff");
        t.foreground = QColor("#333333");
        t.headingColor = QColor("#111111");
        t.headingBorderColor = QColor("#eeeeee");
        t.codeBackground = QColor("#f7f7f7");
        t.codeForeground = QColor("#333333");
        t.quoteBorderColor = QColor("#0076df");
        t.quoteForeground = QColor("#666666");
        t.linkColor = QColor("#0076df");
        t.bodyFont = QFont("Avenir Next", 11);
        t.lineHeight = 1.65;
        break;
    case ThemePreset::Whitey:
        t.background = QColor("#ffffff");
        t.pageBackground = QColor("#ffffff");
        t.foreground = QColor("#222222");
        t.headingColor = QColor("#222222");
        t.headingBorderColor = QColor("#f0f0f0");
        t.codeBackground = QColor("#f4f4f4");
        t.codeForeground = QColor("#222222");
        t.quoteBorderColor = QColor("#dddddd");
        t.quoteForeground = QColor("#777777");
        t.tableBorderColor = QColor("#e5e5e5");
        t.tableHeaderBackground = QColor("#fafafa");
        t.linkColor = QColor("#2f6f9f");
        t.hrColor = QColor("#eeeeee");
        t.bodyFont = QFont("Segoe UI", 11);
        t.lineHeight = 1.6;
        break;
    }

    return t;
}

QString Theme::displayName(ThemePreset preset) {
    switch (preset) {
    case ThemePreset::Github:
        return QStringLiteral("Github");
    case ThemePreset::Newsprint:
        return QStringLiteral("Newsprint");
    case ThemePreset::Night:
        return QStringLiteral("Night");
    case ThemePreset::Pixyll:
        return QStringLiteral("Pixyll");
    case ThemePreset::Whitey:
        return QStringLiteral("Whitey");
    }

    return QStringLiteral("Github");
}

Theme Theme::lightTheme() {
    return preset(ThemePreset::Github);
}

Theme Theme::darkTheme() {
    return preset(ThemePreset::Night);
}

} // namespace Muffin
