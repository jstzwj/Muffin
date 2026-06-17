#include "document/ImageSyntaxOps.h"

#include <QRegularExpression>
#include <QStringView>

namespace muffin::image_syntax {
namespace {

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
  // Typora-style trailing ';' (e.g. "zoom:25%;").
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
    const int labelEnd = s.indexOf(QLatin1Char(']'), 2);
    if (labelEnd < 0) {
      return img;
    }
    const int openParen = s.indexOf(QLatin1Char('('), labelEnd);
    if (openParen < 0) {
      return img;
    }
    int depth = 1;
    int closeParen = -1;
    for (int i = openParen + 1; i < s.size(); ++i) {
      const QChar ch = s.at(i);
      if (ch == QLatin1Char('(')) {
        ++depth;
      } else if (ch == QLatin1Char(')')) {
        --depth;
        if (depth == 0) {
          closeParen = i;
          break;
        }
      }
    }
    if (closeParen < 0) {
      return img;
    }

    img.syntax = Syntax::Markdown;
    img.alt = s.mid(2, labelEnd - 2);

    const QString paren = s.mid(openParen + 1, closeParen - openParen - 1).trimmed();
    // Optional title: a quoted string following the URL after whitespace.
    const int titleStart = paren.indexOf(QLatin1Char('"'));
    if (titleStart > 0) {
      const int titleEnd = paren.lastIndexOf(QLatin1Char('"'));
      if (titleEnd > titleStart) {
        img.src = paren.left(titleStart).trimmed();
        img.title = paren.mid(titleStart + 1, titleEnd - titleStart - 1);
        return img;
      }
    }
    img.src = paren;
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
    img.src = extractAttr(s, QStringLiteral("src"));
    img.alt = extractAttr(s, QStringLiteral("alt"));
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

int zoomPercent(const QString& source) {
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
  return QStringLiteral("![%1](%2)").arg(img.alt, img.src);
}

QString toHtml(const QString& source) {
  const Image img = parse(source);
  if (img.syntax != Syntax::Markdown) {
    return source;
  }
  return QStringLiteral("<img src=\"%1\" alt=\"%2\">").arg(img.src, img.alt);
}

ImgTagLocation findImgTag(QStringView source) {
  const QRegularExpressionMatch m = kImgTagRE.match(source);
  if (!m.hasMatch()) {
    return {false, 0, 0};
  }
  return {true, m.capturedStart(), m.capturedEnd()};
}

}  // namespace muffin::image_syntax
