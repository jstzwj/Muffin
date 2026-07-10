#include "theme/RenderTheme.h"

#include "document/MarkdownNode.h"
#include "theme/CssComputedStyleEngine.h"
#include "theme/CssThemeMapper.h"
#include "theme/NodeCssElement.h"

#include <QFontDatabase>
#include <QStringList>
#include <QtGlobal>

#include <initializer_list>

namespace muffin {
namespace {

QString firstAvailableFontFamily(std::initializer_list<QString> candidates) {
  const QStringList availableFamilies = QFontDatabase::families();
  for (const QString& candidate : candidates) {
    for (const QString& family : availableFamilies) {
      if (family.compare(candidate, Qt::CaseInsensitive) == 0) {
        return family;
      }
    }
  }
  const QString systemFamily = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
  return systemFamily.isEmpty() ? QStringLiteral("sans-serif") : systemFamily;
}

bool familyAvailable(const QString& family, const QStringList& availableFamilies) {
  for (const QString& available : availableFamilies) {
    if (available.compare(family, Qt::CaseInsensitive) == 0) {
      return true;
    }
  }
  return false;
}

// Resolved per-platform fallback families, each cached on first use. Used both as
// the legacy default (when a theme supplies no font) and as the substitution tail
// appended after a theme-supplied family so missing glyphs (CJK, symbols) resolve.
const QString& sansFamily() {
  static const QString f = firstAvailableFontFamily({
#if defined(Q_OS_WIN)
      QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Segoe UI"), QStringLiteral("Arial"),
#elif defined(Q_OS_MACOS)
      QStringLiteral("PingFang SC"), QStringLiteral("Hiragino Sans GB"), QStringLiteral("Helvetica Neue"),
      QStringLiteral("Arial"),
#else
      QStringLiteral("Noto Sans CJK SC"), QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans"),
      QStringLiteral("Arial"),
#endif
  });
  return f;
}
const QString& serifFamily() {
  static const QString f = firstAvailableFontFamily({
#if defined(Q_OS_WIN)
      QStringLiteral("Georgia"), QStringLiteral("Cambria"), QStringLiteral("Times New Roman"),
#elif defined(Q_OS_MACOS)
      QStringLiteral("New York"), QStringLiteral("Times New Roman"), QStringLiteral("Georgia"),
#else
      QStringLiteral("Noto Serif"), QStringLiteral("DejaVu Serif"), QStringLiteral("Times New Roman"),
#endif
      QStringLiteral("serif"),
  });
  return f;
}
const QString& codeFamily() {
  static const QString f = firstAvailableFontFamily({
#if defined(Q_OS_WIN)
      QStringLiteral("Lucida Console"), QStringLiteral("Consolas"), QStringLiteral("Courier"),
#elif defined(Q_OS_MACOS)
      QStringLiteral("Menlo"), QStringLiteral("Monaco"), QStringLiteral("Courier New"),
#else
      QStringLiteral("DejaVu Sans Mono"), QStringLiteral("Noto Sans Mono"), QStringLiteral("Liberation Mono"),
#endif
      QStringLiteral("monospace"),
  });
  return f;
}
QString genericFamilyTail(const QString& generic) {
  const QString lower = generic.toLower();
  if (lower == QStringLiteral("serif")) { return serifFamily(); }
  if (lower == QStringLiteral("monospace")) { return codeFamily(); }
  return sansFamily();
}
QStringList themeFamilyList(const QString& raw, const QString& platformTail) {
  QStringList requested = raw.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  for (QString& f : requested) { f = f.trimmed(); }
  requested.removeAll(QString());

  QString genericTail;
  if (!requested.isEmpty()) {
    const QString last = requested.last().toLower();
    if (last == QStringLiteral("serif") || last == QStringLiteral("sans-serif") || last == QStringLiteral("monospace")) {
      genericTail = genericFamilyTail(requested.takeLast());
    }
  }

  const QStringList availableFamilies = QFontDatabase::families();
  QStringList out;
  for (const QString& family : requested) {
    // @font-face: a CSS theme may declare `font-family: CascadiaCode` while the
    // font file's internal name (what QFontDatabase registers) is "Cascadia Code",
    // or `"LXGW WenKai"` whose internal name is 霞鹜文楷. Substitute the declared
    // alias with the registered name so the stack resolves to the bundled font.
    QString resolved = family;
    if (const QString alias = ThemeDefinition::fontFamilyAlias(family); !alias.isEmpty()) {
      resolved = alias;
    }
    if (familyAvailable(resolved, availableFamilies) && !out.contains(resolved, Qt::CaseInsensitive)) {
      out << resolved;
    }
  }
  if (!genericTail.isEmpty() && !out.contains(genericTail, Qt::CaseInsensitive)) { out << genericTail; }
  if (!platformTail.isEmpty() && !out.contains(platformTail, Qt::CaseInsensitive)) { out << platformTail; }
  if (out.isEmpty()) { out = requested; }
  return out;
}

const QString& mathFamily() {
  static const QString f = firstAvailableFontFamily({
#if defined(Q_OS_WIN)
      QStringLiteral("Cambria Math"), QStringLiteral("Segoe UI Symbol"),
#elif defined(Q_OS_MACOS)
      QStringLiteral("STIX Two Math"), QStringLiteral("STIXGeneral"), QStringLiteral("Apple Symbols"),
#else
      QStringLiteral("STIX Two Math"), QStringLiteral("Latin Modern Math"),
      QStringLiteral("DejaVu Math TeX Gyre"),
#endif
  });
  return f;
}

}  // namespace

RenderTheme RenderTheme::defaultTheme(int zoomPercent) {
  return github(zoomPercent);
}

RenderTheme RenderTheme::fromDefinition(const ThemeDefinition& definition, int zoomPercent, int fontSizePx) {
  const ThemeColors& c = definition.colors;
  RenderTheme t;
  t.backgroundColor_ = c.background;
  t.textColor_ = c.text;
  t.mutedTextColor_ = c.muted;
  t.linkColor_ = c.link;
  t.codeBackgroundColor_ = c.codeBackground;
  t.highlightBackgroundColor_ = c.highlight;
  t.codeBorderColor_ = c.codeBorder;
  t.quoteBorderColor_ = c.quoteBorder;
  t.tableBorderColor_ = c.tableBorder;
  t.tableHeaderBackgroundColor_ = c.tableHeaderBackground;
  t.tableAlternateBackgroundColor_ = c.tableAlternateBackground;
  t.selectionColor_ = c.selection;
  // Spell-check red isn't part of the theme JSON — pick the readable variant for
  // the page tone (matches the night() factory's lighter red on dark pages).
  t.spellCheckColor_ = c.isDark ? QColor(QStringLiteral("#ff6a6a")) : QColor(QStringLiteral("#d1242f"));
  t.serifBody_ = c.serifBody;
  // Typography + P5 colours from the theme (CSS themes). Built-in themes
  // leave these empty/invalid so the per-platform fonts and legacy colours win.
  const ThemeTypography& ty = definition.typography;
  t.bodyFont_ = ty.bodyFont;
  t.headingFont_ = ty.headingFont;
  t.codeFont_ = ty.codeFont;
  t.mathFont_ = ty.mathFont;
  t.bodySizePt_ = ty.bodySizePt;
  t.lineHeight_ = ty.lineHeight;
  t.letterSpacing_ = ty.letterSpacing;
  t.codeLetterSpacing_ = ty.codeLetterSpacing;
  t.linkUnderlined_ = ty.linkUnderlined;
  t.linkUnderlineStyle_ = ty.linkUnderlineStyle;
  t.linkUnderlineColor_ = ty.linkUnderlineColor;
  t.linkOverline_ = ty.linkOverline;
  t.inlineCodePaddingH_ = ty.inlineCodePaddingH;
  t.inlineCodePaddingV_ = ty.inlineCodePaddingV;
  t.inlineCodeBorderRadius_ = ty.inlineCodeBorderRadius;
  t.inlineCodeBorderWidth_ = ty.inlineCodeBorderWidth;
  t.inlineCodeTextColor_ = ty.inlineCodeTextColor;
  t.delColor_ = ty.delColor;
  t.kbdBackground_ = ty.kbdBackground;
  t.kbdTextColor_ = ty.kbdTextColor;
  t.kbdFont_ = ty.kbdFont;
  t.kbdPaddingH_ = ty.kbdPaddingH;
  t.kbdPaddingV_ = ty.kbdPaddingV;
  t.kbdBorderRadius_ = ty.kbdBorderRadius;
  t.kbdBorderColor_ = ty.kbdBorderColor;
  t.kbdBorderWidth_ = ty.kbdBorderWidth;
  t.kbdBorderBottomWidth_ = ty.kbdBorderBottomWidth;
  t.kbdBorderBottomColor_ = ty.kbdBorderBottomColor;
  t.kbdShadowColor_ = ty.kbdShadowColor;
  t.bodyAlignment_ = ty.bodyAlignment;
  for (int i = 0; i < 6; ++i) {
    t.headingSizePt_[i] = ty.headingSizePt[i];
    t.headingLineHeight_[i] = ty.headingLineHeight[i];
    t.headingColor_[i] = ty.headingColor[i];
    t.headingAlignment_[i] = ty.headingAlignment[i];
    t.headingFontWeight_[i] = ty.headingFontWeight[i];
    t.headingFontWeightSet_[i] = ty.headingFontWeightSet[i];
    t.headingItalic_[i] = ty.headingItalic[i];
    t.headingItalicSet_[i] = ty.headingItalicSet[i];
  }
  t.viewportBackgroundColor_ = definition.page.viewportBackground;
  t.pageBackgroundColor_ = definition.page.pageBackground;
  t.pageBorderColor_ = definition.page.pageBorderColor;
  t.pageBorderWidth_ = definition.page.pageBorderWidth;
  t.pageBorderRadius_ = definition.page.pageBorderRadius;
  t.pagePadding_ = definition.page.pagePadding;
  t.pageMargin_ = definition.page.pageMargin;
  t.pageMarginExplicit_ = definition.page.pageMarginExplicit;
  t.pageMaxWidth_ = definition.page.pageMaxWidth;
  t.pageShadowColor_ = definition.page.pageShadowColor;
  t.pageShadowBlur_ = definition.page.pageShadowBlur;
  t.pageShadowOffsetY_ = definition.page.pageShadowOffsetY;
  t.decorations_ = definition.decorations;
  t.elementStyles_ = definition.elementStyles;
  // Build the key→index lookup that elementStyle() queries on every paint. Iterate in
  // reverse so a duplicate key keeps its FIRST occurrence (matching the old linear scan).
  t.elementStyleIndex_.reserve(static_cast<int>(t.elementStyles_.size()));
  for (qsizetype i = static_cast<qsizetype>(t.elementStyles_.size()) - 1; i >= 0; --i) {
    t.elementStyleIndex_[t.elementStyles_[i].key] = i;
  }
  for (const ThemeElementStyle& style : t.elementStyles_) {
    if (style.key == QStringLiteral("li::marker") && style.paint.color.isValid()) {
      t.listMarkerColor_ = style.paint.color;
      break;
    }
  }
  t.hasStructuralRules_ = definition.hasStructuralRules;
  t.hasNthOfType_ = definition.hasNthOfType;
  t.bodyFontPx_ = definition.bodyFontPx;
  t.structuralSheet_ = definition.structuralSheet;
  if (t.structuralSheet_) {
    t.structuralEngine_ = std::make_shared<CssComputedStyleEngine>(*t.structuralSheet_);
  }
  t.codeBlockPadding_ = definition.spacing.codeBlockPadding;
  t.codeBlockBorderRadius_ = definition.spacing.codeBlockBorderRadius;
  t.codeBlockBoxThemed_ = definition.spacing.codeBlockBoxThemed;
  t.tableCellPadding_ = definition.spacing.tableCellPadding;
  t.tableBorderRadius_ = definition.spacing.tableBorderRadius;
  t.tableBoxThemed_ = definition.spacing.tableBoxThemed;
  t.codeBlockMargin_ = definition.spacing.codeBlockMargin;
  t.tableMargin_ = definition.spacing.tableMargin;
  t.listMargin_ = definition.spacing.listMargin;
  t.listMarkerGap_ = definition.spacing.listMarkerGap;
  t.ulListStyleType_ = definition.spacing.ulListStyleType;
  t.olListStyleType_ = definition.spacing.olListStyleType;
  t.liListStyleType_ = definition.spacing.liListStyleType;
  for (int i = 0; i < 6; ++i) {
    t.headingBeforeAdvance_[i] = definition.spacing.headingBeforeAdvance[i];
  }
  t.codeBlockBackground_ = c.codeBlockBackground;
  t.headingAccentColor_ = c.headingAccentColor;
  t.blockquoteBackground_ = c.blockquoteBackground;
  t.setZoomPercent(zoomPercent);
  t.setFontSizePx(fontSizePx);
  return t;
}

namespace {
// Built-in themes are authored as CSS at :/themes/<id>.css and loaded through
// ThemeDefinition::builtIns(). Each factory re-loads that SAME definition, so the
// factory and the CSS-loaded theme can never drift apart — no hand-synced colour
// table to maintain, and every token (borders, table headers, …) stays aligned
// automatically. Falls back to a plain default if the resource is missing.
RenderTheme loadBuiltInTheme(const QString& id, int zoomPercent) {
  const std::optional<ThemeDefinition> def = ThemeDefinition::builtIn(id);
  if (def) { return RenderTheme::fromDefinition(*def, zoomPercent); }
  RenderTheme theme;
  theme.setZoomPercent(zoomPercent);
  return theme;
}
}  // namespace

RenderTheme RenderTheme::github(int zoomPercent) { return loadBuiltInTheme(QStringLiteral("github"), zoomPercent); }
RenderTheme RenderTheme::newsprint(int zoomPercent) { return loadBuiltInTheme(QStringLiteral("newsprint"), zoomPercent); }
RenderTheme RenderTheme::night(int zoomPercent) { return loadBuiltInTheme(QStringLiteral("night"), zoomPercent); }
RenderTheme RenderTheme::pixyll(int zoomPercent) { return loadBuiltInTheme(QStringLiteral("pixyll"), zoomPercent); }
RenderTheme RenderTheme::whitey(int zoomPercent) { return loadBuiltInTheme(QStringLiteral("whitey"), zoomPercent); }

int RenderTheme::zoomPercent() const {
  return zoomPercent_;
}

void RenderTheme::setZoomPercent(int percent) {
  zoomPercent_ = qBound(60, percent, 200);
}

int RenderTheme::fontSizePx() const {
  return fontSizePx_;
}

void RenderTheme::setFontSizePx(int px) {
  fontSizePx_ = qBound(12, px, 24);
}

qreal RenderTheme::pageWidth() const {
  return scaled(pageMaxWidth_ > 0.0 ? pageMaxWidth_ : 860.0);
}

qreal RenderTheme::topMargin() const {
  return scaled(30.0);
}

qreal RenderTheme::bottomMargin() const {
  return scaled(70.0);
}

qreal RenderTheme::blockSpacing() const {
  return scaled(11.0);
}

qreal RenderTheme::listIndent() const {
  // List indent comes from the `ul` element style's left padding; themes that set
  // none (JSON themes, or CSS without `ul` padding) fall back to 30px — the legacy
  // default that keeps built-in list spacing intact.
  if (const ThemeElementStyle* style = elementStyle(QStringLiteral("ul"))) {
    if (style->box.padding.left() > 0.0) { return scaled(style->box.padding.left()); }
  }
  return scaled(30.0);
}

qreal RenderTheme::listMarkerGap() const {
  // An explicit theme override wins. Otherwise "auto": keep the legacy
  // proportional gap (0.2 × indent, already zoomed via listIndent()) BUT floor
  // it so small-indent themes don't collapse the marker onto the text. The floor
  // is small enough that large-indent themes (e.g. github's 30px → 6px) are
  // visually unchanged.
  if (listMarkerGap_ > 0.0) { return scaled(listMarkerGap_); }
  constexpr qreal kMarkerGapFloor = 4.5;
  return qMax(listIndent() * 0.2, scaled(kMarkerGapFloor));
}

QString RenderTheme::listStyleTypeForItem(bool ordered) const {
  // Ordered items take `ol`'s type, bullet items `ul`'s; a direct `li` declaration
  // is the fallback (covers `li { list-style-type }` with no per-list rule).
  const QString type = ordered ? olListStyleType_ : ulListStyleType_;
  return !type.isEmpty() ? type : liListStyleType_;
}

ListGuide RenderTheme::listGuide() const {
  // Scale the CSS-px geometry to the current zoom; colour and `present` pass through.
  ListGuide g = decorations_.listGuide;
  g.width = scaled(g.width);
  g.leftOffset = scaled(g.leftOffset);
  g.topInset = scaled(g.topInset);
  g.bottomInset = scaled(g.bottomInset);
  return g;
}

qreal RenderTheme::blockQuoteIndent() const {
  return scaled(16.0);
}

QColor RenderTheme::viewportBackgroundColor() const {
  return viewportBackgroundColor_.isValid() ? viewportBackgroundColor_ : backgroundColor_;
}

QColor RenderTheme::pageBackgroundColor() const {
  return pageBackgroundColor_.isValid() ? pageBackgroundColor_ : backgroundColor_;
}

QColor RenderTheme::pageBorderColor() const {
  return pageBorderColor_;
}

qreal RenderTheme::pageBorderWidth() const {
  return scaled(pageBorderWidth_);
}

qreal RenderTheme::pageBorderRadius() const {
  return scaled(pageBorderRadius_);
}

QMarginsF RenderTheme::pagePadding() const {
  return QMarginsF(scaled(pagePadding_.left()), scaled(pagePadding_.top()), scaled(pagePadding_.right()), scaled(pagePadding_.bottom()));
}

QMarginsF RenderTheme::pageMargin() const {
  // An explicit #write margin (even `margin: 0 auto`, which parses to an
  // all-zero QMarginsF) must be honoured as-is — it is NOT the same as "the
  // theme specified no margin", which is the only case that should fall back to
  // the legacy flat-document 30/70 inset. pageMarginExplicit_ is the flag that
  // separates those two null-QMarginsF cases.
  if (!pageMarginExplicit_) {
    return QMarginsF(0, topMargin(), 0, bottomMargin());
  }
  return QMarginsF(scaled(pageMargin_.left()), scaled(pageMargin_.top()), scaled(pageMargin_.right()), scaled(pageMargin_.bottom()));
}

QColor RenderTheme::pageShadowColor() const { return pageShadowColor_; }
qreal RenderTheme::pageShadowBlur() const { return scaled(pageShadowBlur_); }
qreal RenderTheme::pageShadowOffsetY() const { return scaled(pageShadowOffsetY_); }

QMarginsF RenderTheme::blockMargin(BlockType type, int headingLevel, const MarkdownNode* node) const {
  // Element box geometry for p / h1-h6 / blockquote / list comes from elementStyles;
  // pre/table (no element style) keep their legacy block-flow margins. When `node`
  // is supplied and the theme uses structural selectors, the node's live position
  // is matched first (so `li:first-child`, `p + p`, … override the base margin).
  QString key;
  QMarginsF m;  // null unless a block-flow margin or an element style sets it
  switch (type) {
    case BlockType::Heading:      key = QStringLiteral("h%1").arg(headingLevel); break;
    case BlockType::Paragraph:    key = QStringLiteral("p"); break;
    case BlockType::BlockQuote:   key = QStringLiteral("blockquote"); break;
    case BlockType::CodeFence:
    case BlockType::FrontMatter:  key = QStringLiteral("pre"); m = codeBlockMargin_; break;
    case BlockType::Table:        key = QStringLiteral("table"); m = tableMargin_; break;
    case BlockType::List:         key = QStringLiteral("ul"); m = listMargin_; break;
    default: break;
  }
  if (!key.isEmpty()) {
    const ThemeElementStyle* style = node ? elementStyleForNode(*node, key) : elementStyle(key);
    if (style) {
      if (style->box.present && !style->box.margin.isNull()) { m = style->box.margin; }
    }
  }
  return QMarginsF(scaled(m.left()), scaled(m.top()), scaled(m.right()), scaled(m.bottom()));
}

QMarginsF RenderTheme::headingPadding(int level) const {
  if (const ThemeElementStyle* style = elementStyle(QStringLiteral("h%1").arg(level))) {
    if (!style->box.padding.isNull()) {
      const QMarginsF& p = style->box.padding;
      return QMarginsF(scaled(p.left()), scaled(p.top()), scaled(p.right()), scaled(p.bottom()));
    }
  }
  return QMarginsF();  // no CSS padding → flush (the legacy default was null too)
}

bool RenderTheme::blockquoteBoxThemed() const {
  // Flips to the CSS box only when the author styled padding/border/radius — NOT
  // margin alone (that draws no box). Derived from elementStyles so it can't drift
  // from the unified per-side box painter.
  if (const ThemeElementStyle* style = elementStyle(QStringLiteral("blockquote"))) {
    const ThemeElementBoxStyle& b = style->box;
    return !b.padding.isNull() || b.borderTopWidth > 0.0 || b.borderRightWidth > 0.0 ||
           b.borderBottomWidth > 0.0 || b.borderLeftWidth > 0.0 || b.borderRadius > 0.0 ||
           style->paint.backgroundColor.isValid();
  }
  return false;
}
QMarginsF RenderTheme::blockquotePadding() const {
  if (const ThemeElementStyle* style = elementStyle(QStringLiteral("blockquote"))) {
    if (!style->box.padding.isNull()) {
      const QMarginsF& p = style->box.padding;
      return QMarginsF(scaled(p.left()), scaled(p.top()), scaled(p.right()), scaled(p.bottom()));
    }
  }
  return QMarginsF();
}
qreal RenderTheme::blockquoteBorderWidth() const {
  if (const ThemeElementStyle* style = elementStyle(QStringLiteral("blockquote"))) {
    if (style->box.borderLeftWidth > 0.0) { return scaled(style->box.borderLeftWidth); }
  }
  return 0.0;
}
QColor RenderTheme::blockquoteBorderColor() const {
  if (const ThemeElementStyle* style = elementStyle(QStringLiteral("blockquote"))) {
    return style->box.borderLeftColor;  // invalid when unset
  }
  return QColor();
}
qreal RenderTheme::blockquoteBorderRadius() const {
  if (const ThemeElementStyle* style = elementStyle(QStringLiteral("blockquote"))) {
    if (style->box.borderRadius > 0.0) { return scaled(style->box.borderRadius); }
  }
  return 0.0;
}

bool RenderTheme::codeBlockBoxThemed() const { return codeBlockBoxThemed_; }
qreal RenderTheme::codeBlockBorderRadius() const { return scaled(codeBlockBorderRadius_); }

bool RenderTheme::tableBoxThemed() const { return tableBoxThemed_; }
qreal RenderTheme::tableBorderRadius() const { return scaled(tableBorderRadius_); }

QColor RenderTheme::headingBorderBottomColor(int level) const {
  if (const ThemeElementStyle* style = elementStyle(QStringLiteral("h%1").arg(level))) {
    return style->box.borderBottomColor;  // invalid when unset
  }
  return QColor();
}
qreal RenderTheme::headingBorderBottomWidth(int level) const {
  if (const ThemeElementStyle* style = elementStyle(QStringLiteral("h%1").arg(level))) {
    if (style->box.borderBottomWidth > 0.0) { return scaled(style->box.borderBottomWidth); }
  }
  return 0.0;
}
QColor RenderTheme::headingBorderLeftColor(int level) const {
  if (const ThemeElementStyle* style = elementStyle(QStringLiteral("h%1").arg(level))) {
    return style->box.borderLeftColor;  // invalid when unset
  }
  return QColor();
}
qreal RenderTheme::headingBorderLeftWidth(int level) const {
  if (const ThemeElementStyle* style = elementStyle(QStringLiteral("h%1").arg(level))) {
    if (style->box.borderLeftWidth > 0.0) { return scaled(style->box.borderLeftWidth); }
  }
  return 0.0;
}

bool RenderTheme::headingFitContent(int level) const {
  if (const ThemeElementStyle* style = elementStyle(QStringLiteral("h%1").arg(level))) {
    return style->box.widthFitContent;
  }
  return false;
}

qreal RenderTheme::headingBeforeAdvance(int level) const {
  return scaled(headingBeforeAdvance_[qBound(0, level - 1, 5)]);
}

qreal RenderTheme::lineHeightMultiplier(BlockType type, int headingLevel) const {
  if (type == BlockType::Heading) {
    const qreal v = headingLineHeight_[qBound(0, headingLevel - 1, 5)];
    if (v > 0.0) { return v; }
  }
  return lineHeight_;
}

Qt::Alignment RenderTheme::textAlignment(BlockType type, int headingLevel) const {
  if (type == BlockType::Heading) {
    const Qt::Alignment heading = headingAlignment_[qBound(0, headingLevel - 1, 5)];
    if (heading != Qt::Alignment()) { return heading; }
  }
  return bodyAlignment_;
}

QFont RenderTheme::paragraphFont() const {
  const QString& platform = serifBody_ ? serifFamily() : sansFamily();
  QFont font;
  if (!bodyFont_.isEmpty()) {
    // Theme font primary, platform family as substitution tail so missing glyphs
    // (CJK, symbols) still resolve.
    font.setFamilies(themeFamilyList(bodyFont_, platform));
  } else {
    font.setFamily(platform);
  }
  font.setStyleStrategy(QFont::PreferDefault);
  font.setPointSizeF(scaledFont(bodySizePt_ > 0.0 ? bodySizePt_ : 12.0));
  // Phase 3: CSS letter-spacing (body + headings inherit this font). Qt's text
  // engine honours it for advance/measure, so layout, the lazy estimate, paint
  // and hit-test all stay consistent. Zoom-scaled; 0 → untouched (built-ins).
  if (letterSpacing_ > 0.0) {
    font.setLetterSpacing(QFont::AbsoluteSpacing, scaled(letterSpacing_));
  }
  return font;
}

QFont RenderTheme::textFontForElement(const QString& key, const MarkdownNode* node) const {
  // Prototype path (no live node — the Lazy estimate loop): the result depends only on the theme's
  // prototype style + zoom/fontSize, fixed between rebuilds, so cache it per key. Without this every
  // estimated paragraph built two fresh QFonts (font + line-height), ~80µs each on Windows — the
  // dominant per-block cost that made a 112k-paragraph Lazy rebuild take ~21s.
  if (!node) {
    const auto it = prototypeFontCache_.constFind(key);
    if (it != prototypeFontCache_.constEnd()) { return it.value(); }
  }
  QFont font = paragraphFont();
  const ThemeElementStyle* style = node ? elementStyleForNode(*node, key) : elementStyle(key);
  if (!style) { return font; }
  const QString& platform = serifBody_ ? serifFamily() : sansFamily();
  if (!style->text.fontFamily.isEmpty()) { font.setFamilies(themeFamilyList(style->text.fontFamily, platform)); }
  if (style->text.fontSizePx > 0.0) { font.setPointSizeF(scaledFont(style->text.fontSizePx * 72.0 / 96.0)); }
  if (style->text.fontWeightSet) {
    font.setWeight(static_cast<QFont::Weight>(qBound(static_cast<int>(QFont::Thin), style->text.fontWeight, static_cast<int>(QFont::Black))));
  }
  if (style->text.italicSet) { font.setItalic(style->text.italic); }
  if (!node) { prototypeFontCache_.insert(key, font); }
  return font;
}

QColor RenderTheme::textColorForElement(const QString& key, const MarkdownNode* node) const {
  if (const ThemeElementStyle* style = node ? elementStyleForNode(*node, key) : elementStyle(key)) {
    if (style->paint.color.isValid()) { return style->paint.color; }
  }
  return textColor_;
}

qreal RenderTheme::lineHeightMultiplierForElement(const QString& key, BlockType fallbackType, int headingLevel, const MarkdownNode* node) const {
  if (const ThemeElementStyle* style = node ? elementStyleForNode(*node, key) : elementStyle(key)) {
    if (style->text.lineHeight > 0.0) { return style->text.lineHeight; }
  }
  return lineHeightMultiplier(fallbackType, headingLevel);
}

qreal RenderTheme::wordSpacingForElement(const QString& key, const MarkdownNode* node) const {
  if (const ThemeElementStyle* style = node ? elementStyleForNode(*node, key) : elementStyle(key)) {
    if (style->text.wordSpacing != 0.0) { return scaled(style->text.wordSpacing); }
  }
  return 0.0;
}

Qt::Alignment RenderTheme::textAlignmentForElement(const QString& key, BlockType fallbackType, int headingLevel, const MarkdownNode* node) const {
  if (const ThemeElementStyle* style = node ? elementStyleForNode(*node, key) : elementStyle(key)) {
    if (style->text.alignment != Qt::Alignment()) { return style->text.alignment; }
  }
  return textAlignment(fallbackType, headingLevel);
}

int RenderTheme::textTransformForElement(const QString& key, const MarkdownNode* node) const {
  if (const ThemeElementStyle* style = node ? elementStyleForNode(*node, key) : elementStyle(key)) {
    return style->text.textTransform;
  }
  return 0;
}

TextShadow RenderTheme::textShadowForElement(const QString& key, const MarkdownNode* node) const {
  if (const ThemeElementStyle* style = node ? elementStyleForNode(*node, key) : elementStyle(key)) {
    return style->text.textShadow;
  }
  return TextShadow{};
}

const ThemeElementStyle* RenderTheme::elementStyleForNode(const MarkdownNode& node, const QString& key) const {
  if (!hasStructuralRules_ || !structuralEngine_) { return elementStyle(key); }
  const auto it = nodeStyleCache_.constFind(node.id());
  if (it != nodeStyleCache_.constEnd()) { return &it.value(); }
  // Reuse one sparse adapter across all queries in this rebuild. It materializes only
  // nodes reached by selector navigation, while final styles remain cached by NodeId.
  if (!structuralBuilder_) { structuralBuilder_ = std::make_shared<NodeCssElementBuilder>(hasNthOfType_); }
  ThemeElementStyle resolved = CssThemeMapper::elementStyleForNode(*structuralBuilder_, *structuralEngine_, node, key, bodyFontPx_);
  resolved.key = key;
  return &nodeStyleCache_.insert(node.id(), std::move(resolved)).value();
}

void RenderTheme::clearStructuralCache() const {
  nodeStyleCache_.clear();
  prototypeFontCache_.clear();
  // The adapter views live MarkdownNodes. Rebuilding it is cheap now that it is sparse,
  // and guarantees edits/deletions cannot leave copied tags or :has results behind.
  structuralBuilder_.reset();
}

void RenderTheme::dropStructuralBuilder() const {
  structuralBuilder_.reset();
}

void RenderTheme::invalidateStructuralSiblingLinks() const {
  structuralBuilder_.reset();
}

QFont RenderTheme::headingFont(int level) const {
  static constexpr qreal sizes[] = {24.0, 19.0, 16.0, 14.0, 12.5, 12.0};
  QFont font = paragraphFont();
  const int idx = qBound(0, level - 1, 5);
  // Like the other element-text getters, the CSS computed style wins and the legacy
  // typography fields are the fallback for JSON / hand-built themes (no elementStyles).
  const ThemeElementStyle* style = elementStyle(QStringLiteral("h%1").arg(level));
  const auto applyWeight = [&](int w) {
    font.setWeight(static_cast<QFont::Weight>(qBound(static_cast<int>(QFont::Thin), w, static_cast<int>(QFont::Black))));
  };
  // font-weight: element-style → legacy heading weight → bold (built-in default).
  if (style && style->text.fontWeightSet) {
    applyWeight(style->text.fontWeight);
  } else if (headingFontWeightSet_[idx]) {
    applyWeight(headingFontWeight_[idx]);
  } else {
    font.setBold(true);
  }
  // italic: applied only when explicitly declared (element-style or legacy).
  if (style && style->text.italicSet) {
    font.setItalic(style->text.italic);
  } else if (headingItalicSet_[idx]) {
    font.setItalic(headingItalic_[idx]);
  }
  // font-size: element-style → legacy heading size → built-in table.
  const qreal elementPt = (style && style->text.fontSizePx > 0.0) ? style->text.fontSizePx * 72.0 / 96.0 : 0.0;
  const qreal sizePt = elementPt > 0.0 ? elementPt : (headingSizePt_[idx] > 0.0 ? headingSizePt_[idx] : sizes[idx]);
  font.setPointSizeF(scaledFont(sizePt));
  // font-family: element-style → legacy heading family.
  const QString family = (style && !style->text.fontFamily.isEmpty()) ? style->text.fontFamily : headingFont_;
  if (!family.isEmpty()) {
    font.setFamilies(themeFamilyList(family, serifBody_ ? serifFamily() : sansFamily()));
  }
  return font;
}

QFont RenderTheme::codeFont() const {
  QFont font;
  if (!codeFont_.isEmpty()) {
    font.setFamilies(themeFamilyList(codeFont_, codeFamily()));
  } else {
    font.setFamily(codeFamily());
  }
  font.setStyleHint(QFont::Monospace);
  font.setPointSizeF(scaledFont(10.8));
  if (codeLetterSpacing_ > 0.0) {
    font.setLetterSpacing(QFont::AbsoluteSpacing, scaled(codeLetterSpacing_));
  }
  return font;
}

qreal RenderTheme::codeLineHeight() const {
  return scaledFont(23.04);
}

QFont RenderTheme::mathFont() const {
  QFont font;
  if (!mathFont_.isEmpty()) {
    font.setFamilies(themeFamilyList(mathFont_, mathFamily()));
  } else {
    font.setFamily(mathFamily());
  }
  font.setPointSizeF(scaledFont(12.5));
  return font;
}

QColor RenderTheme::backgroundColor() const {
  return backgroundColor_;
}

QColor RenderTheme::textColor() const {
  return textColor_;
}

QColor RenderTheme::mutedTextColor() const {
  return mutedTextColor_;
}

QColor RenderTheme::linkColor() const {
  return linkColor_;
}

bool RenderTheme::linkUnderlined() const {
  return linkUnderlined_;
}
int RenderTheme::linkUnderlineStyle() const {
  return linkUnderlineStyle_;
}
QColor RenderTheme::linkUnderlineColor() const {
  return linkUnderlineColor_;
}
bool RenderTheme::linkOverline() const {
  return linkOverline_;
}

qreal RenderTheme::inlineCodePaddingH() const { return scaled(inlineCodePaddingH_); }
qreal RenderTheme::inlineCodePaddingV() const { return scaled(inlineCodePaddingV_); }
qreal RenderTheme::inlineCodeBorderRadius() const { return scaled(inlineCodeBorderRadius_); }
qreal RenderTheme::inlineCodeBorderWidth() const { return scaled(inlineCodeBorderWidth_); }
QColor RenderTheme::inlineCodeTextColor() const { return inlineCodeTextColor_; }
QColor RenderTheme::delColor() const { return delColor_; }
QColor RenderTheme::kbdBackgroundColor() const { return kbdBackground_; }
QColor RenderTheme::kbdTextColor() const { return kbdTextColor_; }
QString RenderTheme::kbdFont() const { return kbdFont_; }
qreal RenderTheme::kbdPaddingH() const { return scaled(kbdPaddingH_); }
qreal RenderTheme::kbdPaddingV() const { return scaled(kbdPaddingV_); }
qreal RenderTheme::kbdBorderRadius() const { return scaled(kbdBorderRadius_); }
QColor RenderTheme::kbdBorderColor() const { return kbdBorderColor_; }
qreal RenderTheme::kbdBorderWidth() const { return scaled(kbdBorderWidth_); }
qreal RenderTheme::kbdBorderBottomWidth() const { return scaled(kbdBorderBottomWidth_); }
QColor RenderTheme::kbdBorderBottomColor() const { return kbdBorderBottomColor_; }
QColor RenderTheme::kbdShadowColor() const { return kbdShadowColor_; }

QColor RenderTheme::codeBackgroundColor() const {
  return codeBackgroundColor_;
}

QColor RenderTheme::highlightBackgroundColor() const {
  return highlightBackgroundColor_;
}

QColor RenderTheme::codeBorderColor() const {
  return codeBorderColor_;
}

QColor RenderTheme::quoteBorderColor() const {
  return quoteBorderColor_;
}

QColor RenderTheme::alertAccent(AlertKind kind) const {
  switch (kind) {
    case AlertKind::Note:
      return QColor(QStringLiteral("#0969da"));      // blue
    case AlertKind::Tip:
      return QColor(QStringLiteral("#1a7f37"));      // green
    case AlertKind::Important:
      return QColor(QStringLiteral("#8250df"));      // purple
    case AlertKind::Warning:
      return QColor(QStringLiteral("#9a6700"));      // amber
    case AlertKind::Caution:
      return QColor(QStringLiteral("#cf222e"));      // red
    case AlertKind::None:
      break;
  }
  return quoteBorderColor_;
}

QColor RenderTheme::tableBorderColor() const {
  return tableBorderColor_;
}

QColor RenderTheme::tableHeaderBackgroundColor() const {
  return tableHeaderBackgroundColor_;
}

QColor RenderTheme::tableAlternateBackgroundColor() const {
  return tableAlternateBackgroundColor_;
}

QColor RenderTheme::selectionColor() const {
  return selectionColor_;
}

QColor RenderTheme::spellCheckColor() const {
  return spellCheckColor_;
}

QColor RenderTheme::listMarkerColor() const {
  return listMarkerColor_.isValid() ? listMarkerColor_ : textColor_;
}

const ThemeElementStyle* RenderTheme::elementStyle(const QString& key) const {
  const auto it = elementStyleIndex_.constFind(key);
  if (it == elementStyleIndex_.cend()) { return nullptr; }
  return &elementStyles_[static_cast<std::size_t>(it.value())];
}

ThemeElementBoxStyle RenderTheme::elementBoxStyle(const QString& key, const MarkdownNode* node) const {
  ThemeElementBoxStyle out;
  if (const ThemeElementStyle* style = node ? elementStyleForNode(*node, key) : elementStyle(key)) {
    out = style->box;
    out.margin = QMarginsF(scaled(out.margin.left()), scaled(out.margin.top()), scaled(out.margin.right()), scaled(out.margin.bottom()));
    out.padding = QMarginsF(scaled(out.padding.left()), scaled(out.padding.top()), scaled(out.padding.right()), scaled(out.padding.bottom()));
    out.borderTopWidth = scaled(out.borderTopWidth);
    out.borderRightWidth = scaled(out.borderRightWidth);
    out.borderBottomWidth = scaled(out.borderBottomWidth);
    out.borderLeftWidth = scaled(out.borderLeftWidth);
    out.borderRadius = scaled(out.borderRadius);
  }
  return out;
}

QColor RenderTheme::headingColor(int level) const {
  const int idx = qBound(0, level - 1, 5);
  return headingColor_[idx];  // invalid when the theme doesn't set one
}

QColor RenderTheme::codeBlockBackgroundColor() const {
  return codeBlockBackground_.isValid() ? codeBlockBackground_ : codeBackgroundColor_;
}

QColor RenderTheme::headingAccentColor() const {
  return headingAccentColor_;  // invalid → caller skips the accent bar
}

QColor RenderTheme::blockquoteBackgroundColor() const {
  return blockquoteBackground_;  // invalid → caller paints no quote fill
}

const ThemeDecorations& RenderTheme::decorations() const {
  return decorations_;
}

QColor RenderTheme::codeHighlightColor(CodeHighlightRole role) const {
  const bool dark = backgroundColor_.lightness() < 128;
  switch (role) {
    case CodeHighlightRole::Comment:
      return dark ? QColor(QStringLiteral("#8b949e")) : QColor(QStringLiteral("#8c8c8c"));
    case CodeHighlightRole::Keyword:
      return dark ? QColor(QStringLiteral("#ff7b72")) : QColor(QStringLiteral("#9b008b"));
    case CodeHighlightRole::Preprocessor:
      // Light themes: ride the theme's text colour so preprocessor tokens stay
      // close to plain text regardless of the theme's chosen ink colour.
      return dark ? QColor(QStringLiteral("#ff7b72")) : textColor_;
    case CodeHighlightRole::String:
      return dark ? QColor(QStringLiteral("#a5d6ff")) : QColor(QStringLiteral("#a31515"));
    case CodeHighlightRole::Number:
    case CodeHighlightRole::Constant:
      return dark ? QColor(QStringLiteral("#79c0ff")) : QColor(QStringLiteral("#1a4fb5"));
    case CodeHighlightRole::Function:
      return dark ? QColor(QStringLiteral("#d2a8ff")) : QColor(QStringLiteral("#0000a8"));
    case CodeHighlightRole::Type:
      return dark ? QColor(QStringLiteral("#ffa657")) : QColor(QStringLiteral("#008000"));
    case CodeHighlightRole::Variable:
      return textColor_;
    case CodeHighlightRole::Property:
      return dark ? QColor(QStringLiteral("#7ee787")) : QColor(QStringLiteral("#795e26"));
    case CodeHighlightRole::Operator:
    case CodeHighlightRole::Punctuation:
      return dark ? mutedTextColor_ : QColor(QStringLiteral("#3f3f3f"));
    case CodeHighlightRole::Escape:
      return dark ? QColor(QStringLiteral("#f2cc60")) : QColor(QStringLiteral("#b000b0"));
    case CodeHighlightRole::Plain:
    default:
      return textColor_;
  }
}

QMarginsF RenderTheme::codePadding() const {
  if (codeBlockBoxThemed_) {
    return QMarginsF(scaled(codeBlockPadding_.left()), scaled(codeBlockPadding_.top()),
                     scaled(codeBlockPadding_.right()), scaled(codeBlockPadding_.bottom()));
  }
  return QMarginsF(scaled(12), scaled(10), scaled(12), scaled(10));
}

QMarginsF RenderTheme::tableCellPadding() const {
  if (tableBoxThemed_) {
    return QMarginsF(scaled(tableCellPadding_.left()), scaled(tableCellPadding_.top()),
                     scaled(tableCellPadding_.right()), scaled(tableCellPadding_.bottom()));
  }
  return QMarginsF(scaled(12), scaled(6), scaled(12), scaled(6));
}

qreal RenderTheme::scaled(qreal value) const {
  return value * static_cast<qreal>(zoomPercent_) / 100.0;
}

qreal RenderTheme::scaledFont(qreal value) const {
  return scaled(value) * static_cast<qreal>(fontSizePx_) / 16.0;
}

}  // namespace muffin
