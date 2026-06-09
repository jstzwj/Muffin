#include "html/HtmlBox.h"

namespace muffin::html {

HtmlBox::HtmlBox(HtmlTag tag) : tag_(tag) {}

HtmlBox::~HtmlBox() = default;

HtmlTag HtmlBox::tag() const { return tag_; }
void HtmlBox::setTag(HtmlTag tag) { tag_ = tag; }

QString HtmlBox::text() const { return text_; }
void HtmlBox::setText(QString text) { text_ = std::move(text); }

QString HtmlBox::src() const { return src_; }
void HtmlBox::setSrc(QString src) { src_ = std::move(src); }

QString HtmlBox::alt() const { return alt_; }
void HtmlBox::setAlt(QString alt) { alt_ = std::move(alt); }

QString HtmlBox::href() const { return href_; }
void HtmlBox::setHref(QString href) { href_ = std::move(href); }

QString HtmlBox::listMarker() const { return listMarker_; }
void HtmlBox::setListMarker(QString marker) { listMarker_ = std::move(marker); }

int HtmlBox::listStart() const { return listStart_; }
void HtmlBox::setListStart(int start) { listStart_ = start; }
HtmlListMarkerType HtmlBox::listMarkerType() const { return listMarkerType_; }
void HtmlBox::setListMarkerType(HtmlListMarkerType type) { listMarkerType_ = type; }
bool HtmlBox::listReversed() const { return listReversed_; }
void HtmlBox::setListReversed(bool reversed) { listReversed_ = reversed; }

bool HtmlBox::detailsOpen() const { return detailsOpen_; }
void HtmlBox::setDetailsOpen(bool open) { detailsOpen_ = open; }

int HtmlBox::colSpan() const { return colSpan_; }
void HtmlBox::setColSpan(int span) { colSpan_ = qMax(1, span); }
int HtmlBox::rowSpan() const { return rowSpan_; }
void HtmlBox::setRowSpan(int span) { rowSpan_ = qMax(1, span); }

HtmlComputedStyle& HtmlBox::style() { return style_; }
const HtmlComputedStyle& HtmlBox::style() const { return style_; }
void HtmlBox::setStyle(HtmlComputedStyle style) { style_ = std::move(style); }

HtmlLayoutGeometry& HtmlBox::geometry() { return geometry_; }
const HtmlLayoutGeometry& HtmlBox::geometry() const { return geometry_; }
void HtmlBox::setGeometry(HtmlLayoutGeometry geo) { geometry_ = std::move(geo); }

int HtmlBox::textLayoutIndex() const { return textLayoutIndex_; }
void HtmlBox::setTextLayoutIndex(int index) { textLayoutIndex_ = index; }
bool HtmlBox::ownsTextLayout() const { return textLayoutIndex_ >= 0; }

std::vector<std::unique_ptr<HtmlBox>>& HtmlBox::children() { return children_; }
const std::vector<std::unique_ptr<HtmlBox>>& HtmlBox::children() const { return children_; }

void HtmlBox::addChild(std::unique_ptr<HtmlBox> child) {
  child->parent_ = this;
  children_.push_back(std::move(child));
}

HtmlBox* HtmlBox::parent() { return parent_; }
const HtmlBox* HtmlBox::parent() const { return parent_; }

bool HtmlBox::isTextRun() const { return tag_ == HtmlTag::TextRun; }
bool HtmlBox::isBlockLevel() const { return isBlockTag(tag_); }
bool HtmlBox::isInlineLevel() const { return isInlineTag(tag_); }

bool HtmlBox::hasTextContent() const {
  if (tag_ == HtmlTag::TextRun && !text_.isEmpty()) {
    return true;
  }
  for (const auto& child : children_) {
    if (child->hasTextContent()) {
      return true;
    }
  }
  return false;
}

QString HtmlBox::collectedText() const {
  if (tag_ == HtmlTag::TextRun) {
    return text_;
  }
  QString result;
  for (const auto& child : children_) {
    result += child->collectedText();
  }
  return result;
}

bool isBlockTag(HtmlTag tag) {
  switch (tag) {
    case HtmlTag::Div:
    case HtmlTag::Paragraph:
    case HtmlTag::Heading1:
    case HtmlTag::Heading2:
    case HtmlTag::Heading3:
    case HtmlTag::Heading4:
    case HtmlTag::Heading5:
    case HtmlTag::Heading6:
    case HtmlTag::Pre:
    case HtmlTag::BlockQuote:
    case HtmlTag::UnorderedList:
    case HtmlTag::OrderedList:
    case HtmlTag::ListItem:
    case HtmlTag::Table:
    case HtmlTag::TableHead:
    case HtmlTag::TableBody:
    case HtmlTag::TableRow:
    case HtmlTag::Details:
    case HtmlTag::Summary:
    case HtmlTag::Hr:
    case HtmlTag::Html:
    case HtmlTag::Body:
    case HtmlTag::Header:
    case HtmlTag::Footer:
    case HtmlTag::Section:
    case HtmlTag::Article:
    case HtmlTag::Nav:
    case HtmlTag::Main:
    case HtmlTag::Aside:
    case HtmlTag::Figure:
    case HtmlTag::FigCaption:
    case HtmlTag::Caption:
      return true;
    default:
      return false;
  }
}

bool isInlineTag(HtmlTag tag) {
  switch (tag) {
    case HtmlTag::TextRun:
    case HtmlTag::Span:
    case HtmlTag::Bold:
    case HtmlTag::Strong:
    case HtmlTag::Italic:
    case HtmlTag::Em:
    case HtmlTag::Underline:
    case HtmlTag::Strikethrough:
    case HtmlTag::Quote:
    case HtmlTag::Del:
    case HtmlTag::Ins:
    case HtmlTag::Code:
    case HtmlTag::Anchor:
    case HtmlTag::Image:
    case HtmlTag::Break:
    case HtmlTag::Mark:
    case HtmlTag::Sub:
    case HtmlTag::Sup:
    case HtmlTag::Kbd:
    case HtmlTag::Small:
    case HtmlTag::Big:
    case HtmlTag::Abbr:
    case HtmlTag::Label:
      return true;
    default:
      return false;
  }
}

bool isVoidTag(HtmlTag tag) {
  switch (tag) {
    case HtmlTag::Break:
    case HtmlTag::Hr:
    case HtmlTag::Image:
    case HtmlTag::Input:
      return true;
    default:
      return false;
  }
}

}  // namespace muffin::html
