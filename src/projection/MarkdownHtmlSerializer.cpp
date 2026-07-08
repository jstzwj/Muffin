#include "projection/MarkdownHtmlSerializer.h"

#include "blocks/html/HtmlSanitizer.h"
#include "blocks/html/HtmlUrlSafety.h"
#include "document/DefinitionBlock.h"
#include "document/InlineNode.h"
#include "document/MarkdownNode.h"
#include "document/MarkdownTypes.h"
#include "editor/EmojiDictionary.h"
#include "parser/CmarkGfmParser.h"
#include "parser/MarkdownParser.h"

#include <QChar>
#include <QHash>
#include <QSettings>
#include <QString>
#include <QStringView>
#include <QVector>

#include <vector>

namespace muffin {
namespace {

// HTML-escape text content: `&` `<` `>`. Mirrors HtmlExporter.cpp's escapeHtmlText and cmark's text
// node escaping (a `"` is NOT escaped inside text nodes).
QString escapeText(QStringView text) {
  QString out;
  out.reserve(text.size());
  for (const QChar ch : text) {
    switch (ch.unicode()) {
      case '&': out += QStringLiteral("&amp;"); break;
      case '<': out += QStringLiteral("&lt;"); break;
      case '>': out += QStringLiteral("&gt;"); break;
      default: out += ch;
    }
  }
  return out;
}

// Escape a URL for a double-quoted href/src attribute. Minimal-safe subset of cmark's
// houdini_escape_href: escapes the chars that could break out of the attribute (`&` `"` `'`) or
// break the URL (space / control chars → %XX). Not byte-identical to houdini (which percent-encodes
// a broader set incl. non-ASCII), but equally injection-safe; no test asserts exact href bytes and
// browsers handle the raw chars we leave (notably non-ASCII) when navigating.
QString escapeHref(QStringView url) {
  QString out;
  out.reserve(url.size());
  for (const QChar ch : url) {
    const ushort u = ch.unicode();
    if (u < 0x20 || ch == QLatin1Char(' ')) {
      out += QLatin1Char('%');
      const QString hex = QString::number(u, 16).toUpper();
      if (hex.size() < 2) out += QLatin1Char('0');
      out += hex;
    } else if (ch == QLatin1Char('%')) {
      out += QStringLiteral("%25");  // encode literal % so it can't smuggle a pre-encoded scheme
    } else if (ch == QLatin1Char('&')) {
      out += QStringLiteral("&amp;");
    } else if (ch == QLatin1Char('"')) {
      out += QStringLiteral("&quot;");
    } else if (ch == QLatin1Char('\'')) {
      out += QStringLiteral("&#x27;");
    } else {
      out += ch;
    }
  }
  return out;
}

// Escape a `title` attribute value: `&` `<` `>` `"` (cmark uses houdini_escape_html0 in attribute
// context for link/image titles).
QString escapeAttr(QStringView text) {
  QString out;
  out.reserve(text.size());
  for (const QChar ch : text) {
    switch (ch.unicode()) {
      case '&': out += QStringLiteral("&amp;"); break;
      case '<': out += QStringLiteral("&lt;"); break;
      case '>': out += QStringLiteral("&gt;"); break;
      case '"': out += QStringLiteral("&quot;"); break;
      default: out += ch;
    }
  }
  return out;
}

// URL safety is shared with the on-screen HtmlSanitizer via HtmlUrlSafety.h (muffin::isSafeUrl),
// so the export path and the preview can't drift — one used to block vectors the other let through.

// Decode `:shortcode:` runs to glyphs (mirrors InlineProjection's third decode-span). Called before
// escapeText since the glyph is a normal char the escaper leaves untouched.
QString decodeEmoji(const QString& text) {
  const QHash<QString, QString>& map = emojiShortcodeMap();
  QString out;
  out.reserve(text.size());
  const QStringView view(text);
  for (qsizetype i = 0; i < text.size();) {
    if (text.at(i) == QLatin1Char(':')) {
      const qsizetype len = emojiShortcodeLengthAt(view, i);
      if (len > 0) {
        const QStringView name = view.mid(i + 1, len - 2);
        const auto it = map.constFind(name.toString());
        if (it != map.constEnd()) {
          out += it.value();
        } else {
          out += text.mid(i, len);  // defensive: lengthAt should imply a known name
        }
        i += len;
        continue;
      }
    }
    out += text.at(i++);
  }
  return out;
}

QString alertKindName(AlertKind kind) {
  switch (kind) {
    case AlertKind::Note: return QStringLiteral("NOTE");
    case AlertKind::Tip: return QStringLiteral("TIP");
    case AlertKind::Important: return QStringLiteral("IMPORTANT");
    case AlertKind::Warning: return QStringLiteral("WARNING");
    case AlertKind::Caution: return QStringLiteral("CAUTION");
    default: return QString();
  }
}

QString alertKindClass(AlertKind kind) {
  switch (kind) {
    case AlertKind::Note: return QStringLiteral("note");
    case AlertKind::Tip: return QStringLiteral("tip");
    case AlertKind::Important: return QStringLiteral("important");
    case AlertKind::Warning: return QStringLiteral("warning");
    case AlertKind::Caution: return QStringLiteral("caution");
    default: return QString();
  }
}

QString alertKindTitle(AlertKind kind) {
  switch (kind) {
    case AlertKind::Note: return QStringLiteral("Note");
    case AlertKind::Tip: return QStringLiteral("Tip");
    case AlertKind::Important: return QStringLiteral("Important");
    case AlertKind::Warning: return QStringLiteral("Warning");
    case AlertKind::Caution: return QStringLiteral("Caution");
    default: return QString();
  }
}

// Returns the alignment attribute for a table column, or empty for the default (no colon).
QString alignAttr(TableAlignment a) {
  switch (a) {
    case TableAlignment::Left: return QStringLiteral(" align=\"left\"");
    case TableAlignment::Center: return QStringLiteral(" align=\"center\"");
    case TableAlignment::Right: return QStringLiteral(" align=\"right\"");
    default: return QString();
  }
}

// Removes the leading `[!KIND]` marker from a copy of a paragraph's inlines (annotateAlertKinds sets
// the kind flag but leaves the marker as literal Text). Strips the marker + one following
// SoftBreak/LineBreak (the line break separating marker from body), or the marker + one leading
// space when body shares the same line (`> [!NOTE] extra`). Returns the (possibly shortened) list.
QVector<InlineNode> stripAlertMarker(QVector<InlineNode> inlines, AlertKind kind) {
  if (inlines.isEmpty() || kind == AlertKind::None) {
    return inlines;
  }
  const QString marker = QStringLiteral("[!%1]").arg(alertKindName(kind));
  InlineNode& first = inlines.first();
  if (first.type() != InlineType::Text || !first.text().startsWith(marker, Qt::CaseInsensitive)) {
    return inlines;
  }
  const QString rest = first.text().mid(marker.size());
  if (rest.isEmpty()) {
    inlines.removeFirst();
    if (!inlines.isEmpty()) {
      const InlineType bt = inlines.first().type();
      if (bt == InlineType::SoftBreak || bt == InlineType::LineBreak) {
        inlines.removeFirst();
      }
    }
  } else {
    QString remaining = rest;
    if (remaining.startsWith(QLatin1Char(' '))) {
      remaining = remaining.mid(1);
    }
    first.setText(remaining);
  }
  return inlines;
}

// Stateful tree→HTML walker. Stateless across calls (one Serializer per serializeTree).
struct Serializer {
  QString out;
  MarkdownHtmlOptions opts;
  HtmlSanitizer sanitizer;  // sanitizes raw HTML blocks/inlines so export can't carry an XSS payload
  // Footnote definitions are hoisted to a trailing <section> (mirrors cmark), collected during the
  // block walk and emitted once at the end.
  QVector<const MarkdownNode*> footnotes;

  void emitBlocks(const std::vector<std::unique_ptr<MarkdownNode>>& children) {
    for (const auto& child : children) {
      emitBlock(*child);
    }
  }

  void emitBlock(const MarkdownNode& node) {
    switch (node.type()) {
      case BlockType::Paragraph: emitParagraph(node); break;
      case BlockType::Heading: emitHeading(node); break;
      case BlockType::BlockQuote: emitBlockQuote(node); break;
      case BlockType::List: emitList(node); break;
      case BlockType::ThematicBreak: out += QStringLiteral("<hr />\n"); break;
      case BlockType::CodeFence: emitCode(node); break;
      case BlockType::HtmlBlock: out += sanitizer.sanitizedPreview(node.literal()); break;  // XSS-sanitized
      case BlockType::MathBlock: emitMathBlock(node); break;
      case BlockType::Table: emitTable(node); break;
      case BlockType::FootnoteDefinition: footnotes.append(&node); break;  // hoist to trailing section
      case BlockType::FrontMatter:
      case BlockType::LinkDefinition: break;  // editor metadata / reference def — not rendered
      default: break;  // Document/ListItem/TableRow/TableCell handled structurally by their parent
    }
  }

  void emitParagraph(const MarkdownNode& node) {
    // Virtual empty paragraphs (inserted for editing) have no inlines; cmark never emits an empty
    // <p> from source, so skipping them matches the previous cmark-based export.
    if (node.inlines().isEmpty()) {
      return;
    }
    out += QStringLiteral("<p>");
    emitInlines(node.inlines());
    out += QStringLiteral("</p>\n");
  }

  void emitHeading(const MarkdownNode& node) {
    const int level = qBound(1, node.headingLevel(), 6);
    out += QStringLiteral("<h%1>").arg(level);
    emitInlines(node.inlines());
    out += QStringLiteral("</h%1>\n").arg(level);
  }

  void emitCode(const MarkdownNode& node) {
    out += QStringLiteral("<pre><code");
    const QString lang = node.codeLanguage();  // already the first whitespace-delimited token
    if (!lang.isEmpty()) {
      out += QStringLiteral(" class=\"language-") + escapeAttr(lang) + QStringLiteral("\"");
    }
    out += QStringLiteral(">");
    out += escapeText(node.literal());
    out += QStringLiteral("</code></pre>\n");
  }

  void emitMathBlock(const MarkdownNode& node) {
    const QString tex = node.literal();  // inner tex, $$ delimiters already stripped
    out += QStringLiteral("<div class=\"mfn-math-block\" data-tex=\"") + escapeAttr(tex) +
           QStringLiteral("\">") + escapeText(tex) + QStringLiteral("</div>\n");
  }

  void emitBlockQuote(const MarkdownNode& node) {
    const AlertKind kind = node.alertKind();
    if (kind == AlertKind::None) {
      out += QStringLiteral("<blockquote>\n");
      emitBlocks(node.children());
      out += QStringLiteral("</blockquote>\n");
      return;
    }
    out += QStringLiteral("<blockquote class=\"markdown-alert markdown-alert-") + alertKindClass(kind) +
           QStringLiteral("\">\n");
    out += QStringLiteral("<p class=\"markdown-alert-title\">") + alertKindTitle(kind) +
           QStringLiteral("</p>\n");
    bool markerStripped = false;
    for (const auto& child : node.children()) {
      if (!markerStripped && child->type() == BlockType::Paragraph) {
        markerStripped = true;
        const QVector<InlineNode> stripped = stripAlertMarker(child->inlines(), kind);
        if (!stripped.isEmpty()) {
          out += QStringLiteral("<p>");
          emitInlines(stripped);
          out += QStringLiteral("</p>\n");
        }
      } else {
        emitBlock(*child);
      }
    }
    out += QStringLiteral("</blockquote>\n");
  }

  void emitList(const MarkdownNode& node) {
    const bool ordered = node.listKind() == ListKind::Ordered;
    out += ordered ? QStringLiteral("<ol") : QStringLiteral("<ul");
    if (ordered && node.listStart() != 1) {
      out += QStringLiteral(" start=\"%1\"").arg(node.listStart());
    }
    out += QStringLiteral(">\n");
    const bool tight = node.listTight();
    for (const auto& child : node.children()) {
      emitListItem(*child, tight);
    }
    out += ordered ? QStringLiteral("</ol>\n") : QStringLiteral("</ul>\n");
  }

  void emitListItem(const MarkdownNode& item, bool tight) {
    out += QStringLiteral("<li>");
    if (item.isTaskItem()) {
      out += item.taskChecked()
                 ? QStringLiteral("<input type=\"checkbox\" checked=\"\" disabled=\"\" /> ")
                 : QStringLiteral("<input type=\"checkbox\" disabled=\"\" /> ");
    }
    for (const auto& child : item.children()) {
      // Tight list: paragraph content emitted without a <p> wrapper (cmark tight-list rule).
      if (tight && child->type() == BlockType::Paragraph) {
        emitInlines(child->inlines());
      } else {
        emitBlock(*child);
      }
    }
    out += QStringLiteral("</li>\n");
  }

  void emitTable(const MarkdownNode& node) {
    const QVector<TableAlignment> aligns = node.tableAlignments();
    out += QStringLiteral("<table>\n");
    bool theadDone = false;
    bool tbodyOpen = false;
    for (const auto& row : node.children()) {
      if (row->type() != BlockType::TableRow) {
        continue;
      }
      if (row->tableRowIsHeader() && !theadDone) {
        out += QStringLiteral("<thead>\n<tr>\n");
        emitTableRowCells(*row, aligns, true);
        out += QStringLiteral("</tr>\n</thead>\n");
        theadDone = true;
      } else {
        if (!tbodyOpen) {
          out += QStringLiteral("<tbody>\n");
          tbodyOpen = true;
        }
        out += QStringLiteral("<tr>\n");
        emitTableRowCells(*row, aligns, false);
        out += QStringLiteral("</tr>\n");
      }
    }
    if (tbodyOpen) {
      out += QStringLiteral("</tbody>\n");
    }
    out += QStringLiteral("</table>\n");
  }

  void emitTableRowCells(const MarkdownNode& row, const QVector<TableAlignment>& aligns, bool header) {
    const QLatin1String tag(header ? "th" : "td");
    int col = 0;
    for (const auto& cell : row.children()) {
      if (cell->type() != BlockType::TableCell) {
        continue;
      }
      out += QStringLiteral("<") + tag + alignAttr(aligns.value(col++)) + QStringLiteral(">");
      emitInlines(cell->inlines());
      out += QStringLiteral("</") + tag + QStringLiteral(">\n");
    }
  }

  void emitInlines(const QVector<InlineNode>& inlines) {
    // cmark splits `<b>bold</b>` into separate HtmlInline tokens (`<b>`, `</b>`) around a Text
    // node. Sanitizing each token alone would auto-close `<b>` and drop the stray `</b>`, breaking
    // the pairing. So coalesce each maximal Text|HtmlInline run: if it holds any raw HTML, rebuild
    // it as ONE HTML fragment and sanitize the whole (structure preserved, attributes still
    // scrubbed); otherwise emit the plain text nodes normally (with emoji decoding).
    int i = 0;
    while (i < inlines.size()) {
      const InlineType t = inlines.at(i).type();
      if (t != InlineType::Text && t != InlineType::HtmlInline) {
        emitInline(inlines.at(i));
        ++i;
        continue;
      }
      int j = i;
      bool hasHtml = false;
      while (j < inlines.size()) {
        const InlineType tj = inlines.at(j).type();
        if (tj != InlineType::Text && tj != InlineType::HtmlInline) { break; }
        if (tj == InlineType::HtmlInline) { hasHtml = true; }
        ++j;
      }
      if (hasHtml) {
        QString raw;
        for (int k = i; k < j; ++k) { raw += inlines.at(k).text(); }
        out += sanitizer.sanitizedPreview(raw);
      } else {
        for (int k = i; k < j; ++k) {
          out += escapeText(opts.renderEmoji ? decodeEmoji(inlines.at(k).text()) : inlines.at(k).text());
        }
      }
      i = j;
    }
  }

  void emitInline(const InlineNode& node) {
    switch (node.type()) {
      case InlineType::Text:
        out += escapeText(opts.renderEmoji ? decodeEmoji(node.text()) : node.text());
        break;
      case InlineType::SoftBreak:
        out += opts.breakOnSingleNewline ? QStringLiteral("<br />\n") : QStringLiteral("\n");
        break;
      case InlineType::LineBreak:
        out += QStringLiteral("<br />\n");
        break;
      case InlineType::Emphasis: wrapInline(QLatin1String("em"), node); break;
      case InlineType::Strong: wrapInline(QLatin1String("strong"), node); break;
      case InlineType::Strikethrough: wrapInline(QLatin1String("del"), node); break;
      case InlineType::Highlight: wrapInline(QLatin1String("mark"), node); break;
      case InlineType::Subscript: wrapInline(QLatin1String("sub"), node); break;
      case InlineType::Superscript: wrapInline(QLatin1String("sup"), node); break;
      case InlineType::Code:
        out += QStringLiteral("<code>") + escapeText(node.text()) + QStringLiteral("</code>");
        break;
      case InlineType::InlineMath: emitInlineMath(node); break;
      case InlineType::Link: emitLink(node); break;
      case InlineType::Image: emitImage(node); break;
      case InlineType::HtmlInline: out += sanitizer.sanitizedPreview(node.text()); break;  // XSS-sanitized
      case InlineType::FootnoteReference: emitFootnoteRef(node); break;
      default: out += escapeText(node.text()); break;
    }
  }

  void wrapInline(QLatin1String tag, const InlineNode& node) {
    out += QStringLiteral("<") + tag + QStringLiteral(">");
    emitInlines(node.children());
    out += QStringLiteral("</") + tag + QStringLiteral(">");
  }

  void emitInlineMath(const InlineNode& node) {
    const QString tex = node.text();  // inner tex, $ stripped
    out += QStringLiteral("<span class=\"mfn-inline-math\" data-tex=\"") + escapeAttr(tex) +
           QStringLiteral("\">") + escapeText(tex) + QStringLiteral("</span>");
  }

  void emitLink(const InlineNode& node) {
    if (!isSafeUrl(node.href(), false)) {
      emitInlines(node.children());  // unsafe scheme: emit label text only, no href
      return;
    }
    out += QStringLiteral("<a href=\"") + escapeHref(node.href()) + QStringLiteral("\"");
    if (!node.title().isEmpty()) {
      out += QStringLiteral(" title=\"") + escapeAttr(node.title()) + QStringLiteral("\"");
    }
    out += QStringLiteral(">");
    emitInlines(node.children());
    out += QStringLiteral("</a>");
  }

  void emitImage(const InlineNode& node) {
    if (!isSafeUrl(node.href(), true)) {  // href holds the src (InlineNode::image factory)
      return;  // cmark omits a dangerous image entirely
    }
    out += QStringLiteral("<img src=\"") + escapeHref(node.href()) + QStringLiteral("\" alt=\"") +
           escapeAttr(node.alt()) + QStringLiteral("\"");
    if (!node.title().isEmpty()) {
      out += QStringLiteral(" title=\"") + escapeAttr(node.title()) + QStringLiteral("\"");
    }
    out += QStringLiteral(" />");
  }

  void emitFootnoteRef(const InlineNode& node) {
    // href is "#fn:<label>" (internal anchor, always safe); text is the ordinal.
    out += QStringLiteral("<sup class=\"footnote-ref\"><a href=\"") + escapeHref(node.href()) +
           QStringLiteral("\">") + escapeText(node.text()) + QStringLiteral("</a></sup>");
  }
};

}  // namespace

QString MarkdownHtmlSerializer::serializeSource(QStringView markdown) {
  ParseOptions options;  // defaults: all GFM extensions + front matter + alertBox ON
  QSettings s;
  options.enableHighlight = s.value(QStringLiteral("markdown/highlight"), false).toBool();
  options.enableSubscript = s.value(QStringLiteral("markdown/subscript"), false).toBool();
  options.enableSuperscript = s.value(QStringLiteral("markdown/superscript"), false).toBool();
  // autoLink / math / alertBox / table / tasklist / strikethrough stay default-true: export emits
  // what the markdown SAYS, independent of the editor's strict-mode editing preferences.
  MarkdownHtmlOptions htmlOptions;
  htmlOptions.breakOnSingleNewline = s.value(QStringLiteral("markdown/breakOnSingleNewline"), true).toBool();
  htmlOptions.renderEmoji = s.value(QStringLiteral("markdown/renderEmoji"), true).toBool();

  CmarkGfmParser parser;
  ParseResult result = parser.parseDocument(markdown, options);
  if (!result.root) {
    return {};
  }
  return serializeTree(*result.root, htmlOptions);
}

QString MarkdownHtmlSerializer::serializeTree(const MarkdownNode& root, const MarkdownHtmlOptions& options) {
  Serializer s;
  s.opts = options;
  s.emitBlocks(root.children());
  if (!s.footnotes.isEmpty()) {
    s.out += QStringLiteral("<section class=\"footnotes\">\n<ol>\n");
    for (const MarkdownNode* def : s.footnotes) {
      const QString label = def->definition().label;
      s.out += QStringLiteral("<li id=\"fn:") + escapeHref(label) + QStringLiteral("\">\n");
      s.emitBlocks(def->children());
      s.out += QStringLiteral("</li>\n");
    }
    s.out += QStringLiteral("</ol>\n</section>\n");
  }
  return s.out;
}

}  // namespace muffin
