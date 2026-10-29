#include "theme/RenderTheme.h"
#include "diagnostics/ThemePerformance.h"
#include "diagnostics/ScopedPerfProbe.h"
#include "render/CssPaintAnimation.h"

#include "theme/FontRendering.h"

#include "document/MarkdownNode.h"
#include "document/MarkdownDocument.h"
#include "theme/CssContent.h"
#include "theme/DocumentCounterCache.h"
#include "theme/CssComputedStyleEngine.h"
#include "theme/CssThemeMapper.h"
#include "theme/CssDecorationExtractor.h"
#include "theme/NodeCssElement.h"
#include "theme/DocumentStyleTree.h"
#include "theme/LegacyThemeAdapter.h"

#include <QFontDatabase>
#include <QDataStream>
#include <QIODevice>
#include <QCache>
#include "render/TextLayout.h"
#include <QStringList>
#include <QtGlobal>

#include <initializer_list>
#include <limits>

namespace muffin {
namespace {
Q_LOGGING_CATEGORY(themeDetailPerf, "muffin.perf.detail", QtWarningMsg)

std::optional<PseudoElementRule> resolvePseudoAttributes(std::optional<PseudoElementRule> rule,
                                                         const CssElement& origin) {
  if (!rule)
    return {};
  for (auto& token : rule->contentTokens) {
    if (token.kind != ContentToken::Kind::Attribute)
      continue;
    token.text = origin.attributes.contains(token.text) ? origin.attributes.value(token.text) : token.fallback;
    token.kind = ContentToken::Kind::Literal;
  }
  if (!rule->contentTokens.empty() &&
      std::all_of(rule->contentTokens.cbegin(), rule->contentTokens.cend(),
                  [](const auto& token) { return token.kind == ContentToken::Kind::Literal; })) {
    rule->content.clear();
    for (const auto& token : rule->contentTokens)
      rule->content += token.text;
  }
  return rule;
}

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

const QString& mathFamily() {
  static const QString f = firstAvailableFontFamily({
#if defined(Q_OS_WIN)
      QStringLiteral("Cambria Math"),
      QStringLiteral("Segoe UI Symbol"),
#elif defined(Q_OS_MACOS)
      QStringLiteral("STIX Two Math"),
      QStringLiteral("STIXGeneral"),
      QStringLiteral("Apple Symbols"),
#else
      QStringLiteral("STIX Two Math"),
      QStringLiteral("Latin Modern Math"),
      QStringLiteral("DejaVu Math TeX Gyre"),
#endif
  });
  return f;
}

} // namespace

RenderTheme RenderTheme::defaultTheme(int zoomPercent) {
  return github(zoomPercent);
}

RenderTheme::RenderTheme() : RenderTheme(fromDefinition(ThemeDefinition{})) {
}

struct RenderTheme::ProjectionCache {
  QCache<QByteArray, RenderTheme> entries{4};
};

RenderTheme RenderTheme::fromDefinition(const ThemeDefinition& input, int zoomPercent, int fontSizePx) {
  auto theme = fromDefinitionWithEngine(input, zoomPercent, fontSizePx, {});
  theme.projectionCache_ = std::make_shared<ProjectionCache>();
  return theme;
}

RenderTheme RenderTheme::detachedPrototype() const {
  RenderTheme result = *this;
  // Compiled selectors are reusable. Mutable matches and document adapters are
  // private to each view, even when several hidden pages share a pending theme.
  if (styleEngine_)
    result.styleEngine_ = styleEngine_->withEnvironment(styleEngine_->environment());
  result.invalidateDocumentStyles();
  result.semanticSeed_.reset();
  result.semanticDocument_ = nullptr;
  return result;
}

void RenderTheme::reuseDocumentSemantics(const RenderTheme& previous) {
  semanticSeed_ = previous.counterCache_ ? previous.counterCache_ : previous.semanticSeed_;
  semanticDocument_ = previous.counterCache_ ? previous.counterDocument_ : previous.semanticDocument_;
  semanticRevision_ = previous.counterCache_ ? previous.counterRevision_ : previous.semanticRevision_;
}

RenderTheme RenderTheme::fromDefinitionWithEngine(const ThemeDefinition& input, int zoomPercent, int fontSizePx,
                                                  std::shared_ptr<CssComputedStyleEngine> engine) {
  diag::ScopedPerfProbe perf("theme.fromDefinition", themeDetailPerf());
  const ThemeDefinition definition = adaptLegacyTheme(input);
  const ThemeColors& c = definition.colors;
  RenderTheme t(nullptr);
  t.sourceSheet_ = definition.sourceSheet;
  t.sourceId_ = definition.id;
  t.fontAliases_ = definition.fontAliases;
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
  // Semantic math font and palette conveniences projected by the shared engine.
  const ThemeTypography& ty = definition.typography;
  t.mathFont_ = ty.mathFont;
  t.mathSizePt_ = ty.mathSizePt;
  t.linkUnderlined_ = ty.linkUnderlined;
  t.linkUnderlineStyle_ = ty.linkUnderlineStyle;
  t.linkUnderlineColor_ = ty.linkUnderlineColor;
  t.linkOverline_ = ty.linkOverline;
  t.delColor_ = ty.delColor;
  t.viewportBackgroundColor_ = definition.page.viewportBackground;
  t.pageBackgroundColor_ = definition.page.pageBackground;
  t.pageBorderColor_ = definition.page.pageBorderColor;
  t.pageBorderWidth_ = definition.page.pageBorderWidth;
  t.pageBorderRadius_ = definition.page.pageBorderRadius;
  t.pagePadding_ = definition.page.pagePadding;
  t.pageMargin_ = definition.page.pageMargin;
  t.pageMarginExplicit_ = definition.page.pageMarginExplicit;
  t.pageMaxWidth_ = definition.page.pageMaxWidth;
  t.pageBorderBox_ = definition.page.borderBox;
  t.pageShadowColor_ = definition.page.pageShadowColor;
  t.pageShadowOffsetX_ = definition.page.pageShadowOffsetX;
  t.pageShadowBlur_ = definition.page.pageShadowBlur;
  t.pageShadowOffsetY_ = definition.page.pageShadowOffsetY;
  t.pageShadowSpread_ = definition.page.pageShadowSpread;
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
  t.styleEngine_ = std::move(engine);
  if (t.sourceSheet_ && !t.styleEngine_) {
    diag::ScopedPerfProbe enginePerf("theme.createEngine", themeDetailPerf());
    if (definition.preparedEngine && definition.preparedSourceRevision == t.sourceSheet_->revision())
      t.styleEngine_ = definition.preparedEngine->withEnvironment({});
    else
      t.styleEngine_ = std::make_shared<CssComputedStyleEngine>(muffin::documentStyleSheet(*t.sourceSheet_));
  }
  t.viewportUnits_ = t.styleEngine_ && t.styleEngine_->hasViewportUnits();
  if (const auto* page = t.elementStyle(QStringLiteral("#write"))) {
    const auto& box = page->box;
    t.percentagePageBox_ =
        box.widthLength.hasPercentage || box.minWidthLength.hasPercentage || box.maxWidthLength.hasPercentage;
    for (const auto& sides : {box.marginLengths.sides, box.paddingLengths.sides})
      for (const auto& length : sides)
        t.percentagePageBox_ = t.percentagePageBox_ || length.hasPercentage;
  }
  t.listMarkerGap_ = definition.spacing.listMarkerGap;
  t.ulListStyleType_ = definition.spacing.ulListStyleType;
  t.olListStyleType_ = definition.spacing.olListStyleType;
  t.liListStyleType_ = definition.spacing.liListStyleType;
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
  if (def) {
    return RenderTheme::fromDefinition(*def, zoomPercent);
  }
  RenderTheme theme;
  theme.setZoomPercent(zoomPercent);
  return theme;
}
} // namespace

RenderTheme RenderTheme::github(int zoomPercent) {
  return loadBuiltInTheme(QStringLiteral("github"), zoomPercent);
}
RenderTheme RenderTheme::newsprint(int zoomPercent) {
  return loadBuiltInTheme(QStringLiteral("newsprint"), zoomPercent);
}
RenderTheme RenderTheme::night(int zoomPercent) {
  return loadBuiltInTheme(QStringLiteral("night"), zoomPercent);
}
RenderTheme RenderTheme::pixyll(int zoomPercent) {
  return loadBuiltInTheme(QStringLiteral("pixyll"), zoomPercent);
}
RenderTheme RenderTheme::whitey(int zoomPercent) {
  return loadBuiltInTheme(QStringLiteral("whitey"), zoomPercent);
}

int RenderTheme::zoomPercent() const {
  return zoomPercent_;
}

void RenderTheme::setZoomPercent(int percent) {
  percent = qBound(60, percent, 200);
  if (zoomPercent_ == percent)
    return;
  zoomPercent_ = percent;
  cssViewportWidth_ = -1.0;
}

int RenderTheme::fontSizePx() const {
  return fontSizePx_;
}

void RenderTheme::setFontSizePx(int px) {
  px = qBound(12, px, 24);
  if (fontSizePx_ == px)
    return;
  fontSizePx_ = px;
  cssViewportWidth_ = -1.0;
}

bool RenderTheme::updateForViewport(qreal width, qreal height) {
  diag::ScopedPerfProbe perf("theme.viewportProjection", themeDetailPerf());
  if (!sourceSheet_)
    return false;
  const qreal zoom = zoomPercent_ / 100.0;
  const qreal cssWidth = width / zoom, cssHeight = height / zoom;
  if (qAbs(cssWidth - cssViewportWidth_) < 0.01 && qAbs(cssHeight - cssViewportHeight_) < 0.01)
    return false;
  CssEnvironment environment;
  environment.viewportWidth = cssWidth;
  environment.viewportHeight = cssHeight;
  environment.textScale = fontSizePx_ / 16.0;
  environment.dark = backgroundColor_.lightnessF() < 0.5;
  const bool reuseProjection =
      cssViewportWidth_ >= 0 && !viewportUnits_ && !percentagePageBox_ && styleEngine_->sameActiveRules(environment);
  if (reuseProjection) {
    // A width change rewraps text, but it does not change selectors or computed
    // styles in this media bucket. Keep the live tree, paint recipes and fonts.
    // Detach only if another theme snapshot still owns the engine.
    if (styleEngine_.use_count() == 1)
      styleEngine_->updateViewportSize(cssWidth, cssHeight);
    else
      styleEngine_ = styleEngine_->withEnvironment(environment);
    cssViewportWidth_ = cssWidth;
    cssViewportHeight_ = cssHeight;
    return false;
  }
  QByteArray projectionKey;
  QDataStream keyStream(&projectionKey, QIODevice::WriteOnly);
  keyStream << sourceSheet_->revision() << cssWidth << cssHeight << zoomPercent_ << fontSizePx_ << environment.dark;
  if (projectionCache_)
    if (const auto* cached = projectionCache_->entries.object(projectionKey)) {
      diag::ScopedPerfProbe cachePerf("theme.reuseProjection", themeDetailPerf());
      auto resolved = cached->detachedPrototype();
      resolved.projectionCache_ = projectionCache_;
      resolved.contentWidthPx_ = contentWidthPx_;
      resolved.reuseDocumentSemantics(*this);
      *this = std::move(resolved);
      return true;
    }
  auto engine = styleEngine_->withEnvironment(environment);
  const auto definition = CssThemeMapper::fromSheet(*sourceSheet_, sourceId_, environment, engine.get());
  RenderTheme resolved = fromDefinitionWithEngine(definition, zoomPercent_, fontSizePx_, std::move(engine));
  resolved.contentWidthPx_ = contentWidthPx_;
  const auto before = cssViewportWidth_ < 0 ? CssEnvironment{} : documentCssEnvironment();
  const bool fontsChanged =
      std::any_of(sourceSheet_->fontFaces().begin(), sourceSheet_->fontFaces().end(),
                  [&](const auto& face) { return face.active(before) != face.active(environment); });
  resolved.fontAliases_ =
      fontsChanged ? ThemeDefinition::registerFonts(sourceSheet_->evaluated(environment)) : fontAliases_;
  resolved.cssViewportWidth_ = cssWidth;
  resolved.cssViewportHeight_ = cssHeight;
  if (projectionCache_) {
    resolved.styleEngine_->clearCache();
    auto* prototype = new RenderTheme(resolved);
    prototype->styleEngine_ = resolved.styleEngine_->withEnvironment(environment);
    projectionCache_->entries.insert(projectionKey, prototype);
  }
  resolved.projectionCache_ = projectionCache_;
  resolved.reuseDocumentSemantics(*this);
  *this = std::move(resolved);
  return true;
}

int RenderTheme::contentWidthPx() const {
  return contentWidthPx_;
}

CssEnvironment RenderTheme::documentCssEnvironment() const {
  CssEnvironment environment;
  if (cssViewportWidth_ > 0)
    environment.viewportWidth = cssViewportWidth_;
  if (cssViewportHeight_ > 0)
    environment.viewportHeight = cssViewportHeight_;
  environment.textScale = fontSizePx_ / 16.0;
  environment.dark = backgroundColor_.lightnessF() < 0.5;
  return environment;
}

void RenderTheme::setContentWidthPx(int px) {
  if (px == -1) {
    contentWidthPx_ = -1;
  } else if (px > 0) {
    contentWidthPx_ = qBound(640, px, 2400);
  } else {
    contentWidthPx_ = 0;
  }
}

qreal RenderTheme::pageWidth() const {
  if (contentWidthPx_ == -1) {
    return std::numeric_limits<qreal>::max() / 4.0;
  }
  const qreal width =
      contentWidthPx_ > 0 ? static_cast<qreal>(contentWidthPx_) : (pageMaxWidth_ > 0.0 ? pageMaxWidth_ : 860.0);
  return scaled(width);
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
    if (style->box.padding.left() > 0.0) {
      return scaled(style->box.padding.left());
    }
  }
  return scaled(30.0);
}

qreal RenderTheme::listMarkerGap() const {
  // An explicit theme override wins. Otherwise "auto": keep the legacy
  // proportional gap (0.2 × indent, already zoomed via listIndent()) BUT floor
  // it so small-indent themes don't collapse the marker onto the text. The floor
  // is small enough that large-indent themes (e.g. github's 30px → 6px) are
  // visually unchanged.
  if (listMarkerGap_ > 0.0) {
    return scaled(listMarkerGap_);
  }
  constexpr qreal kMarkerGapFloor = 4.5;
  return qMax(listIndent() * 0.2, scaled(kMarkerGapFloor));
}

QString RenderTheme::listStyleTypeForItem(bool ordered) const {
  // Ordered items take `ol`'s type, bullet items `ul`'s; a direct `li` declaration
  // is the fallback (covers `li { list-style-type }` with no per-list rule).
  const QString type = ordered ? olListStyleType_ : ulListStyleType_;
  return !type.isEmpty() ? type : liListStyleType_;
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
  return QMarginsF(scaled(pagePadding_.left()), scaled(pagePadding_.top()), scaled(pagePadding_.right()),
                   scaled(pagePadding_.bottom()));
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
  return QMarginsF(scaled(pageMargin_.left()), scaled(pageMargin_.top()), scaled(pageMargin_.right()),
                   scaled(pageMargin_.bottom()));
}

QColor RenderTheme::pageShadowColor() const {
  return pageShadowColor_;
}
qreal RenderTheme::pageShadowOffsetX() const {
  return scaled(pageShadowOffsetX_);
}
qreal RenderTheme::pageShadowBlur() const {
  return scaled(pageShadowBlur_);
}
qreal RenderTheme::pageShadowOffsetY() const {
  return scaled(pageShadowOffsetY_);
}
qreal RenderTheme::pageShadowSpread() const {
  return scaled(pageShadowSpread_);
}

QMarginsF RenderTheme::blockMargin(BlockType type, int headingLevel, const MarkdownNode* node,
                                   qreal containingWidth) const {
  // Prototypes serve estimates; live nodes always resolve against the document tree.
  QString key;
  QMarginsF m; // null unless a block-flow margin or an element style sets it
  switch (type) {
  case BlockType::Heading:
    key = QStringLiteral("h%1").arg(headingLevel);
    break;
  case BlockType::Paragraph:
    key = QStringLiteral("p");
    break;
  case BlockType::BlockQuote:
    key = QStringLiteral("blockquote");
    break;
  case BlockType::CodeFence:
  case BlockType::FrontMatter:
    key = QStringLiteral("pre");
    break;
  case BlockType::Table:
    key = QStringLiteral(".table-figure");
    break;
  case BlockType::List:
    key = node && node->listKind() == ListKind::Ordered ? QStringLiteral("ol") : QStringLiteral("ul");
    break;
  case BlockType::HtmlBlock:
    if (node)
      if (const auto* container = markdownFlow(*node)->container(*node))
        key = container->attributes.tag;
    break;
  default:
    break;
  }
  if (!key.isEmpty()) {
    const ThemeElementStyle* style = node ? elementStyleForNode(*node, key) : elementStyle(key);
    if (type == BlockType::Table && (!style || !style->box.marginSpecified))
      style = node ? elementStyleForNode(*node, QStringLiteral("table")) : elementStyle(QStringLiteral("table"));
    if (style) {
      if (style->box.marginSpecified) {
        m = style->box.marginLengths.used(style->box.margin, containingWidth / (zoomPercent_ / 100.0));
      }
    }
  }
  return QMarginsF(scaled(m.left()), scaled(m.top()), scaled(m.right()), scaled(m.bottom()));
}

bool RenderTheme::hasBlockMargin(BlockType type, int headingLevel, const MarkdownNode* node) const {
  const QString key = type == BlockType::Heading      ? QStringLiteral("h%1").arg(headingLevel)
                      : type == BlockType::Paragraph  ? QStringLiteral("p")
                      : type == BlockType::BlockQuote ? QStringLiteral("blockquote")
                      : type == BlockType::CodeFence  ? QStringLiteral("pre")
                      : type == BlockType::Table      ? QStringLiteral(".table-figure")
                                                      : QString();
  if (const auto* style = node ? elementStyleForNode(*node, key) : elementStyle(key)) {
    if (style->box.marginSpecified)
      return true;
    if (type == BlockType::Table) {
      const auto* table =
          node ? elementStyleForNode(*node, QStringLiteral("table")) : elementStyle(QStringLiteral("table"));
      return table && table->box.marginSpecified;
    }
    return false;
  }
  return !blockMargin(type, headingLevel, node).isNull();
}

QFont RenderTheme::paragraphFont() const {
  return textFontForElement(QStringLiteral("p"));
}

QFont RenderTheme::textFontForElement(const QString& key, const MarkdownNode* node) const {
  if (!node) {
    const auto it = prototypeFontCache_.constFind(key);
    if (it != prototypeFontCache_.constEnd())
      return it.value();
  }
  QFont font;
  font.setFamilies(font_rendering::cssFamilyList(serifBody_ ? QStringLiteral("serif") : QStringLiteral("sans-serif"),
                                                 font_rendering::documentFallbackTail()));
  font.setPointSizeF(scaledFont(12));
  const ThemeElementStyle* style = node ? elementStyleForNode(*node, key) : elementStyle(key);
  if (style)
    font = fontForStyle(*style, font);
  else
    font_rendering::configureForScreen(font);
  if (!node)
    prototypeFontCache_.insert(key, font);
  return font;
}

QColor RenderTheme::textColorForElement(const QString& key, const MarkdownNode* node) const {
  if (const ThemeElementStyle* style = node ? elementStyleForNode(*node, key) : elementStyle(key)) {
    if (style->paint.color.isValid()) {
      return style->paint.color;
    }
  }
  return textColor_;
}

qreal RenderTheme::lineHeightMultiplierForElement(const QString& key, const MarkdownNode* node) const {
  const auto* style = node ? elementStyleForNode(*node, key) : elementStyle(key);
  return style ? style->text.lineHeight : 0;
}

Qt::Alignment RenderTheme::textAlignmentForElement(const QString& key, const MarkdownNode* node) const {
  const auto* style = node ? elementStyleForNode(*node, key) : elementStyle(key);
  return style && style->text.alignment ? style->text.alignment : Qt::AlignLeft;
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

QFont RenderTheme::fontForStyle(const ThemeElementStyle& style, QFont font) const {
  diag::ScopedPerfProbe perf("theme.fontForStyle", themeDetailPerf());
  const QString cacheKey = QString::number(style.fingerprint) + QLatin1Char('/') + font.key() + QLatin1Char('/') +
                           QString::number(zoomPercent_) + QLatin1Char('/') + QString::number(fontSizePx_);
  if (const auto found = computedFontCache_.constFind(cacheKey); found != computedFontCache_.cend())
    return found.value();
  font.setStyleHint(style.text.fontFamily.split(QLatin1Char('\n')).contains(QStringLiteral("monospace"))
                        ? QFont::Monospace
                        : QFont::AnyStyle);
  if (style.text.fontWeightSet)
    font.setWeight(static_cast<QFont::Weight>(style.text.fontWeight));
  if (style.text.italicSet)
    font.setItalic(style.text.italic);
  if (!style.text.fontFamily.isEmpty())
    font.setFamilies(font_rendering::cssFamilyList(style.text.fontFamily, font_rendering::documentFallbackTail(),
                                                   fontAliases_, font.weight(), font.italic()));
  if (style.text.fontSizeSet || style.text.fontSizePx > 0)
    font.setPointSizeF(qMax<qreal>(.001, scaledFont(pxToPt(style.text.fontSizePx))));
  font_rendering::configureCssFont(font, scaled(style.text.letterSpacing), scaled(style.text.wordSpacing));
  computedFontCache_.insert(cacheKey, font);
  return font;
}

ThemeElementStyle RenderTheme::projectStyle(const QString& key, const CssComputedStyle& computed) const {
  diag::ScopedPerfProbe perf("theme.projectStyle", themeDetailPerf());
  const auto cacheKey = key + QLatin1Char('/') + QString::number(computed.fingerprint());
  const auto found = projectedStyleCache_.constFind(cacheKey);
  if (found != projectedStyleCache_.cend())
    return found.value();
  auto projected = CssThemeMapper::projectComputedStyle(key, computed);
  projectedStyleCache_.insert(cacheKey, projected);
  return projected;
}

ThemeElementStyle RenderTheme::inlineStyleForNode(const MarkdownNode& owner, qsizetype offset) const {
  diag::ScopedPerfProbe perf("theme.inlineStyle", themeDetailPerf());
  if (!styleEngine_)
    return {};
  if (!styleTree_)
    styleTree_ = std::make_shared<NodeCssElementBuilder>(hasNthOfType_);
  const auto* element = styleTree_->buildInline(owner, offset);
  const auto* block = styleTree_->build(owner);
  const auto cacheKey = qMakePair(block->cacheId, element->cacheId);
  if (const auto found = inlineStyleCache_.constFind(cacheKey); found != inlineStyleCache_.cend())
    return found.value();
  auto resolved = styleForElement(*element);
  for (const auto* parent = element == block ? nullptr : element->parent; parent;
       parent = parent == block ? nullptr : parent->parent) {
    const auto inheritedLine = projectStyle(parent->tag, styleEngine_->styleFor(*parent)).text;
    if (inheritedLine.decorationLines) {
      if (!resolved.text.decorationLines) {
        resolved.text.decorationColor = inheritedLine.decorationColor;
        resolved.text.underlineStyle = inheritedLine.underlineStyle;
      }
      resolved.text.decorationLines |= inheritedLine.decorationLines;
    }
  }
  resolved.fingerprint ^= qHashMulti(size_t(0), resolved.text.decorationLines, resolved.text.underlineStyle,
                                     resolved.text.decorationColor.rgba());
  if (inlineStyleCache_.size() >= 2048)
    inlineStyleCache_.clear();
  inlineStyleCache_.insert(cacheKey, resolved);
  return resolved;
}

ThemeElementStyle RenderTheme::styleForElement(const CssElement& element) const {
  diag::ScopedPerfProbe perf("theme.elementStyle", themeDetailPerf());
  if (!styleEngine_)
    return {};
  if (element.cacheId) {
    if (const auto found = elementStyleCache_.constFind(element.cacheId); found != elementStyleCache_.cend())
      return found.value();
  }
  auto style = projectStyle(element.tag, styleEngine_->styleFor(element));
  style.paintRecipe = paintRecipeForElement(element);
  if (element.cacheId) {
    if (elementStyleCache_.size() >= 2048)
      elementStyleCache_.clear();
    elementStyleCache_.insert(element.cacheId, style);
  }
  return style;
}

CssComputedStyle RenderTheme::computedStyleForElement(const CssElement& element) const {
  return styleEngine_ ? styleEngine_->styleFor(element) : CssComputedStyle{};
}

QVector<CssInlineImage> RenderTheme::cssInlineImages(const MarkdownNode& owner) const {
  if (!styleTree_)
    styleTree_ = std::make_shared<NodeCssElementBuilder>(hasNthOfType_);
  return styleTree_->inlineImages(owner);
}

std::shared_ptr<const CssPaintRecipe> RenderTheme::paintRecipeForElement(const CssElement& origin,
                                                                         const QString& pseudo) const {
  diag::ScopedPerfProbe perf("theme.paintRecipe", themeDetailPerf());
  if (!styleEngine_ || !sourceSheet_)
    return {};
  const auto key = QString::number(origin.cacheId) + '/' + pseudo;
  if (origin.cacheId) {
    const auto found = paintRecipeCache_.constFind(key);
    if (found != paintRecipeCache_.cend())
      return found.value();
  }
  CssElement temporary;
  const CssElement* element = &origin;
  if (!pseudo.isEmpty()) {
    if (origin.cacheId) {
      if (!styleTree_)
        styleTree_ = std::make_shared<NodeCssElementBuilder>(hasNthOfType_);
      element = styleTree_->buildPseudo(origin, pseudo);
    } else {
      temporary = origin;
      temporary.inlineDeclarations.clear();
      temporary.pseudoElement = pseudo;
      temporary.originatingElement = &origin;
      element = &temporary;
    }
  }
  const auto computed = styleEngine_->styleFor(*element);
  const auto stateMask = styleEngine_->interactiveStateMask(*element);
  if (!stateMask && computed.resolvedValue("animation").isEmpty() &&
      computed.resolvedValue("animation-name").isEmpty()) {
    if (origin.cacheId)
      paintRecipeCache_.insert(key, {});
    return {};
  }
  const auto recipeKey = origin.tag + (pseudo.isEmpty() ? QString() : "::" + pseudo);
  std::array<CssComputedStyle, 8> states;
  quint64 fingerprint = qHash(recipeKey);
  for (int bits = 0; bits < 8; ++bits) {
    const int canonical = bits & stateMask;
    if (canonical != bits)
      states[bits] = states[canonical];
    else if (!bits)
      states[bits] = computed;
    else {
      CssElementState state;
      state.hover = bits & 1;
      state.focus = state.mdFocus = bits & 2;
      state.active = bits & 4;
      state.subject = &origin;
      states[bits] = styleEngine_->styleFor(*element, state);
    }
    fingerprint = qHashMulti(size_t(fingerprint), states[bits].fingerprint());
  }
  if (const auto bucket = sharedPaintRecipes_.constFind(fingerprint); bucket != sharedPaintRecipes_.cend())
    for (const auto& cached : *bucket)
      if (cached.key == recipeKey && cached.states == states) {
        if (origin.cacheId)
          paintRecipeCache_.insert(key, cached.recipe);
        return cached.recipe;
      }
  const auto recipe = compileCssPaintRecipe(
      recipeKey,
      [&](CssElementState state) {
        return states[(state.hover ? 1 : 0) | ((state.focus || state.mdFocus) ? 2 : 0) | (state.active ? 4 : 0)];
      },
      *sourceSheet_, documentCssEnvironment().reducedMotion, stateMask);
  if (sharedPaintRecipeCount_ >= 512) {
    sharedPaintRecipes_.clear();
    sharedPaintRecipeCount_ = 0;
  }
  sharedPaintRecipes_[fingerprint].push_back({recipeKey, std::move(states), recipe});
  ++sharedPaintRecipeCount_;
  if (origin.cacheId)
    paintRecipeCache_.insert(key, recipe);
  return recipe;
}

const CssElement* RenderTheme::cssParentForInlineHtml(const MarkdownNode& owner, qsizetype offset) const {
  if (!styleTree_)
    styleTree_ = std::make_shared<NodeCssElementBuilder>(hasNthOfType_);
  const auto* element = styleTree_->buildInline(owner, offset);
  return element->parent ? element->parent : cssElementForNode(owner);
}

const CssElement* RenderTheme::cssElementForNode(const MarkdownNode& node) const {
  if (!styleTree_)
    styleTree_ = std::make_shared<NodeCssElementBuilder>(hasNthOfType_);
  return styleTree_->build(node);
}

std::shared_ptr<const MarkdownFlow> RenderTheme::markdownFlow(const MarkdownNode& root) const {
  if (!styleTree_)
    styleTree_ = std::make_shared<NodeCssElementBuilder>(hasNthOfType_);
  return styleTree_->flowFor(root);
}

const ThemeElementStyle* RenderTheme::elementStyleForNode(const MarkdownNode& node, const QString& key) const {
  if (!styleEngine_) {
    return elementStyle(key);
  }
  // Reuse one sparse adapter across all queries in this rebuild. It materializes only
  // nodes reached by selector navigation. CSS identities survive text-only AST replacements.
  if (!styleTree_) {
    styleTree_ = std::make_shared<NodeCssElementBuilder>(hasNthOfType_);
  }
  const auto* element = styleTree_->build(node, key);
  const QString cacheKey = QString::number(element->cacheId) + '/' + key;
  const auto it = nodeStyleCache_.constFind(cacheKey);
  if (it != nodeStyleCache_.constEnd())
    return it.value().get();
  CssElementState state;
  state.hover = key.endsWith(QStringLiteral(":hover"));
  state.focus = key.endsWith(QStringLiteral(":focus"));
  ThemeElementStyle resolved = projectStyle(key, styleEngine_->styleFor(*element, state));
  if (!state.hover && !state.focus)
    resolved.paintRecipe = paintRecipeForElement(*element);
  resolved.key = key;
  const auto snapshot = std::make_shared<const ThemeElementStyle>(std::move(resolved));
  nodeStyleCache_.insert(cacheKey, snapshot);
  return snapshot.get();
}

std::optional<PseudoElementRule> RenderTheme::pseudoForNode(const MarkdownNode& node, const QString& pseudo) const {
  return pseudoForElement(*cssElementForNode(node), pseudo);
}

const CssElement* RenderTheme::cssInlineElement(const MarkdownNode& owner, qsizetype sourceOffset,
                                                const QString& tag) const {
  if (!styleTree_)
    styleTree_ = std::make_shared<NodeCssElementBuilder>(hasNthOfType_);
  const auto* element = styleTree_->buildInline(owner, sourceOffset);
  if (tag.isEmpty())
    return element;
  while (element && element->tag != tag)
    element = element->parent;
  return element;
}

const CssElement* RenderTheme::cssLinkInSourceRange(const MarkdownNode& owner, qsizetype start, qsizetype end,
                                                    const QString& href) const {
  if (!styleTree_)
    styleTree_ = std::make_shared<NodeCssElementBuilder>(hasNthOfType_);
  return styleTree_->linkForSourceRange(owner, start, end, href);
}

std::optional<PseudoElementRule> RenderTheme::pseudoForElement(const CssElement& origin, const QString& pseudo,
                                                               CssElementState state) const {
  const auto finish = [&](std::optional<PseudoElementRule> rule) {
    rule = resolvePseudoAttributes(std::move(rule), origin);
    const auto found =
        counterChains_.constFind(styleTree_ ? styleTree_->semanticKey(origin) + '/' + pseudo : QString());
    if (rule && found != counterChains_.cend()) {
      for (auto& token : rule->contentTokens) {
        if (token.kind != ContentToken::Kind::Counter && token.kind != ContentToken::Kind::Counters)
          continue;
        const auto chain = found->value(token.text, QVector<int>{0});
        QStringList values;
        if (token.kind == ContentToken::Kind::Counter)
          values << formatCounterValue(chain.back(), token.style);
        else
          for (int value : chain)
            values << formatCounterValue(value, token.style);
        token.text = values.join(token.sep);
        token.kind = ContentToken::Kind::Literal;
      }
      rule->content.clear();
      for (const auto& token : rule->contentTokens)
        rule->content += token.text;
    }
    return rule;
  };
  const auto host = origin.id == QStringLiteral("write") ? QStringLiteral("#write") : origin.tag;
  const auto key = host + QStringLiteral("::") + pseudo;
  if (styleEngine_) {
    if (!styleEngine_->mayGeneratePseudo(origin, pseudo))
      return {};
    if (!origin.cacheId) {
      // Standalone renderers may supply a temporary semantic tree. Never
      // retain its addresses in the document adapter or snapshot cache.
      CssElement element = origin;
      element.inlineDeclarations.clear();
      element.pseudoElement = pseudo;
      element.originatingElement = &origin;
      const auto rules = extractPseudoRules({{key, styleEngine_->styleFor(element, state)}});
      return finish(rules.empty() ? std::nullopt : std::optional<PseudoElementRule>(rules.front()));
    }
    if (!styleTree_)
      styleTree_ = std::make_shared<NodeCssElementBuilder>(hasNthOfType_);
    const auto* element = styleTree_->buildPseudo(origin, pseudo);
    const auto cacheKey = QString::number(element->cacheId) + QLatin1Char('/') +
                          QString::number(int(state.hover) | (int(state.focus) << 1) | (int(state.active) << 2) |
                                          (int(state.visited) << 3) | (int(state.mdFocus) << 4));
    if (const auto cached = pseudoStyleCache_.constFind(cacheKey); cached != pseudoStyleCache_.cend())
      return finish(cached.value());
    const auto rules = extractPseudoRules({{key, styleEngine_->styleFor(*element, state)}});
    const auto result =
        resolvePseudoAttributes(rules.empty() ? std::nullopt : std::optional<PseudoElementRule>(rules.front()), origin);
    pseudoStyleCache_.insert(cacheKey, result);
    return finish(result);
  }
  for (const auto& rule : decorations_.pseudos)
    if (rule.host == host && rule.pseudo == pseudo)
      return finish(rule);
  return {};
}

DocumentCounterStats RenderTheme::documentCounterStats() const {
  return counterCache_ ? counterCache_->stats() : DocumentCounterStats{};
}

RenderTheme::StyleCacheStats RenderTheme::styleCacheStats() const {
  return {styleTree_ ? styleTree_->materializedElementCount() : 0, nodeStyleCache_.size(), paintRecipeCache_.size(),
          sharedPaintRecipeCount_, styleEngine_ ? styleEngine_->cacheStats() : CssComputedStyleEngine::CacheStats{}};
}

void RenderTheme::releaseInlineStyles(const QSet<NodeId>& topLevelIds) const {
  // Other theme snapshots can still own layout/HTML pointers into this tree.
  if (!styleTree_ || styleTree_.use_count() != 1 || !styleEngine_ || styleEngine_.use_count() != 1)
    return;
  const auto identities = styleTree_->releaseInlineViews(topLevelIds);
  const QSet<quint64> removed(identities.begin(), identities.end());
  styleEngine_->forgetElements(identities);
  const auto trim = [&](auto& cache) {
    for (auto it = cache.begin(); it != cache.end();)
      if (removed.contains(it.key().section('/', 0, 0).toULongLong()))
        it = cache.erase(it);
      else
        ++it;
  };
  trim(nodeStyleCache_);
  trim(pseudoStyleCache_);
  trim(paintRecipeCache_);
  for (auto it = elementStyleCache_.begin(); it != elementStyleCache_.end();)
    if (removed.contains(it.key()))
      it = elementStyleCache_.erase(it);
    else
      ++it;
  for (auto it = inlineStyleCache_.begin(); it != inlineStyleCache_.end();)
    if (removed.contains(it.key().first) || removed.contains(it.key().second))
      it = inlineStyleCache_.erase(it);
    else
      ++it;
}

QHash<NodeId, QPair<QString, QString>> RenderTheme::resolveDocumentCounters(const MarkdownDocument& document) const {
  diag::ScopedPerfProbe perf("theme.documentCounters", themeDetailPerf());
  ThemePerfScope performance(ThemePerfStage::Counters);
  if (counterDocument_ == &document && counterRevision_ == document.revision())
    return counterText_;
  const auto& nodes = document.root().children();
  const bool local =
      counterCache_ && counterDocument_ == &document && counterChange_.isValid() &&
      counterChange_.documentRevision == document.revision() && counterRevision_ + 1 == document.revision() &&
      counterChange_.first + counterChange_.oldCount <= counterCache_->blocks.size() &&
      counterChange_.first + counterChange_.newCount <= qsizetype(nodes.size()) &&
      counterCache_->blocks.size() - counterChange_.oldCount + counterChange_.newCount == qsizetype(nodes.size());
  if (!local)
    counterCache_ = std::make_shared<DocumentCounterCache>();
  else if (counterCache_.use_count() > 1)
    counterCache_ = std::make_shared<DocumentCounterCache>(*counterCache_);
  const auto seed =
      semanticDocument_ == &document && semanticRevision_ == document.revision() ? semanticSeed_ : nullptr;
  semanticSeed_.reset();
  semanticDocument_ = nullptr;
  counterDocument_ = &document;
  counterRevision_ = document.revision();
  counterChains_.clear();
  counterFingerprints_.clear();
  counterText_.clear();
  if (!styleEngine_ || !sourceSheet_)
    return counterText_;
  const auto& features = styleEngine_->selectorFeatures();
  const auto& subjects = features.counterSubjects;
  const bool reuseSemantics = !local && seed && seed->blocks.size() == qsizetype(nodes.size()) &&
                              std::equal(nodes.begin(), nodes.end(), seed->blocks.begin(),
                                         [](const auto& node, const auto& block) { return node->id() == block.id; });
  if (reuseSemantics)
    counterCache_->reuseSemanticPool(*seed);
  const auto semanticFor = [&](qsizetype index) {
    const auto& node = *nodes[size_t(index)];
    if (seed && index < seed->blocks.size() && seed->blocks[index].id == node.id())
      return seed->blocks[index].semantic;
    ++counterCache_->semanticBuilds;
    return cssSemanticSignature(node);
  };
  const auto retainedSemanticFor = [&](qsizetype index) {
    auto shape = semanticFor(index);
    return reuseSemantics ? shape : counterCache_->retainSemantic(std::move(shape));
  };
  if (subjects.isEmpty()) {
    auto& cache = *counterCache_;
    if (local) {
      for (qsizetype i = 0; i < counterChange_.oldCount; ++i)
        cache.releaseSemantic(cache.blocks[counterChange_.first + i].semantic);
      cache.blocks.remove(counterChange_.first, counterChange_.oldCount);
      for (qsizetype i = 0; i < counterChange_.newCount; ++i) {
        const auto& node = *nodes[size_t(counterChange_.first + i)];
        cache.blocks.insert(counterChange_.first + i,
                            {node.id(), {}, cache.retainSemantic(cssSemanticSignature(node))});
      }
    } else {
      cache.blocks.reserve(nodes.size());
      for (qsizetype i = 0; i < qsizetype(nodes.size()); ++i)
        cache.blocks.append({nodes[size_t(i)]->id(), {}, retainedSemanticFor(i)});
    }
    counterChange_ = {};
    return counterText_;
  }
  if (!styleTree_)
    styleTree_ = std::make_shared<NodeCssElementBuilder>(hasNthOfType_);

  const auto* root = cssElementForNode(document.root());
  const auto operations = [](const CssComputedStyle& style, CounterProgram& program) {
    int i = 0;
    for (const auto* property : {"counter-reset", "counter-increment", "counter-set"}) {
      if (const auto ops = parseCounterOperations(style.resolvedValue(property), i == 1 ? 1 : 0))
        program.operations[i] = *ops;
      ++i;
    }
  };
  const auto pseudo = [&](const CssElement& element, const QString& side) -> DocumentCounterCache::Program {
    if (!styleEngine_->mayGeneratePseudo(element, side))
      return {};
    const auto style = styleEngine_->styleFor(*styleTree_->buildPseudo(element, side));
    const auto content = style.resolvedValue("content").trimmed();
    if (style.resolvedValue("display") == "none" || content.isEmpty() || content == "none" || content == "normal")
      return {};
    auto result = std::make_shared<CounterProgram>();
    operations(style, *result);
    result->content = parseContentTokens(content);
    for (auto& token : result->content)
      if (token.kind == ContentToken::Kind::Attribute) {
        token.text = element.attributes.value(token.text, token.fallback);
        token.kind = ContentToken::Kind::Literal;
      }
    result->key = styleTree_->semanticKey(element) + '/' + side;
    for (auto* ancestor = &element; ancestor; ancestor = ancestor->parent)
      if (const auto id = styleTree_->sourceNodeId(*ancestor); id.isValid()) {
        result->owner = id;
        break;
      }
    return result;
  };
  const auto dependencies = [&](const CssElement& element) {
    QByteArray result;
    QDataStream stream(&result, QIODevice::WriteOnly);
    stream << styleEngine_->counterDependencySignature(element);
    for (const auto& side : {QStringLiteral("before"), QStringLiteral("after")})
      if (styleEngine_->mayGeneratePseudo(element, side))
        stream << side << styleEngine_->counterDependencySignature(*styleTree_->buildPseudo(element, side));
    return result;
  };
  const auto compileElement = [&](const CssElement& element) -> std::shared_ptr<CounterProgram> {
    // UI/export context rules cannot invalidate document counters. Keep
    // structural predicates conservative, but require their semantic subject
    // to exist on a counter path or one of its inherited ancestors.
    if (std::any_of(features.counterContextSubjects.begin(), features.counterContextSubjects.end(),
                    [&](const auto& subject) { return subject.mayMatch(element); }))
      counterCache_->contextual = true;
    const auto style = styleEngine_->styleFor(element);
    if (style.resolvedValue("display") == "none")
      return {};
    auto result = std::make_shared<CounterProgram>();
    operations(style, *result);
    result->key = styleTree_->semanticKey(element);
    result->dependencies = dependencies(element);
    result->owner = styleTree_->sourceNodeId(element);
    result->before = pseudo(element, "before");
    result->after = pseudo(element, "after");
    return result;
  };
  const auto compileBlock = [&](const MarkdownNode& node, const DocumentCounterCache::Program& previous = {},
                                bool knownCandidate = false) -> DocumentCounterCache::Program {
    if (!knownCandidate) {
      ++counterCache_->candidateScans;
      if (!styleTree_->mayHaveCounterSubjects(node, subjects))
        return {};
    }
    const auto* blockRoot = cssElementForNode(node);
    while (blockRoot->parent && blockRoot->parent != root)
      blockRoot = blockRoot->parent;
    QSet<const CssElement*> relevant;
    QHash<const CssElement*, QVector<const CssElement*>> children;
    const auto retain = [&](const CssElement* view) {
      for (; view && view != root; view = view->parent) {
        if (!relevant.contains(view) && view->parent != root)
          children[view->parent].append(view);
        relevant.insert(view);
      }
    };
    std::function<void(const CssElement&)> discover = [&](const auto& element) {
      if (!element.inlineDeclarations.empty() ||
          std::any_of(subjects.begin(), subjects.end(), [&](const auto& subject) { return subject.mayMatch(element); }))
        retain(&element);
      const auto inlineSubjects = styleTree_->mayHaveInlineSubjects(element, subjects);
      for (const auto* child : styleTree_->semanticChildren(element, inlineSubjects))
        discover(*child);
    };
    discover(*blockRoot);
    if (!relevant.contains(blockRoot))
      return {};
    std::function<DocumentCounterCache::Program(const CssElement&, DocumentCounterCache::Program)> compile;
    compile = [&](const auto& element, auto old) -> DocumentCounterCache::Program {
      const bool same =
          old && old->key == styleTree_->semanticKey(element) && old->dependencies == dependencies(element);
      auto result = same ? std::make_shared<CounterProgram>(*old) : compileElement(element);
      if (!result)
        return {};
      result->children.clear();
      for (const auto* child : children.value(&element)) {
        DocumentCounterCache::Program previousChild;
        if (old)
          for (const auto& candidate : old->children)
            if (candidate->key == styleTree_->semanticKey(*child)) {
              previousChild = candidate;
              break;
            }
        if (auto program = compile(*child, previousChild))
          result->children.append(std::move(program));
      }
      return same && result->children == old->children ? old : result;
    };
    return compile(*blockRoot, previous);
  };
  auto& cache = *counterCache_;
  bool refreshContext = !local;
  if (local) {
    bool semanticChange = counterChange_.oldCount != counterChange_.newCount;
    for (qsizetype i = 0; !semanticChange && i < counterChange_.newCount; ++i)
      semanticChange = cache.blocks[counterChange_.first + i].semantic !=
                       cssSemanticSignature(*nodes[size_t(counterChange_.first + i)]);
    for (qsizetype i = 0; i < counterChange_.oldCount; ++i)
      cache.releaseSemantic(cache.blocks[counterChange_.first + i].semantic);
    cache.blocks.remove(counterChange_.first, counterChange_.oldCount);
    for (qsizetype i = 0; i < counterChange_.newCount; ++i) {
      const auto& node = *nodes[size_t(counterChange_.first + i)];
      cache.blocks.insert(counterChange_.first + i,
                          {node.id(), compileBlock(node), cache.retainSemantic(cssSemanticSignature(node))});
    }
    refreshContext = cache.contextual && semanticChange;
  } else {
    diag::ScopedPerfProbe compilePerf("counter.compileBlocks", themeDetailPerf());
    cache.blocks.reserve(nodes.size());
    // Top-level blocks share their host ancestry. An exact semantic shape that
    // cannot contain any counter subject needs no repeated AST walk. This is
    // only a negative discovery memo; positional selectors still compile and
    // evaluate every positive candidate independently.
    // retainSemantic (or the validated seed) interns shapes. Pointer identity
    // avoids hashing the same potentially large signature on every block.
    QSet<const char*> excludedShapes;
    for (qsizetype i = 0; i < qsizetype(nodes.size()); ++i) {
      const auto& node = *nodes[size_t(i)];
      const auto semantic = retainedSemanticFor(i);
      DocumentCounterCache::Program program;
      if (!excludedShapes.contains(semantic.constData())) {
        ++cache.candidateScans;
        if (styleTree_->mayHaveCounterSubjects(node, subjects))
          program = compileBlock(node, {}, true);
        else
          excludedShapes.insert(semantic.constData());
      }
      cache.blocks.append({node.id(), std::move(program), semantic});
    }
  }
  if (refreshContext) {
    if (local)
      for (qsizetype i = 0; i < cache.blocks.size(); ++i)
        cache.blocks[i].program = compileBlock(*nodes[size_t(i)], cache.blocks[i].program);
    auto write = compileElement(*root);
    if (write)
      write->documentRoot = true;
    DocumentCounterCache::Program host = write;
    for (auto* parent = root->parent; parent; parent = parent->parent) {
      auto next = compileElement(*parent);
      if (next && host)
        next->children.append(host);
      host = next;
    }
    cache.host = std::move(host);
  }
  {
    diag::ScopedPerfProbe evaluatePerf("counter.evaluate", themeDetailPerf());
    cache.evaluate();
  }
  counterChains_ = cache.chains;
  counterFingerprints_ = cache.fingerprints;
  counterText_ = cache.text;
  counterChange_ = {};
  return counterText_;
}

void RenderTheme::invalidateInlineViewportStyles() const {
  if (hasInlineViewportUnits())
    invalidateDocumentStyles();
}

bool RenderTheme::hasInlineViewportUnits() const {
  return styleTree_ && styleTree_->hasInlineViewportUnits();
}

void RenderTheme::invalidateDocumentStyles(TopLevelRangeChange change, const MarkdownDocument* document) const {
  localStyleChange_ =
      document && counterCache_ && counterDocument_ == document && styleTree_ && styleTree_.use_count() == 1 &&
      styleEngine_ && styleEngine_.use_count() == 1 && !counterChange_.isValid() && change.isValid() &&
      change.documentRevision == document->revision() && counterRevision_ + 1 == document->revision() &&
      change.oldCount == change.newCount && change.first + change.oldCount <= counterCache_->blocks.size() &&
      counterCache_->blocks.size() == qsizetype(document->root().children().size());
  QHash<NodeId, const MarkdownNode*> replacements;
  if (localStyleChange_)
    for (qsizetype i = 0; i < change.oldCount; ++i) {
      const auto& previous = counterCache_->blocks[change.first + i];
      const auto& current = *document->root().children()[size_t(change.first + i)];
      if (previous.semantic != cssSemanticSignature(current)) {
        localStyleChange_ = false;
        break;
      }
      replacements.insert(previous.id, &current);
    }
  if (localStyleChange_)
    localStyleChange_ = styleTree_->rebindBlocks(replacements);
  if (!change.isValid() || counterChange_.isValid()) {
    if (counterCache_) {
      semanticSeed_ = counterCache_;
      semanticDocument_ = counterDocument_;
      semanticRevision_ = counterRevision_;
    }
    counterCache_.reset();
    counterDocument_ = nullptr;
  }
  counterChange_ = change;
  if (localStyleChange_) {
    // CSS-visible topology is identical. Ancestor queries, projected fonts,
    // unchanged nodes and counter output remain valid until the local update.
    return;
  }
  nodeStyleCache_.clear();
  elementStyleCache_.clear();
  inlineStyleCache_.clear();
  pseudoStyleCache_.clear();
  counterChains_.clear();
  counterFingerprints_.clear();
  counterText_.clear();
  paintRecipeCache_.clear();
  sharedPaintRecipes_.clear();
  sharedPaintRecipeCount_ = 0;
  if (styleEngine_)
    styleEngine_->clearCache();
  prototypeFontCache_.clear();
  projectedStyleCache_.clear();
  computedFontCache_.clear();
  // The adapter views live MarkdownNodes. Rebuilding it is cheap now that it is sparse,
  // and guarantees edits/deletions cannot leave copied tags or :has results behind.
  styleTree_.reset();
}

QFont RenderTheme::headingFont(int level) const {
  return textFontForElement(QStringLiteral("h%1").arg(qBound(1, level, 6)));
}

QFont RenderTheme::codeFont() const {
  return textFontForElement(QStringLiteral("pre"));
}

QFont RenderTheme::inlineCodeFont() const {
  return textFontForElement(QStringLiteral("code"));
}

qreal RenderTheme::codeLineHeight() const {
  const auto* style = elementStyle(QStringLiteral("pre"));
  if (style && style->text.fontSizePx > 0 && style->text.lineHeight > 0)
    return scaledFont(style->text.fontSizePx * style->text.lineHeight);
  return TextFontMetrics(codeFont()).height();
}

QFont RenderTheme::mathFont() const {
  QFont font;
  if (!mathFont_.isEmpty()) {
    font.setFamilies(font_rendering::cssFamilyList(mathFont_, mathFamily(), fontAliases_));
  } else {
    font.setFamily(mathFamily());
  }
  font.setPointSizeF(scaledFont(mathSizePt_ > 0.0 ? mathSizePt_ : 12.5));
  font_rendering::configureForScreen(font);
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

QColor RenderTheme::delColor() const {
  return delColor_;
}

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
    return QColor(QStringLiteral("#0969da")); // blue
  case AlertKind::Tip:
    return QColor(QStringLiteral("#1a7f37")); // green
  case AlertKind::Important:
    return QColor(QStringLiteral("#8250df")); // purple
  case AlertKind::Warning:
    return QColor(QStringLiteral("#9a6700")); // amber
  case AlertKind::Caution:
    return QColor(QStringLiteral("#cf222e")); // red
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
  if (it == elementStyleIndex_.cend()) {
    return nullptr;
  }
  return &elementStyles_[static_cast<std::size_t>(it.value())];
}

ThemeElementBoxStyle RenderTheme::elementBoxStyle(const QString& key, const MarkdownNode* node,
                                                  qreal containingWidth) const {
  if (const ThemeElementStyle* style = node ? elementStyleForNode(*node, key) : elementStyle(key)) {
    return usedBoxForStyle(*style, containingWidth);
  }
  return {};
}

ThemeElementBoxStyle RenderTheme::usedBoxForStyle(const ThemeElementStyle& style, qreal containingWidth) const {
  ThemeElementBoxStyle out = style.box;
  const qreal cssWidth = containingWidth / (zoomPercent_ / 100.0);
  out.margin = out.marginLengths.used(out.margin, cssWidth);
  out.padding = out.paddingLengths.used(out.padding, cssWidth, true);
  out.margin = QMarginsF(scaled(out.margin.left()), scaled(out.margin.top()), scaled(out.margin.right()),
                         scaled(out.margin.bottom()));
  out.padding = QMarginsF(scaled(out.padding.left()), scaled(out.padding.top()), scaled(out.padding.right()),
                          scaled(out.padding.bottom()));
  out.borderTopWidth = scaled(out.borderTopWidth);
  out.borderRightWidth = scaled(out.borderRightWidth);
  out.borderBottomWidth = scaled(out.borderBottomWidth);
  out.borderLeftWidth = scaled(out.borderLeftWidth);
  out.borderRadius = scaled(out.borderRadius);
  for (auto* length : {&out.widthLength, &out.minWidthLength, &out.maxWidthLength, &out.heightLength,
                       &out.minHeightLength, &out.maxHeightLength})
    length->px = scaled(length->px);
  return out;
}

QColor RenderTheme::codeBlockBackgroundColor() const {
  return codeBlockBackground_.isValid() ? codeBlockBackground_ : codeBackgroundColor_;
}

QColor RenderTheme::headingAccentColor() const {
  return headingAccentColor_; // invalid → caller skips the accent bar
}

QColor RenderTheme::blockquoteBackgroundColor() const {
  return blockquoteBackground_; // invalid → caller paints no quote fill
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

qreal RenderTheme::scaled(qreal value) const {
  return value * static_cast<qreal>(zoomPercent_) / 100.0;
}

qreal RenderTheme::scaledFont(qreal value) const {
  return scaled(value) * static_cast<qreal>(fontSizePx_) / 16.0;
}

} // namespace muffin
