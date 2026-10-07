#pragma once

#include <QDir>
#include <QString>

namespace muffin::link_syntax {

// A filesystem name is not a URL: literal %, # and ? must survive URL parsing.
inline QString localPathHref(QString path) {
  path = QDir::fromNativeSeparators(path);
  path.replace(QLatin1Char('%'), QStringLiteral("%25"));
  path.replace(QLatin1Char('#'), QStringLiteral("%23"));
  path.replace(QLatin1Char('?'), QStringLiteral("%3F"));
  return path;
}

inline QString escapedLabel(const QString& label) {
  QString escaped;
  escaped.reserve(label.size());
  const QString punctuation = QStringLiteral("\\`*_{}[]<>()!#&~");
  for (QChar c : label) {
    if (punctuation.contains(c)) escaped += QLatin1Char('\\');
    escaped += c;
  }
  return escaped;
}

// Serialize an href, preserving existing URL escapes. Angle brackets make spaces
// and parentheses safe; characters forbidden inside them are percent-encoded.
inline QString destination(QString href, bool bracketed = false) {
  href = QDir::fromNativeSeparators(href);
  href.replace(QLatin1Char('<'), QStringLiteral("%3C"));
  href.replace(QLatin1Char('>'), QStringLiteral("%3E"));
  href.replace(QLatin1Char('\\'), QStringLiteral("%5C"));
  href.replace(QLatin1Char('\n'), QStringLiteral("%0A"));
  href.replace(QLatin1Char('\r'), QStringLiteral("%0D"));
  href.replace(QLatin1Char('\t'), QStringLiteral("%09"));
  href.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
  for (QChar c : href) {
    bracketed = bracketed || c.isSpace() || c == QLatin1Char('(') || c == QLatin1Char(')');
  }
  return bracketed ? QLatin1Char('<') + href + QLatin1Char('>') : href;
}

}  // namespace muffin::link_syntax
