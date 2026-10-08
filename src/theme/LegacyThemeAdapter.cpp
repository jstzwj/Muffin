#include "theme/LegacyThemeAdapter.h"
#include "theme/CssThemeMapper.h"
#include "theme/CssThemeParser.h"
#include <QStringList>

namespace muffin {
namespace {
QString number(qreal value) { return QString::number(value, 'g', 14); }
QString color(const QColor& value) {
  return QStringLiteral("rgba(%1,%2,%3,%4)").arg(value.red()).arg(value.green()).arg(value.blue()).arg(number(value.alphaF()));
}
QString family(const QString& value) {
  QStringList values = value.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  for (auto& item : values) {
    item.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    item.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    item = QLatin1Char('"') + item + QLatin1Char('"');
  }
  return values.join(QLatin1Char(','));
}
QString margins(QMarginsF value) {
  return number(value.top()) + "px " + number(value.right()) + "px " + number(value.bottom()) + "px " + number(value.left()) + "px";
}
QString alignment(Qt::Alignment value) {
  if (value & Qt::AlignHCenter) return QStringLiteral("center");
  if (value & Qt::AlignRight) return QStringLiteral("right");
  if (value & Qt::AlignJustify) return QStringLiteral("justify");
  return QStringLiteral("left");
}
}  // namespace
ThemeDefinition adaptLegacyTheme(const ThemeDefinition& input) {
  if (input.sourceSheet) return input;
  QString css;
  auto rule = [&](const QString& selector, const QString& declarations) { css += selector + "{" + declarations + "}\n"; };
  auto paint = [&](const QString& selector, const QString& property, const QColor& value) {
    if (value.isValid()) rule(selector, property + ":" + color(value));
  };
  const auto& c = input.colors;
  const auto& t = input.typography;
  rule("body", "color:#202124;background:white;font-size:16px;line-height:1.6");
  rule("#write", "max-width:700px");
  rule("pre,code", "font-size:14.4px");
  rule("pre", "background:#f6f8fa;border:1px solid #e5e7eb;line-height:1.6");
  rule("blockquote", "border-left:3px solid #d0d7de");
  rule("th,td", "border:1px solid #dfe2e5");
  rule("th", "background:#edf4ff");
  rule("tbody tr:nth-child(even)", "background:#f6f8fa");
  paint("body", "color", c.text);
  paint("body", "background", c.background);
  paint("a", "color", c.link);
  paint("code", "background", c.codeBackground);
  paint("pre", "background", c.codeBlockBackground.isValid() ? c.codeBlockBackground : c.codeBackground);
  paint("pre", "border-color", c.codeBorder);
  paint("blockquote", "border-left-color", c.quoteBorder);
  paint("blockquote", "background", c.blockquoteBackground);
  paint("th,td", "border-color", c.tableBorder);
  paint("th", "background", c.tableHeaderBackground);
  paint("tbody tr:nth-child(even)", "background", c.tableAlternateBackground);
  paint("mark", "background", c.highlight);
  paint("::selection", "background", c.selection);
  if (!t.bodyFont.isEmpty())
    rule("body", "font-family:" + family(t.bodyFont));
  else if (c.serifBody)
    rule("body", "font-family:serif");
  if (t.bodySizePt > 0) rule("body", "font-size:" + number((t.bodySizePt * 96.0 / 72.0)) + "px");
  if (t.lineHeight > 0) rule("body", "line-height:" + number(t.lineHeight));
  if (t.bodyAlignment) rule("body", "text-align:" + alignment(t.bodyAlignment));
  if (t.letterSpacing) rule("body", "letter-spacing:" + number(t.letterSpacing) + "px");
  if (!t.headingFont.isEmpty()) rule("h1,h2,h3,h4,h5,h6", "font-family:" + family(t.headingFont));
  if (!t.codeFont.isEmpty()) rule("code,pre", "font-family:" + family(t.codeFont));
  if (!t.mathFont.isEmpty()) rule(".MathJax", "font-family:" + family(t.mathFont));
  if (t.mathSizePt > 0) rule(".MathJax", "font-size:" + number(t.mathSizePt * 96.0 / 72.0) + "px");
  if (t.codeLetterSpacing) rule("code,pre", "letter-spacing:" + number(t.codeLetterSpacing) + "px");
  paint("code", "color", t.inlineCodeTextColor);
  paint("del", "color", t.delColor);
  rule("a", QStringLiteral("text-decoration:") + (t.linkUnderlined ? "underline" : "none"));
  rule("code", "padding:" + number(t.inlineCodePaddingV) + "px " + number(t.inlineCodePaddingH) +
                   "px;border-radius:" + number(t.inlineCodeBorderRadius) + "px");
  if (t.inlineCodeBorderWidth > 0 && c.codeBorder.isValid())
    rule("code", "border:" + number(t.inlineCodeBorderWidth) + "px solid " + color(c.codeBorder));
  if (t.inlineCodeShadowColor.isValid())
    rule("code", "box-shadow:" + number(t.inlineCodeShadowOffsetX) + "px " + number(t.inlineCodeShadowOffsetY) + "px " +
                     number(t.inlineCodeShadowBlur) + "px " + number(t.inlineCodeShadowSpread) + "px " + color(t.inlineCodeShadowColor));
  if (!t.kbdFont.isEmpty()) rule("kbd", "font-family:" + family(t.kbdFont));
  paint("kbd", "background", t.kbdBackground);
  paint("kbd", "color", t.kbdTextColor);
  if (t.kbdPaddingH || t.kbdPaddingV) rule("kbd", "padding:" + number(t.kbdPaddingV) + "px " + number(t.kbdPaddingH) + "px");
  if (t.kbdBorderWidth > 0 && t.kbdBorderColor.isValid())
    rule("kbd", "border:" + number(t.kbdBorderWidth) + "px solid " + color(t.kbdBorderColor));
  if (t.kbdBorderRadius > 0) rule("kbd", "border-radius:" + number(t.kbdBorderRadius) + "px");
  if (t.kbdBorderBottomWidth > 0) rule("kbd", "border-bottom-width:" + number(t.kbdBorderBottomWidth) + "px");
  paint("kbd", "border-bottom-color", t.kbdBorderBottomColor);
  if (t.kbdShadowColor.isValid()) rule("kbd", "box-shadow:0 2px 0 " + color(t.kbdShadowColor));
  for (int i = 0; i < 6; ++i) {
    const auto host = QStringLiteral("h%1").arg(i + 1);
    if (t.headingSizePt[i] > 0) rule(host, "font-size:" + number((t.headingSizePt[i] * 96.0 / 72.0)) + "px");
    if (t.headingLineHeight[i] > 0) rule(host, "line-height:" + number(t.headingLineHeight[i]));
    if (t.headingFontWeightSet[i]) rule(host, "font-weight:" + QString::number(t.headingFontWeight[i]));
    if (t.headingItalicSet[i]) rule(host, QStringLiteral("font-style:") + (t.headingItalic[i] ? "italic" : "normal"));
    if (t.headingAlignment[i]) rule(host, "text-align:" + alignment(t.headingAlignment[i]));
    paint(host, "color", t.headingColor[i]);
  }
  if (input.spacing.codeBlockBoxThemed)
    rule("pre",
         "padding:" + margins(input.spacing.codeBlockPadding) + ";border-radius:" + number(input.spacing.codeBlockBorderRadius) + "px");
  if (input.spacing.tableBoxThemed) rule("th,td", "padding:" + margins(input.spacing.tableCellPadding));
  if (input.spacing.tableBorderRadius > 0) rule("table", "border-radius:" + number(input.spacing.tableBorderRadius) + "px");
  if (!input.spacing.codeBlockMargin.isNull()) rule("pre", "margin:" + margins(input.spacing.codeBlockMargin));
  if (!input.spacing.tableMargin.isNull()) rule("table", "margin:" + margins(input.spacing.tableMargin));
  if (!input.spacing.listMargin.isNull()) rule("ul,ol", "margin:" + margins(input.spacing.listMargin));
  const auto& page = input.page;
  if (page.pageMaxWidth > 0) rule("#write", "max-width:" + number(page.pageMaxWidth) + "px");
  if (!page.pagePadding.isNull()) rule("#write", "padding:" + margins(page.pagePadding));
  if (page.pageMarginExplicit) rule("#write", "margin:" + margins(page.pageMargin));
  rule("#write", QStringLiteral("box-sizing:") + (page.borderBox ? "border-box" : "content-box"));
  if (page.pageBorderWidth > 0 && page.pageBorderColor.isValid())
    rule("#write", "border:" + number(page.pageBorderWidth) + "px solid " + color(page.pageBorderColor));
  if (page.pageBorderRadius > 0) rule("#write", "border-radius:" + number(page.pageBorderRadius) + "px");
  if (page.pageShadowColor.isValid())
    rule("#write", "box-shadow:" + number(page.pageShadowOffsetX) + "px " + number(page.pageShadowOffsetY) + "px " +
                       number(page.pageShadowBlur) + "px " + number(page.pageShadowSpread) + "px " + color(page.pageShadowColor));
  paint("body", "background", page.viewportBackground);
  paint("#write", "background", page.pageBackground);
  for (const auto& style : input.elementStyles) {
    const auto& b = style.box;
    const auto& text = style.text;
    if (b.paddingSpecified || !b.padding.isNull()) rule(style.key, "padding:" + margins(b.padding));
    if (b.marginSpecified) rule(style.key, "margin:" + margins(b.margin));
    if (b.borderTopWidth || b.borderRightWidth || b.borderBottomWidth || b.borderLeftWidth) {
      const qreal widths[] = {b.borderTopWidth, b.borderRightWidth, b.borderBottomWidth, b.borderLeftWidth};
      const QColor colors[] = {b.borderTopColor, b.borderRightColor, b.borderBottomColor, b.borderLeftColor};
      const QStringList sides = {"top", "right", "bottom", "left"};
      for (int i = 0; i < 4; ++i)
        rule(style.key,
             "border-" + sides[i] + ":" + number(widths[i]) + "px solid " + (colors[i].isValid() ? color(colors[i]) : "currentColor"));
    }
    if (b.borderRadius > 0) rule(style.key, "border-radius:" + number(b.borderRadius) + "px");
    if (b.widthFitContent) rule(style.key, "width:fit-content");
    paint(style.key, "color", style.paint.color);
    paint(style.key, "background", style.paint.backgroundColor);
    if (!text.fontFamily.isEmpty()) rule(style.key, "font-family:" + family(text.fontFamily));
    if (text.fontSizeSet || text.fontSizePx > 0) rule(style.key, "font-size:" + number(text.fontSizePx) + "px");
    if (text.lineHeight > 0) rule(style.key, "line-height:" + number(text.lineHeight));
    if (text.fontWeightSet) rule(style.key, "font-weight:" + QString::number(text.fontWeight));
    if (text.italicSet) rule(style.key, QStringLiteral("font-style:") + (text.italic ? "italic" : "normal"));
  }
  auto result = CssThemeMapper::fromCss(css, input.id, {});
  result.label = input.label;
  result.isBuiltIn = input.isBuiltIn;
  result.fontAliases = input.fontAliases;
  result.spacing.listMarkerGap = input.spacing.listMarkerGap;
  if (!input.decorations.pseudos.empty()) result.decorations = input.decorations;
  return result;
}
}  // namespace muffin
