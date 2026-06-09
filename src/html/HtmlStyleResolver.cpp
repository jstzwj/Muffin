#include "html/HtmlStyleResolver.h"

namespace muffin::html {

HtmlStyleResolver::HtmlStyleResolver() = default;
HtmlStyleResolver::~HtmlStyleResolver() = default;

void HtmlStyleResolver::resolve(HtmlBox& root, qreal baseFontSize) {
  resolveBox(root, baseFontSize, false, QColor(), QString());
}

void HtmlStyleResolver::resolveBox(HtmlBox& box, qreal fontSize, bool inheritColor, QColor parentColor, const QString& parentFontFamily) {
  // Apply tag-based defaults first
  applyTagDefaults(box, fontSize);

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
    resolveBox(*child, fontSize, shouldInheritColor || inheritColor, effectiveColor, effectiveFontFamily);
  }
}

void HtmlStyleResolver::applyTagDefaults(HtmlBox& box, qreal fontSize) {
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
      style.backgroundColor = QColor(246, 248, 250);
      style.lineHeight = 1.45;
      break;

    case HtmlTag::BlockQuote:
      style.display = HtmlDisplay::Block;
      style.margin = QMarginsF(40, fontSize, 40, fontSize);
      style.borderWidth = QMarginsF(0, 0, 0, 3);
      style.borderColor = QColor(204, 204, 204);
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
      style.backgroundColor = QColor(255, 255, 0);
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
      style.color = QColor(36, 41, 47);
      break;

    case HtmlTag::Anchor:
      style.display = HtmlDisplay::Inline;
      style.color = QColor(6, 69, 173);
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
      style.backgroundColor = QColor(240, 240, 240);
      style.borderWidth = QMarginsF(1, 1, 1, 1);
      style.borderColor = QColor(204, 204, 204);
      style.padding = QMarginsF(8, 8, 8, 8);
      break;

    case HtmlTag::TableCell:
      style.display = HtmlDisplay::TableCell;
      style.borderWidth = QMarginsF(1, 1, 1, 1);
      style.borderColor = QColor(204, 204, 204);
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
      style.borderColor = QColor(204, 204, 204);
      style.padding = QMarginsF(2, 2, 2, 2);
      break;

    case HtmlTag::Button:
      style.display = HtmlDisplay::Inline;
      style.borderWidth = QMarginsF(1, 1, 1, 1);
      style.borderColor = QColor(153, 153, 153);
      style.backgroundColor = QColor(240, 240, 240);
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
