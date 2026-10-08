#include "html/HtmlStyleResolver.h"

#include "theme/FontRendering.h"
#include "theme/CssComputedStyleEngine.h"
#include "theme/CssThemeMapper.h"
#include <functional>

namespace muffin::html {

HtmlStyleResolver::HtmlStyleResolver() = default;
HtmlStyleResolver::~HtmlStyleResolver() = default;

void HtmlStyleResolver::resolve(HtmlBox& root, qreal baseFontSize, const HtmlColorPalette& palette) {
  resolveBox(root, baseFontSize, false, QColor(), QString(), palette);
  if (palette.documentStyleSheet) {
    const CssComputedStyleEngine engine(*palette.documentStyleSheet, palette.cssEnvironment);
    CssElement html;
    html.tag = QStringLiteral("html");
    CssElement body;
    body.tag = QStringLiteral("body");
    body.parent = &html;
    CssElement write;
    write.id = QStringLiteral("write");
    if (palette.parentFontPx > 0)
      write.inlineDeclarations.push_back({QStringLiteral("font-size"), QString::number(palette.parentFontPx) + QStringLiteral("px"), true});
    write.parent = &body;
    const qreal zoom = palette.cssZoom;
    const qreal fontScale = zoom * palette.cssEnvironment.textScale;
    std::function<void(HtmlBox&, const CssElement&, const HtmlComputedStyle*)> project;
    project = [&](HtmlBox& box, const CssElement& element, const HtmlComputedStyle* parent) {
      auto& target = box.style();
      if (box.tag() == HtmlTag::TextRun && parent) {
        target.font = parent->font;
        target.fontSize = parent->fontSize;
        target.color = parent->color;
        target.fontFamily = parent->fontFamily;
        target.lineHeight = parent->lineHeight;
        target.fontWeight = parent->fontWeight;
        target.fontStyle = parent->fontStyle;
      } else {
        const auto computed = engine.styleFor(element);
        const auto style = CssThemeMapper::projectComputedStyle(element.tag, computed);
        if (style.paint.color.isValid()) target.color = style.paint.color;
        if (style.paint.backgroundColor.isValid()) target.backgroundColor = style.paint.backgroundColor;
        if (!style.text.fontFamily.isEmpty()) {
          QStringList families = style.text.fontFamily.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
          for (QString& family : families) {
            const QString alias = palette.fontAliases.value(family.toLower());
            if (!alias.isEmpty()) family = alias;
          }
          target.font.setFamilies(families);
          target.fontFamily = families.join(QStringLiteral(", "));
        }
        target.fontSize = pxToPt(computed.fontSizePx) * fontScale;
        target.font.setPointSizeF(target.fontSize);
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
        const auto scaleBox = [&](QMarginsF m) { return QMarginsF(m.left() * zoom, m.top() * zoom, m.right() * zoom, m.bottom() * zoom); };
        const auto anySide = [&](const QString& name) {
          return computed.hasProperty(name) || computed.hasProperty(name + QStringLiteral("-top")) ||
                 computed.hasProperty(name + QStringLiteral("-right")) || computed.hasProperty(name + QStringLiteral("-bottom")) ||
                 computed.hasProperty(name + QStringLiteral("-left"));
        };
        if (anySide(QStringLiteral("margin"))) {
          target.margin = scaleBox(style.box.margin);
          target.marginPercent = QMarginsF(-1, -1, -1, -1);
        }
        if (anySide(QStringLiteral("padding"))) {
          target.padding = scaleBox(style.box.padding);
          target.paddingPercent = QMarginsF(-1, -1, -1, -1);
        }
        if (computed.hasProperty(QStringLiteral("border")) || computed.hasProperty(QStringLiteral("border-top-width"))) {
          target.borderWidth = scaleBox(
              QMarginsF(style.box.borderLeftWidth, style.box.borderTopWidth, style.box.borderRightWidth, style.box.borderBottomWidth));
          target.borderColor = style.box.borderTopColor;
          target.borderRadius = style.box.borderRadius * zoom;
        }
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
        el.parent = &element;
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
    project(root, write, nullptr);
  }
}

void HtmlStyleResolver::resolveBox(HtmlBox& box, qreal fontSize, bool inheritColor, QColor parentColor, const QString& parentFontFamily, const HtmlColorPalette& palette) {
  // Apply tag-based defaults first
  applyTagDefaults(box, fontSize, palette);

  // If the box has inline styles, they were already parsed in HtmlBoxBuilder::extractInlineStyle.
  // Now resolve the effective font size considering inheritance.
  fontSize = resolveFontSize(box.style(), fontSize);
  box.style().fontSize = fontSize;

  // Resolve font family: inline style overrides, then inherit from parent
  QString effectiveFontFamily = parentFontFamily;
  if (box.style().fontFamily.isEmpty() && !parentFontFamily.isEmpty()) {
    box.style().fontFamily = parentFontFamily;
  }
  if (!box.style().fontFamily.isEmpty()) {
    effectiveFontFamily = box.style().fontFamily;
  }

  // Build the font object from resolved properties
  QFont& font = box.style().font;
  font.setPointSizeF(fontSize);
  font.setWeight(static_cast<QFont::Weight>(box.style().fontWeight));
  font.setStyle(box.style().fontStyle);
  if (box.style().whiteSpace != HtmlWhiteSpace::Normal) {
    font.setFamily(QStringLiteral("Courier New"));
  } else if (!effectiveFontFamily.isEmpty()) {
    font.setFamily(effectiveFontFamily);
  }
  font_rendering::configureForScreen(font);

  // Inherit color if not set
  if (!box.style().color.isValid() && inheritColor) {
    box.style().color = parentColor;
  }

  // Apply display type from tag classification
  if (box.style().display == HtmlDisplay::Block && isInlineTag(box.tag())) {
    box.style().display = HtmlDisplay::Inline;
  }

  // Recurse into children
  bool shouldInheritColor = box.style().color.isValid();
  QColor effectiveColor = shouldInheritColor ? box.style().color : parentColor;

  for (const auto& child : box.children()) {
    resolveBox(*child, fontSize, shouldInheritColor || inheritColor, effectiveColor, effectiveFontFamily, palette);
  }
}

void HtmlStyleResolver::applyTagDefaults(HtmlBox& box, qreal fontSize, const HtmlColorPalette& palette) {
  auto& style = box.style();
  const auto setDefaultFontSize = [&](qreal value) {
    if (!style.fontSizeExplicit) {
      style.fontSize = value;
    }
  };

  switch (box.tag()) {
    case HtmlTag::Body:
    case HtmlTag::Html:
      style.display = HtmlDisplay::Block;
      setDefaultFontSize(fontSize);
      if (!style.backgroundColor.isValid()) {
        style.backgroundColor = palette.background;
      }
      break;

    case HtmlTag::Paragraph:
      style.display = HtmlDisplay::Block;
      style.margin = QMarginsF(0, fontSize, 0, fontSize);
      break;

    case HtmlTag::Heading1:
      style.display = HtmlDisplay::Block;
      setDefaultFontSize(fontSize * 2.0);
      style.fontWeight = QFont::Bold;
      style.margin = QMarginsF(0, fontSize * 0.67, 0, fontSize * 0.67);
      break;
    case HtmlTag::Heading2:
      style.display = HtmlDisplay::Block;
      setDefaultFontSize(fontSize * 1.5);
      style.fontWeight = QFont::Bold;
      style.margin = QMarginsF(0, fontSize * 0.75, 0, fontSize * 0.75);
      break;
    case HtmlTag::Heading3:
      style.display = HtmlDisplay::Block;
      setDefaultFontSize(fontSize * 1.17);
      style.fontWeight = QFont::Bold;
      style.margin = QMarginsF(0, fontSize * 0.83, 0, fontSize * 0.83);
      break;
    case HtmlTag::Heading4:
      style.display = HtmlDisplay::Block;
      setDefaultFontSize(fontSize);
      style.fontWeight = QFont::Bold;
      style.margin = QMarginsF(0, fontSize * 1.12, 0, fontSize * 1.12);
      break;
    case HtmlTag::Heading5:
      style.display = HtmlDisplay::Block;
      setDefaultFontSize(fontSize * 0.83);
      style.fontWeight = QFont::Bold;
      style.margin = QMarginsF(0, fontSize * 1.5, 0, fontSize * 1.5);
      break;
    case HtmlTag::Heading6:
      style.display = HtmlDisplay::Block;
      setDefaultFontSize(fontSize * 0.67);
      style.fontWeight = QFont::Bold;
      style.margin = QMarginsF(0, fontSize * 1.67, 0, fontSize * 1.67);
      break;

    case HtmlTag::Bold:
    case HtmlTag::Strong:
      style.display = HtmlDisplay::Inline;
      style.fontWeight = QFont::Bold;
      break;

    case HtmlTag::Italic:
    case HtmlTag::Em:
      style.display = HtmlDisplay::Inline;
      style.fontStyle = QFont::StyleItalic;
      break;

    case HtmlTag::Underline:
      style.display = HtmlDisplay::Inline;
      style.textDecoration = HtmlTextDecoration::Underline;
      break;

    case HtmlTag::Strikethrough:
    case HtmlTag::Del:
      style.display = HtmlDisplay::Inline;
      style.textDecoration = HtmlTextDecoration::LineThrough;
      break;

    case HtmlTag::Quote:
      style.display = HtmlDisplay::Inline;
      break;

    case HtmlTag::Code:
      style.display = HtmlDisplay::Inline;
      setDefaultFontSize(fontSize * 0.9);
      break;

    case HtmlTag::Pre:
      style.display = HtmlDisplay::Block;
      setDefaultFontSize(fontSize * 0.9);
      if (!style.whiteSpaceExplicit) {
        style.whiteSpace = HtmlWhiteSpace::Pre;
      }
      style.margin = QMarginsF(0, fontSize, 0, fontSize);
      style.padding = QMarginsF(12, 12, 12, 12);
      style.backgroundColor = palette.codeBackground;
      style.lineHeight = 1.45;
      break;

    case HtmlTag::BlockQuote:
      style.display = HtmlDisplay::Block;
      style.margin = QMarginsF(40, fontSize, 40, fontSize);
      style.borderWidth = QMarginsF(0, 0, 0, 3);
      style.borderColor = palette.quoteBorder;
      style.padding = QMarginsF(16, 0, 0, 0);
      break;

    case HtmlTag::Hr:
      style.display = HtmlDisplay::Block;
      style.height = -1;  // auto — Yoga will derive from content/margin
      style.margin = QMarginsF(0, fontSize, 0, fontSize);
      break;

    case HtmlTag::Div:
    case HtmlTag::Section:
    case HtmlTag::Article:
    case HtmlTag::Header:
    case HtmlTag::Footer:
    case HtmlTag::Nav:
    case HtmlTag::Main:
    case HtmlTag::Aside:
      style.display = HtmlDisplay::Block;
      break;

    case HtmlTag::Span:
    case HtmlTag::Abbr:
    case HtmlTag::Ins:
    case HtmlTag::Label:
    case HtmlTag::TextRun:
      style.display = HtmlDisplay::Inline;
      break;

    case HtmlTag::Mark:
      style.display = HtmlDisplay::Inline;
      style.backgroundColor = palette.highlight;
      break;

    case HtmlTag::Sub:
      style.display = HtmlDisplay::Inline;
      setDefaultFontSize(fontSize * 0.75);
      break;

    case HtmlTag::Sup:
      style.display = HtmlDisplay::Inline;
      setDefaultFontSize(fontSize * 0.75);
      break;

    case HtmlTag::Small:
      style.display = HtmlDisplay::Inline;
      setDefaultFontSize(fontSize * 0.85);
      break;

    case HtmlTag::Big:
      style.display = HtmlDisplay::Inline;
      setDefaultFontSize(fontSize * 1.17);
      break;

    case HtmlTag::Kbd:
      style.display = HtmlDisplay::Inline;
      setDefaultFontSize(fontSize * 0.9);
      style.fontWeight = QFont::Normal;
      style.color = palette.text;
      break;

    case HtmlTag::Anchor:
      style.display = HtmlDisplay::Inline;
      style.color = palette.link;
      style.textDecoration = HtmlTextDecoration::Underline;
      break;

    case HtmlTag::Image:
      style.display = HtmlDisplay::Inline;
      break;

    case HtmlTag::Break:
      style.display = HtmlDisplay::Inline;
      break;

    case HtmlTag::UnorderedList:
      style.display = HtmlDisplay::Block;
      style.margin = QMarginsF(0, fontSize, 0, fontSize);
      style.padding = QMarginsF(40, 0, 0, 0);
      break;

    case HtmlTag::OrderedList:
      style.display = HtmlDisplay::Block;
      style.margin = QMarginsF(0, fontSize, 0, fontSize);
      style.padding = QMarginsF(40, 0, 0, 0);
      break;

    case HtmlTag::ListItem:
      style.display = HtmlDisplay::ListItem;
      style.margin = QMarginsF(0, fontSize * 0.25, 0, fontSize * 0.25);
      break;

    case HtmlTag::Table:
      style.display = HtmlDisplay::Table;
      style.borderWidth = QMarginsF(0, 0, 0, 0);
      break;

    case HtmlTag::TableHead:
    case HtmlTag::TableBody:
      style.display = HtmlDisplay::TableRowGroup;
      break;

    case HtmlTag::TableRow:
      style.display = HtmlDisplay::TableRow;
      break;

    case HtmlTag::TableHeader:
      style.display = HtmlDisplay::TableCell;
      style.fontWeight = QFont::Bold;
      style.backgroundColor = palette.tableHeaderBackground;
      style.borderWidth = QMarginsF(1, 1, 1, 1);
      style.borderColor = palette.tableBorder;
      style.padding = QMarginsF(8, 8, 8, 8);
      break;

    case HtmlTag::TableCell:
      style.display = HtmlDisplay::TableCell;
      style.borderWidth = QMarginsF(1, 1, 1, 1);
      style.borderColor = palette.tableBorder;
      style.padding = QMarginsF(8, 8, 8, 8);
      break;

    case HtmlTag::Details:
      style.display = HtmlDisplay::Block;
      style.margin = QMarginsF(0, fontSize, 0, fontSize);
      break;

    case HtmlTag::Summary:
      style.display = HtmlDisplay::Block;
      style.fontWeight = QFont::Bold;
      break;

    case HtmlTag::Figure:
      style.display = HtmlDisplay::Block;
      style.margin = QMarginsF(40, fontSize, 40, fontSize);
      break;

    case HtmlTag::FigCaption:
    case HtmlTag::Caption:
      style.display = HtmlDisplay::Block;
      setDefaultFontSize(fontSize * 0.9);
      style.textAlign = Qt::AlignCenter;
      break;

    case HtmlTag::Input:
      style.display = HtmlDisplay::Inline;
      style.borderWidth = QMarginsF(1, 1, 1, 1);
      style.borderColor = palette.tableBorder;
      style.padding = QMarginsF(2, 2, 2, 2);
      break;

    case HtmlTag::Button:
      style.display = HtmlDisplay::Inline;
      style.borderWidth = QMarginsF(1, 1, 1, 1);
      style.borderColor = palette.muted;
      style.backgroundColor = palette.codeBackground;
      style.padding = QMarginsF(4, 4, 8, 4);
      break;

    case HtmlTag::TextArea:
    case HtmlTag::Select:
    case HtmlTag::Option:
      style.display = HtmlDisplay::Inline;
      break;

    default:
      break;
  }
}

qreal HtmlStyleResolver::resolveFontSize(const HtmlComputedStyle& style, qreal parentFontSize) const {
  return style.fontSize > 0 ? style.fontSize : parentFontSize;
}

}  // namespace muffin::html
