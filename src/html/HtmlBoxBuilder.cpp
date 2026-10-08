#include "html/HtmlBoxBuilder.h"
#include "html/HtmlBox.h"
#include "html/HtmlParser.h"
#include "theme/CssValueParser.h"

#include <lexbor/dom/interfaces/document.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/dom/interfaces/text.h>
#include <lexbor/html/html.h>

namespace muffin::html {
namespace {

bool parseCssLength(QString value, qreal referenceFontSize, qreal& out) {
  value = value.trimmed().toLower();
  if (referenceFontSize <= 0) {
    referenceFontSize = 16.0;
  }
  bool ok = false;
  qreal parsed = 0;
  if (value.endsWith(QLatin1String("px"))) {
    parsed = value.chopped(2).toDouble(&ok);
  } else if (value.endsWith(QLatin1String("em"))) {
    parsed = value.chopped(2).toDouble(&ok) * referenceFontSize;
  } else if (value.endsWith(QLatin1String("pt"))) {
    parsed = value.chopped(2).toDouble(&ok) * 1.333;
  } else if (value.endsWith(QLatin1String("%"))) {
    parsed = value.chopped(1).toDouble(&ok) * referenceFontSize / 100.0;
  } else {
    parsed = value.toDouble(&ok);
  }
  if (!ok) {
    return false;
  }
  out = parsed;
  return true;
}

qreal styleFontReference(const HtmlComputedStyle& style) { return style.fontSize > 0 ? style.fontSize : 16.0; }

}  // namespace

HtmlBoxBuilder::HtmlBoxBuilder() = default;
HtmlBoxBuilder::~HtmlBoxBuilder() = default;

std::unique_ptr<HtmlBox> HtmlBoxBuilder::build(const HtmlDocument& document) {
  if (!document.valid()) {
    return nullptr;
  }

  auto* doc = static_cast<lxb_html_document_t*>(document.document());
  auto* body = lxb_html_document_body_element(doc);
  if (!body) {
    // No body — try to build from the document element
    auto* docEl = lxb_dom_document_element(lxb_dom_interface_document(doc));
    if (!docEl) {
      return nullptr;
    }
    auto root = std::make_unique<HtmlBox>(HtmlTag::Body);
    buildChildren(*root, lxb_dom_interface_node(docEl));
    assignListMarkers(*root);
    return root;
  }

  auto root = std::make_unique<HtmlBox>(HtmlTag::Body);
  buildChildren(*root, lxb_dom_interface_node(body));
  assignListMarkers(*root);
  return root;
}

void HtmlBoxBuilder::buildChildren(HtmlBox& parent, void* parentNode) {
  auto* node = static_cast<lxb_dom_node_t*>(parentNode);
  auto* child = node->first_child;

  while (child) {
    auto box = buildNode(child);
    if (box) {
      parent.addChild(std::move(box));
    }
    child = child->next;
  }
}

std::unique_ptr<HtmlBox> HtmlBoxBuilder::buildNode(void* nodePtr) {
  auto* node = static_cast<lxb_dom_node_t*>(nodePtr);

  if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
    return buildTextNode(node);
  }

  if (node->type != LXB_DOM_NODE_TYPE_ELEMENT) {
    return nullptr;
  }

  HtmlTag tag = mapTag(node);
  if (shouldSkipTag(tag)) {
    return nullptr;
  }

  auto box = std::make_unique<HtmlBox>(tag);

  // Extract inline style attribute if present
  auto* element = lxb_dom_interface_element(node);
  if (element) {
    size_t tagLength = 0;
    const auto* local = lxb_dom_element_local_name(element, &tagLength);
    box->cssTag = QString::fromUtf8(reinterpret_cast<const char*>(local), static_cast<int>(tagLength));
    const auto attribute = [&](const char* name, size_t length) {
      size_t valueLength = 0;
      const auto* value = lxb_dom_element_get_attribute(element, reinterpret_cast<const lxb_char_t*>(name), length, &valueLength);
      return value ? QString::fromUtf8(reinterpret_cast<const char*>(value), static_cast<int>(valueLength)) : QString();
    };
    box->cssId = attribute("id", 2);
    box->cssClasses = attribute("class", 5).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    size_t styleLen = 0;
    const lxb_char_t* styleAttr = lxb_dom_element_get_attribute(
        element, reinterpret_cast<const lxb_char_t*>("style"), 5, &styleLen);
    if (styleAttr && styleLen > 0) {
      box->cssInlineStyle = QString::fromUtf8(reinterpret_cast<const char*>(styleAttr), static_cast<int>(styleLen));
    }

    // Extract src for images
    if (tag == HtmlTag::Image) {
      size_t len = 0;
      const lxb_char_t* val = lxb_dom_element_get_attribute(
          element, reinterpret_cast<const lxb_char_t*>("src"), 3, &len);
      if (val && len > 0) {
        box->setSrc(QString::fromUtf8(reinterpret_cast<const char*>(val), static_cast<int>(len)));
      }
      val = lxb_dom_element_get_attribute(
          element, reinterpret_cast<const lxb_char_t*>("alt"), 3, &len);
      if (val && len > 0) {
        box->setAlt(QString::fromUtf8(reinterpret_cast<const char*>(val), static_cast<int>(len)));
      }
      val = lxb_dom_element_get_attribute(
          element, reinterpret_cast<const lxb_char_t*>("width"), 5, &len);
      if (val && len > 0) {
        qreal width = 0;
        if (parseCssLength(QString::fromUtf8(reinterpret_cast<const char*>(val), static_cast<int>(len)), styleFontReference(box->style()), width) && width >= 0) {
          box->style().width = width;
        }
      }
      val = lxb_dom_element_get_attribute(
          element, reinterpret_cast<const lxb_char_t*>("height"), 6, &len);
      if (val && len > 0) {
        qreal height = 0;
        if (parseCssLength(QString::fromUtf8(reinterpret_cast<const char*>(val), static_cast<int>(len)), styleFontReference(box->style()), height) && height >= 0) {
          box->style().height = height;
        }
      }
    }

    // Extract href for anchors
    if (tag == HtmlTag::Anchor) {
      size_t len = 0;
      const lxb_char_t* val = lxb_dom_element_get_attribute(
          element, reinterpret_cast<const lxb_char_t*>("href"), 4, &len);
      if (val && len > 0) {
        box->setHref(QString::fromUtf8(reinterpret_cast<const char*>(val), static_cast<int>(len)));
      }
    }

    // Extract ordered list attributes
    if (tag == HtmlTag::OrderedList) {
      size_t len = 0;
      const lxb_char_t* val = lxb_dom_element_get_attribute(
          element, reinterpret_cast<const lxb_char_t*>("start"), 5, &len);
      if (val && len > 0) {
        bool ok = false;
        int start = QString::fromUtf8(reinterpret_cast<const char*>(val), static_cast<int>(len)).toInt(&ok);
        if (ok && start > 0) {
          box->setListStart(start);
        }
      }
      val = lxb_dom_element_get_attribute(
          element, reinterpret_cast<const lxb_char_t*>("type"), 4, &len);
      if (val && len == 1) {
        const char ch = static_cast<char>(*val);
        switch (ch) {
          case 'a': box->setListMarkerType(HtmlListMarkerType::LowerAlpha); break;
          case 'A': box->setListMarkerType(HtmlListMarkerType::UpperAlpha); break;
          case 'i': box->setListMarkerType(HtmlListMarkerType::LowerRoman); break;
          case 'I': box->setListMarkerType(HtmlListMarkerType::UpperRoman); break;
          default: break;
        }
      }
      val = lxb_dom_element_get_attribute(
          element, reinterpret_cast<const lxb_char_t*>("reversed"), 8, &len);
      if (val || lxb_dom_element_has_attribute(element,
          reinterpret_cast<const lxb_char_t*>("reversed"), 8)) {
        box->setListReversed(true);
      }
    }

    // Extract details open attribute
    if (tag == HtmlTag::Details) {
      if (lxb_dom_element_has_attribute(element,
          reinterpret_cast<const lxb_char_t*>("open"), 4)) {
        box->setDetailsOpen(true);
      }
    }

    // Extract table cell span attributes
    if (tag == HtmlTag::TableCell || tag == HtmlTag::TableHeader) {
      size_t len = 0;
      const lxb_char_t* val = lxb_dom_element_get_attribute(
          element, reinterpret_cast<const lxb_char_t*>("colspan"), 7, &len);
      if (val && len > 0) {
        bool ok = false;
        int span = QString::fromUtf8(reinterpret_cast<const char*>(val), static_cast<int>(len)).toInt(&ok);
        if (ok && span > 0) {
          box->setColSpan(span);
        }
      }
      val = lxb_dom_element_get_attribute(
          element, reinterpret_cast<const lxb_char_t*>("rowspan"), 7, &len);
      if (val && len > 0) {
        bool ok = false;
        int span = QString::fromUtf8(reinterpret_cast<const char*>(val), static_cast<int>(len)).toInt(&ok);
        if (ok && span > 0) {
          box->setRowSpan(span);
        }
      }
    }
  }

  // Recurse into children (void elements have no children)
  if (!isVoidTag(tag)) {
    buildChildren(*box, node);
  }

  return box;
}

std::unique_ptr<HtmlBox> HtmlBoxBuilder::buildTextNode(void* textNode) {
  auto* node = static_cast<lxb_dom_node_t*>(textNode);

  size_t len = 0;
  const lxb_char_t* data = lxb_dom_node_text_content(node, &len);
  if (!data || len == 0) {
    return nullptr;
  }

  QString content = QString::fromUtf8(reinterpret_cast<const char*>(data), static_cast<int>(len));

  // Skip whitespace-only text nodes that are children of block-level elements
  if (content.trimmed().isEmpty()) {
    if (node->parent && node->parent->type == LXB_DOM_NODE_TYPE_ELEMENT) {
      HtmlTag parentTag = mapTag(node->parent);
      if (parentTag == HtmlTag::Pre || isInlineTag(parentTag)) {
        auto box = std::make_unique<HtmlBox>(HtmlTag::TextRun);
        box->setText(std::move(content));
        return box;
      }
    }
    return nullptr;
  }

  auto box = std::make_unique<HtmlBox>(HtmlTag::TextRun);
  box->setText(std::move(content));
  return box;
}

HtmlTag HtmlBoxBuilder::mapTag(void* nodePtr) const {
  auto* node = static_cast<lxb_dom_node_t*>(nodePtr);

  if (node->type != LXB_DOM_NODE_TYPE_ELEMENT) {
    return HtmlTag::Unknown;
  }

  auto* element = lxb_dom_interface_element(node);
  size_t len = 0;
  const lxb_char_t* local = lxb_dom_element_local_name(element, &len);

  if (!local || len == 0) {
    return HtmlTag::Unknown;
  }

  const char* name = reinterpret_cast<const char*>(local);

  switch (len) {
    case 1:
      switch (name[0]) {
        case 'p': return HtmlTag::Paragraph;
        case 'a': return HtmlTag::Anchor;
        case 'b': return HtmlTag::Bold;
        case 'i': return HtmlTag::Italic;
        case 'u': return HtmlTag::Underline;
        case 's': return HtmlTag::Strikethrough;
        case 'q': return HtmlTag::Quote;
      }
      break;
    case 2:
      if (name[0] == 'b' && name[1] == 'r') return HtmlTag::Break;
      if (name[0] == 'e' && name[1] == 'm') return HtmlTag::Em;
      if (name[0] == 'h' && name[1] >= '1' && name[1] <= '6')
        return static_cast<HtmlTag>(static_cast<int>(HtmlTag::Heading1) + (name[1] - '1'));
      if (name[0] == 'h' && name[1] == 'r') return HtmlTag::Hr;
      if (name[0] == 'o' && name[1] == 'l') return HtmlTag::OrderedList;
      if (name[0] == 'u' && name[1] == 'l') return HtmlTag::UnorderedList;
      if (name[0] == 'l' && name[1] == 'i') return HtmlTag::ListItem;
      if (name[0] == 't' && name[1] == 'd') return HtmlTag::TableCell;
      if (name[0] == 't' && name[1] == 'h') return HtmlTag::TableHeader;
      if (name[0] == 't' && name[1] == 'r') return HtmlTag::TableRow;
      break;
    case 3:
      if (memcmp(name, "div", 3) == 0) return HtmlTag::Div;
      if (memcmp(name, "del", 3) == 0) return HtmlTag::Del;
      if (memcmp(name, "pre", 3) == 0) return HtmlTag::Pre;
      if (memcmp(name, "img", 3) == 0) return HtmlTag::Image;
      if (memcmp(name, "sub", 3) == 0) return HtmlTag::Sub;
      if (memcmp(name, "sup", 3) == 0) return HtmlTag::Sup;
      if (memcmp(name, "big", 3) == 0) return HtmlTag::Big;
      if (memcmp(name, "kbd", 3) == 0) return HtmlTag::Kbd;
      if (memcmp(name, "ins", 3) == 0) return HtmlTag::Ins;
      if (memcmp(name, "nav", 3) == 0) return HtmlTag::Nav;
      break;
    case 4:
      if (memcmp(name, "span", 4) == 0) return HtmlTag::Span;
      if (memcmp(name, "code", 4) == 0) return HtmlTag::Code;
      if (memcmp(name, "mark", 4) == 0) return HtmlTag::Mark;
      if (memcmp(name, "abbr", 4) == 0) return HtmlTag::Abbr;
      if (memcmp(name, "html", 4) == 0) return HtmlTag::Html;
      if (memcmp(name, "body", 4) == 0) return HtmlTag::Body;
      if (memcmp(name, "head", 4) == 0) return HtmlTag::Head;
      if (memcmp(name, "main", 4) == 0) return HtmlTag::Main;
      break;
    case 5:
      if (memcmp(name, "small", 5) == 0) return HtmlTag::Small;
      if (memcmp(name, "table", 5) == 0) return HtmlTag::Table;
      if (memcmp(name, "input", 5) == 0) return HtmlTag::Input;
      if (memcmp(name, "label", 5) == 0) return HtmlTag::Label;
      if (memcmp(name, "aside", 5) == 0) return HtmlTag::Aside;
      if (memcmp(name, "style", 5) == 0) return HtmlTag::Style;
      if (memcmp(name, "thead", 5) == 0) return HtmlTag::TableHead;
      if (memcmp(name, "tbody", 5) == 0) return HtmlTag::TableBody;
      break;
    case 6:
      if (memcmp(name, "script", 6) == 0) return HtmlTag::Script;
      if (memcmp(name, "strong", 6) == 0) return HtmlTag::Strong;
      if (memcmp(name, "button", 6) == 0) return HtmlTag::Button;
      if (memcmp(name, "select", 6) == 0) return HtmlTag::Select;
      if (memcmp(name, "option", 6) == 0) return HtmlTag::Option;
      if (memcmp(name, "figure", 6) == 0) return HtmlTag::Figure;
      if (memcmp(name, "header", 6) == 0) return HtmlTag::Header;
      if (memcmp(name, "footer", 6) == 0) return HtmlTag::Footer;
      break;
    case 7:
      if (memcmp(name, "details", 7) == 0) return HtmlTag::Details;
      if (memcmp(name, "summary", 7) == 0) return HtmlTag::Summary;
      if (memcmp(name, "caption", 7) == 0) return HtmlTag::Caption;
      if (memcmp(name, "section", 7) == 0) return HtmlTag::Section;
      if (memcmp(name, "article", 7) == 0) return HtmlTag::Article;
      break;
    case 8:
      if (memcmp(name, "textarea", 8) == 0) return HtmlTag::TextArea;
      if (memcmp(name, "template", 8) == 0) return HtmlTag::Template;
      break;
    case 10:
      if (memcmp(name, "blockquote", 10) == 0) return HtmlTag::BlockQuote;
      if (memcmp(name, "figcaption", 10) == 0) return HtmlTag::FigCaption;
      break;
  }

  return HtmlTag::Unknown;
}

bool HtmlBoxBuilder::shouldSkipTag(HtmlTag tag) const {
  switch (tag) {
    case HtmlTag::Head:
    case HtmlTag::Script:
    case HtmlTag::Style:
    case HtmlTag::Template:
      return true;
    default:
      return false;
  }
}

void HtmlBoxBuilder::assignListMarkers(HtmlBox& box) {
  if (box.tag() == HtmlTag::UnorderedList || box.tag() == HtmlTag::OrderedList) {
    const int itemCount = static_cast<int>(std::count_if(
        box.children().begin(), box.children().end(),
        [](const auto& c) { return c->tag() == HtmlTag::ListItem; }));
    int itemIndex = box.listStart();
    const bool reversed = box.listReversed();
    const HtmlListMarkerType markerType = box.listMarkerType();
    int step = reversed ? -1 : 1;
    if (reversed && itemCount > 0) {
      itemIndex = box.listStart() + itemCount - 1;
    }
    for (auto& child : box.children()) {
      if (child->tag() != HtmlTag::ListItem) {
        assignListMarkers(*child);
        continue;
      }
      if (box.tag() == HtmlTag::OrderedList) {
        child->setListMarker(formatOrderedMarker(itemIndex, markerType) + QLatin1Char('.'));
      } else {
        child->setListMarker(QStringLiteral("\u2022"));
      }
      itemIndex += step;
      assignListMarkers(*child);
    }
    return;
  }

  for (auto& child : box.children()) {
    assignListMarkers(*child);
  }
}

QString HtmlBoxBuilder::formatOrderedMarker(int index, HtmlListMarkerType type) {
  if (index <= 0) {
    return QString::number(index);
  }
  switch (type) {
    case HtmlListMarkerType::LowerAlpha: {
      // 1=a, 2=b, ..., 26=z, 27=aa, 28=ab, ...
      QString result;
      int n = index;
      while (n > 0) {
        n--;
        result.prepend(QChar::fromLatin1('a' + (n % 26)));
        n /= 26;
      }
      return result;
    }
    case HtmlListMarkerType::UpperAlpha: {
      QString result;
      int n = index;
      while (n > 0) {
        n--;
        result.prepend(QChar::fromLatin1('A' + (n % 26)));
        n /= 26;
      }
      return result;
    }
    case HtmlListMarkerType::LowerRoman:
      return toRoman(index).toLower();
    case HtmlListMarkerType::UpperRoman:
      return toRoman(index);
    default:
      return QString::number(index);
  }
}

QString HtmlBoxBuilder::toRoman(int n) {
  static const QPair<int, const char*> table[] = {
      {1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"},
      {100, "C"}, {90, "XC"}, {50, "L"}, {40, "XL"},
      {10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"},
  };
  QString result;
  for (const auto& [value, roman] : table) {
    while (n >= value) {
      result += QLatin1String(roman);
      n -= value;
    }
  }
  return result;
}

}  // namespace muffin::html
