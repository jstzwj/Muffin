#include "html/HtmlStyleResolver.h"

#include "theme/FontRendering.h"
#include "theme/CssComputedStyleEngine.h"
#include "theme/CssThemeMapper.h"
#include "theme/DocumentStyleTree.h"
#include <QFile>
#include <functional>

namespace muffin::html {

HtmlStyleResolver::HtmlStyleResolver() = default;
HtmlStyleResolver::~HtmlStyleResolver() = default;

void HtmlStyleResolver::resolve(HtmlBox& root, qreal baseFontSize, const HtmlColorPalette& palette) {
  CssThemeSheet defaultSheet;
  if (!palette.documentStyleSheet) {
    const auto cssColor = [](const QColor& color) { return color.name(QColor::HexRgb); };
    const QString defaults = QStringLiteral(
                                 "#write { color:%1; font-size:%2px; } a { color:%3; } pre { background:%4; } "
                                 "blockquote { border-left:3px solid %5; } th,td { border:1px solid %6; } "
                                 "th { background:%7; } mark { background:%8; }")
                                 .arg(cssColor(palette.text))
                                 .arg(baseFontSize * 96.0 / 72.0)
                                 .arg(cssColor(palette.link))
                                 .arg(cssColor(palette.codeBackground))
                                 .arg(cssColor(palette.quoteBorder))
                                 .arg(cssColor(palette.tableBorder))
                                 .arg(cssColor(palette.tableHeaderBackground))
                                 .arg(cssColor(palette.highlight));
    defaultSheet.mergeIn(CssThemeParser::parse(defaults, {}));
  }
  {
    const CssComputedStyleEngine engine(muffin::documentStyleSheet(palette.documentStyleSheet ? *palette.documentStyleSheet : defaultSheet),
                                        palette.cssEnvironment);
    DocumentStyleHost host;
    CssElement fragment;
    fragment.parent = palette.documentParent ? palette.documentParent : &host.write;
    if (!palette.documentParent && palette.parentFontPx > 0)
      fragment.inlineDeclarations.push_back(
          {QStringLiteral("font-size"), QString::number(palette.parentFontPx) + QStringLiteral("px"), true});
    const qreal zoom = palette.cssZoom;
    const qreal fontScale = zoom * palette.cssEnvironment.textScale;
    std::function<void(HtmlBox&, const CssElement&, const HtmlComputedStyle*)> project;
    project = [&](HtmlBox& box, const CssElement& element, const HtmlComputedStyle* parent) {
      auto& target = box.style();
      if (&box == &root) {
        target.margin = {};
        target.padding = {};
        target.borderWidth = {};
        target.backgroundColor = {};
      }
      target.cssScale = zoom;
      // Image width/height attributes enter in CSS pixels. Normalize these
      // presentational hints before author CSS overrides them below.
      if (box.tag() == HtmlTag::Image) {
        if (target.width >= 0) target.width *= zoom;
        if (target.height >= 0) target.height *= zoom;
      }
      if (box.tag() == HtmlTag::TextRun && parent) {
        target.computed = parent->computed;
        target.computed.layout = {};
        target.computed.box = {};
        target.display = HtmlDisplay::Inline;
        target.font = parent->font;
        target.fontSize = parent->fontSize;
        target.color = parent->color;
        target.fontFamily = parent->fontFamily;
        target.lineHeight = parent->lineHeight;
        target.textDecoration = parent->textDecoration;
        target.whiteSpace = parent->whiteSpace;
        target.fontWeight = parent->fontWeight;
        target.fontStyle = parent->fontStyle;
      } else {
        const auto computed = engine.styleFor(element);
        const auto style = CssThemeMapper::projectComputedStyle(element.tag, computed);
        target.computed = style;
        target.computed.layout.scaleLengths(zoom);
        for (auto* length : {&target.computed.box.minHeightLength, &target.computed.box.maxHeightLength})
          length->px *= zoom;
        if (style.paint.color.isValid()) target.color = style.paint.color;
        if (style.paint.backgroundColor.isValid()) target.backgroundColor = style.paint.backgroundColor;
        if (!style.text.fontFamily.isEmpty()) {
          const auto families = font_rendering::cssFamilyList(
              style.text.fontFamily, font_rendering::documentFallbackTail(),
              palette.fontAliases);
          target.font.setFamilies(families);
          target.fontFamily = families.join(QStringLiteral(", "));
        }
        target.fontSize = pxToPt(computed.fontSizePx) * fontScale;
        target.font.setPointSizeF(qMax<qreal>(.001, target.fontSize));
        if (style.text.fontWeightSet) {
          target.fontWeight = style.text.fontWeight;
          target.font.setWeight(static_cast<QFont::Weight>(style.text.fontWeight));
        }
        if (style.text.italicSet) {
          target.fontStyle = style.text.italic ? QFont::StyleItalic : QFont::StyleNormal;
          target.font.setItalic(style.text.italic);
        }
        target.lineHeight = style.text.lineHeight > 0 ? computed.fontSizePx * fontScale * style.text.lineHeight : -1;
        if (style.text.alignment) target.textAlign = style.text.alignment;
        if (!target.color.isValid()) target.color = palette.text;
        const QString decoration = computed.resolvedValue(QStringLiteral("text-decoration-line")) + QLatin1Char(' ') +
                                   computed.resolvedValue(QStringLiteral("text-decoration"));
        target.textDecoration = HtmlTextDecoration::None;
        if (decoration.contains(QStringLiteral("underline"))) target.textDecoration |= HtmlTextDecoration::Underline;
        if (decoration.contains(QStringLiteral("line-through"))) target.textDecoration |= HtmlTextDecoration::LineThrough;
        target.letterSpacing = computed.length(QStringLiteral("letter-spacing")).px * zoom;
        font_rendering::configureCssFont(target.font, target.letterSpacing, style.text.wordSpacing * zoom);
        const auto scaleBox = [&](QMarginsF m) { return QMarginsF(m.left() * zoom, m.top() * zoom, m.right() * zoom, m.bottom() * zoom); };
        const auto anySide = [&](const QString& name) {
          return computed.hasProperty(name) || computed.hasProperty(name + QStringLiteral("-top")) ||
                 computed.hasProperty(name + QStringLiteral("-right")) || computed.hasProperty(name + QStringLiteral("-bottom")) ||
                 computed.hasProperty(name + QStringLiteral("-left"));
        };
        if (anySide(QStringLiteral("margin"))) {
          target.margin = scaleBox(style.box.margin);
          target.marginPercent = QMarginsF(-1, -1, -1, -1);
          target.marginLengths = style.box.marginLengths;
          for (auto& length : target.marginLengths.sides) length.px *= zoom;
        }
        if (anySide(QStringLiteral("padding"))) {
          target.padding = scaleBox(style.box.padding);
          target.paddingPercent = QMarginsF(-1, -1, -1, -1);
          target.paddingLengths = style.box.paddingLengths;
          for (auto& length : target.paddingLengths.sides) length.px *= zoom;
        }
        if (computed.hasProperty(QStringLiteral("border")) || computed.hasProperty(QStringLiteral("border-top-width")) ||
            computed.hasProperty(QStringLiteral("border-right-width")) || computed.hasProperty(QStringLiteral("border-bottom-width")) ||
            computed.hasProperty(QStringLiteral("border-left-width")) || computed.hasProperty(QStringLiteral("border-style"))) {
          target.borderWidth = scaleBox(
              QMarginsF(style.box.borderLeftWidth, style.box.borderTopWidth, style.box.borderRightWidth, style.box.borderBottomWidth));
          target.borderColor = style.box.borderTopColor;
          target.borderRadius = style.box.borderRadius * zoom;
        }
        const auto dimension = [&](const QString& property, CssLengthPercentage& length) {
          if (!computed.hasProperty(property)) return;
          length = computed.length(property);
          length.px *= zoom;
        };
        dimension(QStringLiteral("width"), target.widthLength);
        dimension(QStringLiteral("height"), target.heightLength);
        dimension(QStringLiteral("min-width"), target.minWidthLength);
        dimension(QStringLiteral("max-width"), target.maxWidthLength);
        if (computed.hasProperty(QStringLiteral("width"))) {
          target.width = -1;
          target.widthPercent = -1;
        }
        if (computed.hasProperty(QStringLiteral("height"))) target.height = -1;
        target.borderBox = style.box.borderBox;
        target.fontSizeExplicit = computed.hasProperty(QStringLiteral("font-size"));
        target.whiteSpaceExplicit = computed.hasProperty(QStringLiteral("white-space"));
        const auto zoomValue = computed.resolvedValue(QStringLiteral("zoom"));
        bool zoomOk = false;
        const auto elementZoom = (zoomValue.endsWith(QLatin1Char('%')) ? zoomValue.chopped(1) : zoomValue).toDouble(&zoomOk);
        if (zoomOk && elementZoom > 0) target.zoom = elementZoom / (zoomValue.endsWith(QLatin1Char('%')) ? 100 : 1);
        const QString display = computed.resolvedValue(QStringLiteral("display")).toLower();
        if (display == QLatin1String("none"))
          target.display = HtmlDisplay::None;
        else if (display == QLatin1String("block"))
          target.display = HtmlDisplay::Block;
        else if (display == QLatin1String("inline"))
          target.display = HtmlDisplay::Inline;
        else if (display == QLatin1String("inline-block"))
          target.display = HtmlDisplay::InlineBlock;
        else if (display == QLatin1String("flex") || display == QLatin1String("inline-flex"))
          target.display = HtmlDisplay::Flex;
        else if (display == QLatin1String("grid") || display == QLatin1String("inline-grid"))
          target.display = HtmlDisplay::Grid;
        else if (display == QLatin1String("table"))
          target.display = HtmlDisplay::Table;
        else if (display == QLatin1String("table-row-group"))
          target.display = HtmlDisplay::TableRowGroup;
        else if (display == QLatin1String("table-row"))
          target.display = HtmlDisplay::TableRow;
        else if (display == QLatin1String("table-cell"))
          target.display = HtmlDisplay::TableCell;
        else if (display == QLatin1String("list-item"))
          target.display = HtmlDisplay::ListItem;
        target.visible =
            target.display != HtmlDisplay::None && computed.resolvedValue(QStringLiteral("visibility")) != QStringLiteral("hidden");
        const auto borderStyle = computed.resolvedValue(QStringLiteral("border-top-style"));
        target.borderStyle = borderStyle == "dashed"   ? HtmlBorderStyle::Dashed
                             : borderStyle == "dotted" ? HtmlBorderStyle::Dotted
                             : borderStyle == "double" ? HtmlBorderStyle::Double
                                                       : HtmlBorderStyle::Solid;
        const QString whiteSpace = computed.resolvedValue(QStringLiteral("white-space")).toLower();
        if (whiteSpace == QLatin1String("pre"))
          target.whiteSpace = HtmlWhiteSpace::Pre;
        else if (whiteSpace == QLatin1String("pre-wrap"))
          target.whiteSpace = HtmlWhiteSpace::PreWrap;
        else if (whiteSpace == QLatin1String("normal"))
          target.whiteSpace = HtmlWhiteSpace::Normal;
        font_rendering::configureForScreen(target.font);
      }
      QVector<CssElement> children;
      children.reserve(static_cast<qsizetype>(box.children().size()));
      int childIndex = 0;
      QHash<QString, int> typeIndices;
      for (const auto& child : box.children()) {
        CssElement el;
        el.tag = child->cssTag;
        el.id = child->cssId;
        el.classes = child->cssClasses;
        el.inlineDeclarations = CssThemeParser::parseDeclarations(child->cssInlineStyle);
        el.parent = &box == &root ? element.parent : &element;
        el.childIndex = child->tag() == HtmlTag::TextRun ? -1 : childIndex++;
        if (el.childIndex >= 0) el.typeIndex = typeIndices[el.tag]++;
        children.push_back(std::move(el));
      }
      CssElement* previous = nullptr;
      for (auto& child : children) {
        if (child.childIndex < 0) continue;
        child.previousSibling = previous;
        if (previous) previous->nextSibling = &child;
        previous = &child;
      }
      for (qsizetype i = 0; i < children.size(); ++i) {
        project(*box.children()[i], children[i], &target);
      }
    };
    project(root, fragment, nullptr);
  }
}

}  // namespace muffin::html
