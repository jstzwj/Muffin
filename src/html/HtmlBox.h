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

enum class HtmlWhiteSpace {
  Normal,
  Pre,
  PreWrap,
};

enum class HtmlBorderStyle {
  Solid,
  Dashed,
  Dotted,
  Double,
  None,
};

enum class HtmlListMarkerType {
  Decimal,    // 1, 2, 3
  LowerAlpha, // a, b, c
  UpperAlpha, // A, B, C
  LowerRoman, // i, ii, iii
  UpperRoman, // I, II, III
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
  HtmlBorderStyle borderStyle = HtmlBorderStyle::Solid;
  qreal borderRadius = 0;
  qreal width = -1;   // -1 = auto
  qreal height = -1;  // -1 = auto
  qreal zoom = 1.0;   // scale factor from style="zoom:N%"; 1.0 = natural size
  qreal lineHeight = -1;
  qreal letterSpacing = 0;
  bool visible = true;
  HtmlWhiteSpace whiteSpace = HtmlWhiteSpace::Normal;
  bool fontSizeExplicit = false;
  bool whiteSpaceExplicit = false;
  QString fontFamily;  // empty = inherit

  // Percentage values for properties that resolve against container width at layout time.
  // Negative = not a percentage (use the pixel value above).
  qreal widthPercent = -1;
  QMarginsF marginPercent = QMarginsF(-1, -1, -1, -1);
  QMarginsF paddingPercent = QMarginsF(-1, -1, -1, -1);
};

struct HtmlLayoutGeometry {
  qreal left = 0;
  qreal top = 0;
  qreal width = 0;
  qreal height = 0;
};

// Theme palette for the HTML rendering engine. The engine is theme-unaware by
// construction (it mimics browser-canvas defaults); this struct is the single
// seam where a host theme feeds its colours in. defaultLight() reproduces the
// historical hardcoded values, so callers that don't care (the inline-HTML
// path) keep rendering exactly as before.
struct HtmlColorPalette {
  QColor text;                   // default body text + <kbd>
  QColor background;             // <body> canvas
  QColor muted;                  // secondary text / soft borders (<button> border, placeholder alt)
  QColor link;                   // <a>
  QColor codeBackground;         // <pre>, <th>/<button> surface, <kbd> fill, image-placeholder fill
  QColor codeBorder;             // <pre>/<kbd> border, image-placeholder border
  QColor quoteBorder;            // <blockquote>
  QColor tableBorder;            // table cells / <input> / generic border / <hr>
  QColor tableHeaderBackground;  // <th>
  QColor highlight;              // <mark>

  static HtmlColorPalette defaultLight() {
    HtmlColorPalette p;
    p.text = QColor(31, 35, 40);
    p.background = QColor(255, 255, 255);
    p.muted = QColor(153, 153, 153);
    p.link = QColor(6, 69, 173);
    p.codeBackground = QColor(246, 248, 250);
    p.codeBorder = QColor(204, 204, 204);
    p.quoteBorder = QColor(204, 204, 204);
    p.tableBorder = QColor(204, 204, 204);
    p.tableHeaderBackground = QColor(240, 240, 240);
    p.highlight = QColor(255, 255, 0);
    return p;
  }
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

  // Ordered list attributes (for <ol>)
  int listStart() const;
  void setListStart(int start);
  HtmlListMarkerType listMarkerType() const;
  void setListMarkerType(HtmlListMarkerType type);
  bool listReversed() const;
  void setListReversed(bool reversed);

  // Details open state (for <details>)
  bool detailsOpen() const;
  void setDetailsOpen(bool open);

  // Table cell span attributes (for <td>/<th>)
  int colSpan() const;
  void setColSpan(int span);
  int rowSpan() const;
  void setRowSpan(int span);

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
  int listStart_ = 1;
  HtmlListMarkerType listMarkerType_ = HtmlListMarkerType::Decimal;
  bool listReversed_ = false;
  bool detailsOpen_ = false;
  int colSpan_ = 1;
  int rowSpan_ = 1;
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
