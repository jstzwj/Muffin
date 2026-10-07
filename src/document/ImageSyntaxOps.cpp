#include "document/ImageSyntaxOps.h"
#include "document/LinkSyntaxOps.h"
#include "cmark-gfm.h"
#include "houdini.h"

#include <QRegularExpression>
#include <QDir>
#include <QStringView>

#include <memory>

namespace muffin::image_syntax {
namespace {

qsizetype markdownLabelEnd(QStringView source) {
  int depth = 1;
  bool escaped = false;
  for (qsizetype i = 2; i < source.size(); ++i) {
    const QChar c = source.at(i);
    if (escaped) { escaped = false; continue; }
    if (c == QLatin1Char('\\')) { escaped = true; continue; }
    if (c == QLatin1Char('[')) { ++depth; }
    if (c == QLatin1Char(']') && --depth == 0) { return i; }
  }
  return -1;
}

QString imageAlt(cmark_node* node) {
  QString text;
  for (cmark_node* child = cmark_node_first_child(node); child; child = cmark_node_next(child)) {
    if (const char* literal = cmark_node_get_literal(child)) {
      text += QString::fromUtf8(literal);
    } else if (cmark_node_get_type(child) == CMARK_NODE_SOFTBREAK ||
               cmark_node_get_type(child) == CMARK_NODE_LINEBREAK) {
      text += QLatin1Char('\n');
    } else {
      text += imageAlt(child);
    }
  }
  return text;
}

QString decodedAttribute(const QString& value) {
  const QByteArray bytes = value.toUtf8();
  cmark_strbuf decoded = CMARK_BUF_INIT(cmark_get_default_mem_allocator());
  houdini_unescape_html_f(&decoded, reinterpret_cast<const uint8_t*>(bytes.constData()),
                         static_cast<bufsize_t>(bytes.size()));
  const QString result = QString::fromUtf8(cmark_strbuf_cstr(&decoded), cmark_strbuf_len(&decoded));
  cmark_strbuf_free(&decoded);
  return result;
}

// Matches a single HTML attribute name (CSS/HTML identifier), capturing the name.
// Uses a delimited raw string (R"re(...)re") because the patterns contain `)"`,
// which would otherwise close an undelimited raw literal early.
const QRegularExpression kAttrNameRE(QStringLiteral(R"re(([\w:-]+)\s*=)re"));

// Matches a named attribute's value in double or single quotes, capturing the value.
// Mirrors the projection layer's extractHtmlAttr: src="...", src='...'.
QRegularExpression quotedAttrRE(const QString& name) {
  return QRegularExpression(
      QStringLiteral(R"re((?:^|\s)%1\s*=\s*(?:"([^"]*)"|'([^']*)'))re")
          .arg(QRegularExpression::escape(name)),
      QRegularExpression::CaseInsensitiveOption);
}

// Matches the whole style attribute including its quotes; cap 1 = "..." value, cap 2 = '...' value.
const QRegularExpression kStyleAttrRE(
    QStringLiteral(R"re(style\s*=\s*(?:"([^"]*)"|'([^']*)'))re"),
    QRegularExpression::CaseInsensitiveOption);

QString extractAttr(const QString& tag, const QString& name) {
  const auto m = quotedAttrRE(name).match(tag);
  if (!m.hasMatch()) {
    return {};
  }
  return m.captured(1).isNull() ? m.captured(2) : m.captured(1);
}

// Split a CSS style value into "prop:val" declarations, trimming each side.
QStringList parseDecls(const QString& styleValue) {
  QStringList out;
  for (const QString& raw : styleValue.split(QLatin1Char(';'))) {
    const int colon = raw.indexOf(QLatin1Char(':'));
    if (colon < 0) {
      continue;
    }
    const QString prop = raw.left(colon).trimmed();
    const QString val = raw.mid(colon + 1).trimmed();
    if (!prop.isEmpty()) {
      out.append(prop + QStringLiteral(": ") + val);
    }
  }
  return out;
}

QString assembleStyle(const QStringList& decls) {
  if (decls.isEmpty()) {
    return {};
  }
  // Trailing ';' on style declarations (e.g. "zoom:25%;").
  return decls.join(QStringLiteral("; ")) + QStringLiteral(";");
}

// Drop any "zoom: ..." declaration (case-insensitive property name).
QStringList withoutZoom(const QStringList& decls) {
  QStringList out;
  for (const QString& decl : decls) {
    const int colon = decl.indexOf(QLatin1Char(':'));
    const QString prop = colon < 0 ? decl : decl.left(colon).trimmed();
    if (prop.compare(QStringLiteral("zoom"), Qt::CaseInsensitive) != 0) {
      out.append(decl);
    }
  }
  return out;
}

// Replace (or insert) the style attribute within a single `<img ...>` tag.
// `styleValue` empty => remove the attribute entirely.
QString setStyleAttr(QString tag, const QString& styleValue) {
  QRegularExpressionMatch m = kStyleAttrRE.match(tag);
  if (m.hasMatch()) {
    if (styleValue.isEmpty()) {
      // Remove the attribute; also swallow one preceding space to avoid a double space.
      int start = m.capturedStart();
      int removeStart = start;
      if (removeStart > 0 && tag.at(removeStart - 1) == QLatin1Char(' ')) {
        --removeStart;
      }
      tag.remove(removeStart, m.capturedEnd() - removeStart);
    } else {
      tag.replace(m.capturedStart(), m.capturedLength(),
                  QStringLiteral("style=\"%1\"").arg(styleValue));
    }
    return tag;
  }
  if (styleValue.isEmpty()) {
    return tag;
  }
  // No style attribute yet: insert one immediately before the closing ">" (and ahead
  // of a self-closing slash: <img ... /> -> <img ... style="..." />).
  const int close = tag.lastIndexOf(QLatin1Char('>'));
  if (close < 0) {
    return tag;
  }
  int insertAt = close;
  if (insertAt > 0 && tag.at(insertAt - 1) == QLatin1Char('/')) {
    --insertAt;
  }
  tag.insert(insertAt, QStringLiteral(" style=\"%1\"").arg(styleValue));
  return tag;
}

// Matches a single <img ...> opening tag (img is a void element — no closer). [^>]* stops at the
// first '>', which holds for img tags whose attribute values don't contain '>'. Case-insensitive.
const QRegularExpression kImgTagRE(QStringLiteral(R"(<img\b[^>]*>)"),
                                  QRegularExpression::CaseInsensitiveOption);

}  // namespace

Image parse(const QString& source) {
  Image img;
  const QString s = source.trimmed();
  if (s.isEmpty()) {
    return img;
  }

  // Markdown image: ![alt](src) or ![alt](src "title")
  if (s.startsWith(QStringLiteral("!["))) {
    // Use the same CommonMark parser as the document: destinations can be angle
    // bracketed or escaped, and labels/titles can contain entities and escapes.
    const QByteArray bytes = s.toUtf8();
    const std::unique_ptr<cmark_node, decltype(&cmark_node_free)> document(
        cmark_parse_document(bytes.constData(), bytes.size(), CMARK_OPT_DEFAULT), cmark_node_free);
    cmark_node* paragraph = document ? cmark_node_first_child(document.get()) : nullptr;
    cmark_node* image = paragraph ? cmark_node_first_child(paragraph) : nullptr;
    if (!image || cmark_node_get_type(image) != CMARK_NODE_IMAGE) { return img; }
    img.syntax = Syntax::Markdown;
    img.alt = imageAlt(image);
    img.src = QString::fromUtf8(cmark_node_get_url(image));
    img.title = QString::fromUtf8(cmark_node_get_title(image));
    return img;
  }

  // HTML image: <img ...>
  const QStringView head = QStringView(s).left(4);
  if (head.compare(QStringLiteral("<img"), Qt::CaseInsensitive) == 0 &&
      (s.size() == 4 || s.at(4).isSpace() || s.at(4) == QLatin1Char('/') || s.at(4) == QLatin1Char('>'))) {
    if (!s.contains(QLatin1Char('>'))) {
      return img;
    }
    img.syntax = Syntax::Html;
    img.src = decodedAttribute(extractAttr(s, QStringLiteral("src")));
    img.alt = decodedAttribute(extractAttr(s, QStringLiteral("alt")));
    auto it = kAttrNameRE.globalMatch(s);
    while (it.hasNext()) {
      const QString name = it.next().captured(1).toLower();
      if (name != QLatin1String("src") && name != QLatin1String("alt")) {
        img.otherAttrs.append(name);
      }
    }
    return img;
  }

  return img;
}

SourceLocation findSource(QStringView source) {
  qsizetype left = 0;
  qsizetype right = source.size();
  while (left < right && source.at(left).isSpace()) { ++left; }
  while (right > left && source.at(right - 1).isSpace()) { --right; }
  const QStringView s = source.mid(left, right - left);

  if (s.startsWith(QStringLiteral("!["))) {
    const qsizetype labelEnd = markdownLabelEnd(s);
    const qsizetype openParen = labelEnd >= 0 && labelEnd + 1 < s.size() &&
        s.at(labelEnd + 1) == QLatin1Char('(') ? labelEnd + 1 : -1;
    if (openParen < 0) { return {}; }
    qsizetype start = openParen + 1;
    while (start < s.size() && s.at(start).isSpace()) { ++start; }
    if (start >= s.size()) { return {}; }
    if (s.at(start) == QLatin1Char('<')) {
      const qsizetype close = s.indexOf(QLatin1Char('>'), start + 1);
      return close < 0 ? SourceLocation{} : SourceLocation{true, left + start, left + close + 1};
    }

    qsizetype depth = 0;
    bool escaped = false;
    qsizetype end = start;
    for (; end < s.size(); ++end) {
      const QChar ch = s.at(end);
      if (escaped) {
        escaped = false;
        continue;
      }
      if (ch == QLatin1Char('\\')) {
        escaped = true;
      } else if (ch == QLatin1Char('(')) {
        ++depth;
      } else if (ch == QLatin1Char(')')) {
        if (depth == 0) { break; }
        --depth;
      } else if (ch.isSpace() && depth == 0) {
        break;
      }
    }
    return end > start ? SourceLocation{true, left + start, left + end} : SourceLocation{};
  }

  if (s.left(4).compare(QStringLiteral("<img"), Qt::CaseInsensitive) == 0) {
    const QRegularExpression sourceAttribute = quotedAttrRE(QStringLiteral("src"));
    const QRegularExpressionMatch match = sourceAttribute.matchView(s);
    if (!match.hasMatch()) { return {}; }
    const int capture = match.captured(1).isNull() ? 2 : 1;
    return {true, left + match.capturedStart(capture), left + match.capturedEnd(capture)};
  }
  return {};
}

QString replaceSource(const QString& source, const QString& replacement) {
  const SourceLocation location = findSource(source);
  if (!location.found) { return source; }
  QString value = QDir::fromNativeSeparators(replacement);
  if (source.trimmed().startsWith(QStringLiteral("!["))) {
    const bool wasBracketed = source.at(location.start) == QLatin1Char('<');
    value = link_syntax::destination(value, wasBracketed);
  } else {
    value.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    const QChar quote = location.start > 0 ? source.at(location.start - 1) : QLatin1Char('"');
    value.replace(quote, quote == QLatin1Char('"') ? QStringLiteral("&quot;") : QStringLiteral("&#39;"));
  }
  QString result = source;
  result.replace(location.start, location.end - location.start, value);
  return result;
}

QString markdownImage(const QString& alt, const QString& href, const QString& title) {
  QString suffix;
  if (!title.isEmpty()) {
    QString escaped = title;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    escaped.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    suffix = QStringLiteral(" \"%1\"").arg(escaped);
  }
  return QStringLiteral("![%1](%2%3)").arg(link_syntax::escapedLabel(alt), link_syntax::destination(href), suffix);
}

int zoomPercent(const QString& source) {
  // Markdown images cannot encode zoom; avoid parsing them again while rendering.
  if (source.trimmed().startsWith(QStringLiteral("!["))) { return 100; }
  const Image img = parse(source);
  if (img.syntax == Syntax::None) {
    return 100;
  }
  // Zoom only lives in the HTML <img> style attribute; markdown images have no zoom.
  if (img.syntax == Syntax::Markdown) {
    return 100;
  }
  const QString style = extractAttr(source.trimmed(), QStringLiteral("style"));
  if (style.isEmpty()) {
    return 100;
  }
  static const QRegularExpression zoomRE(QStringLiteral(R"re(zoom\s*:\s*(\d+(?:\.\d+)?)\s*%)re"),
                                        QRegularExpression::CaseInsensitiveOption);
  const auto m = zoomRE.match(style);
  if (!m.hasMatch()) {
    return 100;
  }
  const double value = m.captured(1).toDouble();
  return value <= 0.0 ? 100 : qRound(value);
}

qreal zoomFactor(const QString& source) {
  return static_cast<qreal>(zoomPercent(source)) / 100.0;
}

QString setZoom(const QString& source, int percent) {
  const Image img = parse(source);
  if (img.syntax == Syntax::None) {
    return source;
  }
  const int clamped = qBound(1, percent, 1000);

  if (clamped == 100) {
    if (img.syntax == Syntax::Markdown) {
      return source;
    }
    const QString tag = source.trimmed();
    const QString style = extractAttr(tag, QStringLiteral("style"));
    if (style.isEmpty()) {
      return source;  // nothing to remove
    }
    const QString remaining = assembleStyle(withoutZoom(parseDecls(style)));
    return setStyleAttr(tag, remaining);
  }

  // Non-100 zoom: operate on an <img> form.
  QString tag = img.syntax == Syntax::Markdown ? toHtml(source) : source.trimmed();
  const QString style = extractAttr(tag, QStringLiteral("style"));
  QStringList decls = parseDecls(style);
  decls = withoutZoom(decls);
  decls.append(QStringLiteral("zoom: %1%").arg(clamped));
  return setStyleAttr(tag, assembleStyle(decls));
}

QString toMarkdown(const QString& source) {
  const Image img = parse(source);
  if (img.syntax != Syntax::Html) {
    return source;
  }
  return markdownImage(img.alt, img.src);
}

QString toHtml(const QString& source) {
  const Image img = parse(source);
  if (img.syntax != Syntax::Markdown) {
    return source;
  }
  return QStringLiteral("<img src=\"%1\" alt=\"%2\">").arg(img.src.toHtmlEscaped(), img.alt.toHtmlEscaped());
}

ImgTagLocation findImgTag(QStringView source) {
  const QRegularExpressionMatch m = kImgTagRE.matchView(source);
  if (!m.hasMatch()) {
    return {false, 0, 0};
  }
  return {true, m.capturedStart(), m.capturedEnd()};
}

}  // namespace muffin::image_syntax
