#pragma once

#include <QColor>
#include <QFont>
#include <QMarginsF>
#include <QRectF>
#include <QString>

#include <memory>
#include <vector>

namespace muffin::html {

enum class HtmlDisplay {
  Block,
  Inline,
  InlineBlock,
  Flex,
  None,
  Table,
  TableRow,
  TableRowGroup,
  TableCell,
  ListItem,
};

enum class HtmlTag {
  Unknown,
  TextRun,
  Html,
  Head,
  Body,
  Div,
  Span,
  Paragraph,
  Heading1,
  Heading2,
  Heading3,
  Heading4,
  Heading5,
  Heading6,
  Bold,
  Italic,
  Underline,
  Strikethrough,
  Code,
  Pre,
  BlockQuote,
  Quote,
  Break,
  Hr,
  Image,
  Anchor,
  UnorderedList,
  OrderedList,
  ListItem,
  Table,
  TableHead,
  TableBody,
  TableRow,
  TableHeader,
  TableCell,
  Details,
  Summary,
  Input,
  Button,
  TextArea,
  Select,
  Option,
  Label,
  Strong,
  Em,
  Del,
  Ins,
  Mark,
  Sub,
  Sup,
  Kbd,
  Small,
  Big,
  Abbr,
  Section,
  Article,
  Header,
  Footer,
  Nav,
  Main,
  Aside,
  Figure,
  FigCaption,
  Caption,
  Script,
  Style,
  Template,
};

enum class HtmlTextDecoration : int {
  None = 0,
  Underline = 1 << 0,
  LineThrough = 1 << 1,
};

inline HtmlTextDecoration operator|(HtmlTextDecoration a, HtmlTextDecoration b) {
  return static_cast<HtmlTextDecoration>(static_cast<int>(a) | static_cast<int>(b));
}

inline HtmlTextDecoration& operator|=(HtmlTextDecoration& a, HtmlTextDecoration b) {
  a = a | b;
  return a;
}

inline bool hasDecoration(HtmlTextDecoration value, HtmlTextDecoration flag) {
  return (static_cast<int>(value) & static_cast<int>(flag)) != 0;
}

struct HtmlComputedStyle {
  HtmlDisplay display = HtmlDisplay::Block;
  QColor color;
  QColor backgroundColor;
  QFont font;
  qreal fontSize = 0;  // 0 = inherit
  int fontWeight = QFont::Normal;
  QFont::Style fontStyle = QFont::StyleNormal;
  Qt::Alignment textAlign = Qt::AlignLeft;
  HtmlTextDecoration textDecoration = HtmlTextDecoration::None;
  QMarginsF margin;
  QMarginsF padding;
  QMarginsF borderWidth;
  QColor borderColor;
  qreal width = -1;   // -1 = auto
  qreal height = -1;  // -1 = auto
  qreal lineHeight = -1;
  qreal letterSpacing = 0;
  bool visible = true;
  bool whiteSpacePre = false;
  bool fontSizeExplicit = false;
};

struct HtmlLayoutGeometry {
  qreal left = 0;
  qreal top = 0;
  qreal width = 0;
  qreal height = 0;
};

class HtmlBox {
public:
  explicit HtmlBox(HtmlTag tag);
  ~HtmlBox();

  HtmlTag tag() const;
  void setTag(HtmlTag tag);

  // Text content (TextRun only)
  QString text() const;
  void setText(QString text);

  // Image attributes
  QString src() const;
  void setSrc(QString src);
  QString alt() const;
  void setAlt(QString alt);

  // Anchor attributes
  QString href() const;
  void setHref(QString href);

  // List marker text (for <li>)
  QString listMarker() const;
  void setListMarker(QString marker);

  // Computed style
  HtmlComputedStyle& style();
  const HtmlComputedStyle& style() const;
  void setStyle(HtmlComputedStyle style);

  // Layout result
  HtmlLayoutGeometry& geometry();
  const HtmlLayoutGeometry& geometry() const;
  void setGeometry(HtmlLayoutGeometry geo);
  int textLayoutIndex() const;
  void setTextLayoutIndex(int index);
  bool ownsTextLayout() const;

  // Children
  std::vector<std::unique_ptr<HtmlBox>>& children();
  const std::vector<std::unique_ptr<HtmlBox>>& children() const;
  void addChild(std::unique_ptr<HtmlBox> child);
  HtmlBox* parent();
  const HtmlBox* parent() const;

  // Convenience queries
  bool isTextRun() const;
  bool isBlockLevel() const;
  bool isInlineLevel() const;
  bool hasTextContent() const;

  // Collect all text recursively (for inline formatting context)
  QString collectedText() const;

private:
  HtmlTag tag_;
  QString text_;
  QString src_;
  QString alt_;
  QString href_;
  QString listMarker_;
  HtmlComputedStyle style_;
  HtmlLayoutGeometry geometry_;
  int textLayoutIndex_ = -1;
  std::vector<std::unique_ptr<HtmlBox>> children_;
  HtmlBox* parent_ = nullptr;
};

// Tag classification helpers
bool isBlockTag(HtmlTag tag);
bool isInlineTag(HtmlTag tag);
bool isVoidTag(HtmlTag tag);  // self-closing elements like <br>, <hr>, <img>

}  // namespace muffin::html
