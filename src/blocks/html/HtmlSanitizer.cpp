#include "blocks/html/HtmlSanitizer.h"

#include <lexbor/dom/interfaces/attr.h>
#include <lexbor/dom/interfaces/document.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/dom/interfaces/text.h>
#include <lexbor/html/html.h>

#include <QSet>
#include <QString>
#include <QStringList>

namespace muffin {
namespace {

// Elements whose entire subtree is dropped: active content (script, style),
// embeds/frames, document-level metadata, and SVG/MathML (which can carry
// nested event handlers and <script>). Their text is never preview text.
const QSet<QString>& dropSubtreeTags() {
  static const QSet<QString> tags = {
      QStringLiteral("script"),   QStringLiteral("style"),    QStringLiteral("iframe"),
      QStringLiteral("frame"),    QStringLiteral("frameset"), QStringLiteral("object"),
      QStringLiteral("embed"),    QStringLiteral("applet"),   QStringLiteral("param"),
      QStringLiteral("link"),     QStringLiteral("meta"),     QStringLiteral("base"),
      QStringLiteral("noscript"), QStringLiteral("template"), QStringLiteral("svg"),
      QStringLiteral("math"),     QStringLiteral("title"),    QStringLiteral("head"),
      QStringLiteral("noembed"),  QStringLiteral("xml")};
  return tags;
}

// Elements preserved verbatim. Matches and lightly extends the renderer's
// HtmlTag set. Everything else is unwrapped: the wrapper is removed but its
// children survive, so unknown-but-inert elements don't hide their text.
const QSet<QString>& safeTags() {
  static const QSet<QString> tags = {
      QStringLiteral("div"),   QStringLiteral("span"),   QStringLiteral("p"),
      QStringLiteral("h1"),    QStringLiteral("h2"),     QStringLiteral("h3"),
      QStringLiteral("h4"),    QStringLiteral("h5"),     QStringLiteral("h6"),
      QStringLiteral("b"),     QStringLiteral("i"),      QStringLiteral("u"),
      QStringLiteral("s"),     QStringLiteral("strong"), QStringLiteral("em"),
      QStringLiteral("del"),   QStringLiteral("ins"),    QStringLiteral("mark"),
      QStringLiteral("sub"),   QStringLiteral("sup"),    QStringLiteral("kbd"),
      QStringLiteral("small"), QStringLiteral("big"),    QStringLiteral("abbr"),
      QStringLiteral("code"),  QStringLiteral("pre"),    QStringLiteral("blockquote"),
      QStringLiteral("q"),     QStringLiteral("br"),     QStringLiteral("hr"),
      QStringLiteral("img"),   QStringLiteral("a"),      QStringLiteral("ul"),
      QStringLiteral("ol"),    QStringLiteral("li"),     QStringLiteral("table"),
      QStringLiteral("thead"), QStringLiteral("tbody"),  QStringLiteral("tfoot"),
      QStringLiteral("tr"),    QStringLiteral("th"),     QStringLiteral("td"),
      QStringLiteral("caption"), QStringLiteral("col"),  QStringLiteral("colgroup"),
      QStringLiteral("details"), QStringLiteral("summary"), QStringLiteral("section"),
      QStringLiteral("article"), QStringLiteral("header"), QStringLiteral("footer"),
      QStringLiteral("nav"),     QStringLiteral("main"),  QStringLiteral("aside"),
      QStringLiteral("figure"),  QStringLiteral("figcaption"), QStringLiteral("label"),
      QStringLiteral("input"),   QStringLiteral("button"), QStringLiteral("textarea"),
      QStringLiteral("select"),  QStringLiteral("option"), QStringLiteral("optgroup"),
      QStringLiteral("dl"),      QStringLiteral("dt"),    QStringLiteral("dd"),
      QStringLiteral("wbr"),     QStringLiteral("bdi"),   QStringLiteral("bdo"),
      QStringLiteral("time"),    QStringLiteral("cite"),  QStringLiteral("address")};
  return tags;
}

// Void (self-closing) elements: emitted without a closing tag.
const QSet<QString>& voidTags() {
  static const QSet<QString> tags = {QStringLiteral("br"), QStringLiteral("hr"),
      QStringLiteral("img"), QStringLiteral("input"), QStringLiteral("col"),
      QStringLiteral("wbr"), QStringLiteral("area"), QStringLiteral("source"),
      QStringLiteral("track")};
  return tags;
}

// Attributes kept on surviving elements (after the on* and URL checks).
const QSet<QString>& safeAttributes() {
  static const QSet<QString> attrs = {
      QStringLiteral("class"),   QStringLiteral("id"),      QStringLiteral("style"),
      QStringLiteral("title"),   QStringLiteral("lang"),    QStringLiteral("dir"),
      QStringLiteral("alt"),     QStringLiteral("width"),   QStringLiteral("height"),
      QStringLiteral("colspan"), QStringLiteral("rowspan"), QStringLiteral("span"),
      QStringLiteral("start"),   QStringLiteral("type"),    QStringLiteral("reversed"),
      QStringLiteral("value"),   QStringLiteral("open"),    QStringLiteral("align"),
      QStringLiteral("valign"),  QStringLiteral("color"),   QStringLiteral("size"),
      QStringLiteral("href"),    QStringLiteral("src"),     QStringLiteral("cite"),
      QStringLiteral("poster"),  QStringLiteral("background"), QStringLiteral("action"),
      QStringLiteral("formaction"), QStringLiteral("srcset"), QStringLiteral("usemap"),
      QStringLiteral("name"),    QStringLiteral("target"),  QStringLiteral("rel"),
      QStringLiteral("datetime"), QStringLiteral("abbr"),  QStringLiteral("scope"),
      QStringLiteral("headers"), QStringLiteral("summary"), QStringLiteral("nowrap"),
      QStringLiteral("cellpadding"), QStringLiteral("cellspacing"), QStringLiteral("border")};
  return attrs;
}

// Attributes whose value is a URL and must be scheme-checked.
const QSet<QString>& urlAttributes() {
  static const QSet<QString> attrs = {
      QStringLiteral("href"),     QStringLiteral("src"),   QStringLiteral("action"),
      QStringLiteral("formaction"), QStringLiteral("background"), QStringLiteral("cite"),
      QStringLiteral("poster"),   QStringLiteral("data"),  QStringLiteral("usemap"),
      QStringLiteral("srcset"),   QStringLiteral("longdesc")};
  return attrs;
}

// True if the URL value carries a scheme that is unsafe for a preview context.
// Mirrors how browsers normalize URLs (strip leading whitespace and embedded
// C0/DEL controls) so " java\nscript:" and "java\tscript:" cannot slip through.
bool hasUnsafeUrlScheme(const QString& rawValue) {
  QString collapsed;
  collapsed.reserve(rawValue.size());
  for (const QChar c : rawValue) {
    const ushort code = c.unicode();
    if (code < 0x20 || code == 0x7F) continue;  // browsers strip C0 controls + DEL
    collapsed += c;
  }
  collapsed = collapsed.trimmed().toLower();
  if (collapsed.isEmpty()) return false;
  const QChar first = collapsed.at(0);
  if (first == QLatin1Char('#') || first == QLatin1Char('/') || first == QLatin1Char('?')) {
    return false;  // fragment / absolute / relative path
  }

  // data: images are allowed; SVG data URIs can carry <script>, so block them.
  if (collapsed.startsWith(QStringLiteral("data:image/svg"))) return true;
  if (collapsed.startsWith(QStringLiteral("data:image/"))) return false;

  static const QStringList safeSchemes = {QStringLiteral("http://"), QStringLiteral("https://"),
      QStringLiteral("mailto:"), QStringLiteral("tel:"), QStringLiteral("ftp://"),
      QStringLiteral("ftps://")};
  for (const QString& scheme : safeSchemes) {
    if (collapsed.startsWith(scheme)) return false;
  }

  // Any other explicit scheme (javascript:, vbscript:, file:, blob:, about:,
  // ...) is unsafe. A ':' before any non-scheme char (e.g. "foo: bar") means
  // there is no real scheme, so treat it as a relative URL.
  const int colon = collapsed.indexOf(QLatin1Char(':'));
  if (colon <= 0) return false;
  for (int i = 0; i < colon; ++i) {
    const QChar c = collapsed.at(i);
    if (!(c.isLetterOrNumber() || c == QLatin1Char('+') || c == QLatin1Char('-') ||
          c == QLatin1Char('.'))) {
      return false;  // not a scheme token -> relative URL
    }
  }
  return true;
}

QString escapeText(QString text) {
  return text.replace(QLatin1Char('&'), QStringLiteral("&amp;"))
      .replace(QLatin1Char('<'), QStringLiteral("&lt;"))
      .replace(QLatin1Char('>'), QStringLiteral("&gt;"));
}

QString escapeAttribute(QString value) {
  return value.replace(QLatin1Char('&'), QStringLiteral("&amp;"))
      .replace(QLatin1Char('"'), QStringLiteral("&quot;"))
      .replace(QLatin1Char('<'), QStringLiteral("&lt;"))
      .replace(QLatin1Char('>'), QStringLiteral("&gt;"));
}

// CSS can smuggle script via IE expression(), url(javascript:), @import and
// -moz-binding. The app renderer does not execute these, but scrub for
// defense-in-depth in case sanitized output ever leaves the JS-free renderer.
QString scrubStyle(const QString& value) {
  const QString lower = value.toLower();
  if (lower.contains(QStringLiteral("javascript:")) ||
      lower.contains(QStringLiteral("vbscript:")) ||
      lower.contains(QStringLiteral("expression(")) ||
      lower.contains(QStringLiteral("-moz-binding")) ||
      lower.contains(QStringLiteral("@import"))) {
    return QString();
  }
  return value;
}

void sanitizeNode(lxb_dom_node_t* node, QString& out);

void sanitizeChildren(lxb_dom_node_t* parent, QString& out) {
  for (lxb_dom_node_t* child = parent->first_child; child != nullptr; child = child->next) {
    sanitizeNode(child, out);
  }
}

void sanitizeAttributes(lxb_dom_element_t* element, QString& out) {
  for (lxb_dom_attr_t* attr = lxb_dom_element_first_attribute(element); attr != nullptr;
       attr = lxb_dom_element_next_attribute(attr)) {
    size_t nameLen = 0;
    const lxb_char_t* nameData = lxb_dom_attr_qualified_name(attr, &nameLen);
    const QString name =
        QString::fromUtf8(reinterpret_cast<const char*>(nameData), static_cast<int>(nameLen))
            .toLower();

    // Drop event handlers and any attribute outside the allowlist.
    if (name.startsWith(QStringLiteral("on"))) continue;
    if (!safeAttributes().contains(name)) continue;

    size_t valueLen = 0;
    const lxb_char_t* valueData = lxb_dom_attr_value(attr, &valueLen);
    QString value =
        valueData ? QString::fromUtf8(reinterpret_cast<const char*>(valueData), static_cast<int>(valueLen))
                  : QString();

    if (name == QStringLiteral("style")) {
      value = scrubStyle(value);
      if (value.isEmpty()) continue;
    } else if (urlAttributes().contains(name)) {
      if (hasUnsafeUrlScheme(value)) value = QStringLiteral("#");
    }

    out += QLatin1Char(' ');
    out += name;
    out += QStringLiteral("=\"");
    out += escapeAttribute(value);
    out += QLatin1Char('"');
  }
}

void sanitizeNode(lxb_dom_node_t* node, QString& out) {
  if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
    size_t len = 0;
    const lxb_char_t* data = lxb_dom_node_text_content(node, &len);
    if (data && len > 0) {
      out += escapeText(QString::fromUtf8(reinterpret_cast<const char*>(data), static_cast<int>(len)));
    }
    return;
  }
  if (node->type != LXB_DOM_NODE_TYPE_ELEMENT) {
    return;  // comments, PIs, doctype — dropped
  }

  auto* element = lxb_dom_interface_element(node);
  size_t tagLen = 0;
  const lxb_char_t* tagData = lxb_dom_element_local_name(element, &tagLen);
  const QString tag =
      QString::fromUtf8(reinterpret_cast<const char*>(tagData), static_cast<int>(tagLen)).toLower();

  if (dropSubtreeTags().contains(tag)) {
    return;  // drop the entire subtree
  }
  if (!safeTags().contains(tag)) {
    sanitizeChildren(node, out);  // unwrap: keep children, drop the wrapper
    return;
  }

  out += QLatin1Char('<');
  out += tag;
  sanitizeAttributes(element, out);
  if (voidTags().contains(tag)) {
    out += QLatin1Char('>');  // void: no closing tag, no children
    return;
  }
  out += QLatin1Char('>');
  sanitizeChildren(node, out);
  out += QStringLiteral("</");
  out += tag;
  out += QLatin1Char('>');
}

}  // namespace

QString HtmlSanitizer::sanitizedPreview(QString html) const {
  lxb_html_document_t* doc = lxb_html_document_create();
  if (!doc) {
    return escapeText(html);  // cannot parse -> emit inert escaped text
  }

  const QByteArray utf8 = html.toUtf8();
  const lxb_status_t status = lxb_html_document_parse(
      doc, reinterpret_cast<const lxb_char_t*>(utf8.constData()), static_cast<size_t>(utf8.size()));
  if (status != LXB_STATUS_OK) {
    lxb_html_document_destroy(doc);
    return escapeText(html);
  }

  QString out;
  if (lxb_html_body_element_t* body = lxb_html_document_body_element(doc)) {
    sanitizeChildren(lxb_dom_interface_node(body), out);
  }

  lxb_html_document_destroy(doc);
  return out;
}

}  // namespace muffin
