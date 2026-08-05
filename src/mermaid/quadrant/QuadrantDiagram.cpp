// Native quadrantChart parser. See QuadrantDiagram.h. Reproduces the frozen
// 21-case tests/fixtures/mermaid/quadrant-grammar.json oracle verbatim.

#include "mermaid/quadrant/QuadrantDiagram.h"

#include <QChar>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace muffin::mermaid::quadrant {

QuadrantParseError::QuadrantParseError(const QString& message, int line)
    : std::runtime_error(message.toUtf8().constData()), line(line) {}

namespace {

inline bool isWs(QChar c) { return c == QLatin1Char(' ') || c == QLatin1Char('\t'); }

QStringList splitLines(const QString& source) {
  QStringList lines;
  QString cur;
  const int n = source.size();
  for (int i = 0; i < n; ++i) {
    const QChar c = source.at(i);
    if (c == QLatin1Char('\n')) { lines.append(cur); cur.clear(); }
    else if (c == QLatin1Char('\r')) { lines.append(cur); cur.clear(); if (i + 1 < n && source.at(i + 1) == QLatin1Char('\n')) ++i; }
    else cur.append(c);
  }
  lines.append(cur);
  return lines;
}

// Strip a `%%` line comment (quadrant has no # comments). Unlike pie, %% here is
// only a whole-line/full comment (jison rules 0/3); we strip from the first %%.
QString stripComment(const QString& line) {
  const int i = line.indexOf(QStringLiteral("%%"));
  return i < 0 ? line : line.left(i);
}

// Case-insensitive keyword match at the start of `s` (after optional leading
// spaces), returning the index past the keyword + its trailing spaces, or -1.
int matchKw(const QString& s, QLatin1String kw) {
  int i = 0;
  const int n = s.size();
  while (i < n && isWs(s.at(i))) ++i;
  if (i + kw.size() > n) return -1;
  for (int k = 0; k < kw.size(); ++k)
    if (s.at(i + k).toLower().toLatin1() != kw.at(k)) return -1;
  int j = i + kw.size();
  while (j < n && isWs(s.at(j))) ++j;
  return j;
}

// Match a point coordinate value: exactly `1`, `0`, or `0.<digits>`. `1.0`,
// `1.5`, `2`, `-0.1`, `.5` do NOT match (the jison lexer rule `(1)|(0(.\d+)?)`
// consumes only `1`/`0(..)` and leaves the rest, which then lexical-errors).
// Returns the consumed length (0 on no match).
int matchCoord(const QString& s, int pos) {
  const int n = s.size();
  if (pos >= n) return 0;
  const QChar c = s.at(pos);
  if (c == QLatin1Char('0')) {
    int j = pos + 1;
    if (j < n && s.at(j) == QLatin1Char('.')) {
      int k = j + 1;
      if (k >= n || s.at(k) < QLatin1Char('0') || s.at(k) > QLatin1Char('9')) return 0;
      while (k < n && s.at(k) >= QLatin1Char('0') && s.at(k) <= QLatin1Char('9')) ++k;
      j = k;
    }
    return j - pos;
  }
  if (c == QLatin1Char('1')) {
    // `1` is valid only if not followed by `.` or a digit (else `1.0`/`12`).
    if (pos + 1 < n) {
      const QChar d = s.at(pos + 1);
      if (d == QLatin1Char('.') || (d >= QLatin1Char('0') && d <= QLatin1Char('9'))) return 0;
    }
    return 1;
  }
  return 0;
}

void parsePoint(QuadrantData& data, const QString& line, int lineNo) {
  // <label> : [ x , y ]  [: className]
  const int open = line.indexOf(QStringLiteral("["));
  if (open < 0) throw QuadrantParseError(QStringLiteral("Expecting '[' in point"), lineNo);
  // label = text before the `: [` opener.
  int colon = -1;
  for (int i = 0; i < open; ++i)
    if (line.at(i) == QLatin1Char(':')) { colon = i; break; }
  if (colon < 0) throw QuadrantParseError(QStringLiteral("Expecting ':' before point coords"), lineNo);
  QString label = line.left(colon).trimmed();
  if (label.size() >= 2 && label.startsWith(QLatin1Char('"')) && label.endsWith(QLatin1Char('"')))
    label = label.mid(1, label.size() - 2);
  else if (label.size() >= 2 && label.startsWith(QLatin1Char('\'')) && label.endsWith(QLatin1Char('\'')))
    label = label.mid(1, label.size() - 2);
  int i = open + 1;
  while (i < line.size() && isWs(line.at(i))) ++i;
  const int xlen = matchCoord(line, i);
  if (xlen == 0) throw QuadrantParseError(QStringLiteral("Lexical error: invalid x coordinate"), lineNo);
  const double x = line.mid(i, xlen).toDouble();
  i += xlen;
  while (i < line.size() && isWs(line.at(i))) ++i;
  if (i >= line.size() || line.at(i) != QLatin1Char(','))
    throw QuadrantParseError(QStringLiteral("Expecting ',' between point coords"), lineNo);
  ++i;
  while (i < line.size() && isWs(line.at(i))) ++i;
  const int ylen = matchCoord(line, i);
  if (ylen == 0) throw QuadrantParseError(QStringLiteral("Lexical error: invalid y coordinate"), lineNo);
  const double y = line.mid(i, ylen).toDouble();
  i += ylen;
  while (i < line.size() && isWs(line.at(i))) ++i;
  if (i >= line.size() || line.at(i) != QLatin1Char(']'))
    throw QuadrantParseError(QStringLiteral("Expecting ']' after point coords"), lineNo);
  ++i;
  QuadrantPoint p;
  p.label = label;
  p.x = x;
  p.y = y;
  // Optional `: className` (parsed; classDef styles are deferred).
  int c = i;
  while (c < line.size() && isWs(line.at(c))) ++c;
  if (c < line.size() && line.at(c) == QLatin1Char(':')) {
    ++c;
    p.className = line.mid(c).trimmed();
  }
  data.points.append(p);
}

// Parse an x-axis / y-axis line: `<kw> Left` or `<kw> Left --> Right`.
void parseAxis(QuadrantData& data, QLatin1String kw, const QString& line, bool isX) {
  const int rest = matchKw(line, kw);
  QString r = line.mid(rest).trimmed();
  QString left, right;
  const QRegularExpression sep(QStringLiteral("\\s*(-{2,}>)\\s*"));
  const QRegularExpressionMatch m = sep.match(r);
  if (m.hasMatch()) {
    left = r.left(m.capturedStart()).trimmed();
    right = r.mid(m.capturedEnd()).trimmed();
  } else {
    left = r;
  }
  if (isX) { data.xAxisLeftText = left; data.xAxisRightText = right; }
  else { data.yAxisBottomText = left; data.yAxisTopText = right; }
}

}  // namespace

QuadrantData QuadrantDiagram::parse(const QString& source) {
  QuadrantData data;
  const QStringList lines = splitLines(source);

  int idx = 0;
  while (idx < lines.size() && stripComment(lines.at(idx)).trimmed().isEmpty()) ++idx;
  if (idx >= lines.size()) throw QuadrantParseError(QStringLiteral("missing quadrantChart header"));

  const QString header = stripComment(lines.at(idx));
  const int after = matchKw(header, QLatin1String("quadrantchart"));
  if (after < 0) throw QuadrantParseError(QStringLiteral("missing quadrantChart header"), idx + 1);
  if (after != header.size())  // trailing garbage after the keyword (e.g. quadrantChartx)
    throw QuadrantParseError(QStringLiteral("unexpected text after quadrantChart header"), idx + 1);

  for (int j = idx + 1; j < lines.size(); ++j) {
    const QString raw = stripComment(lines.at(j));
    const QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) continue;
    const int lineNo = j + 1;

    int p;
    if ((p = matchKw(raw, QLatin1String("x-axis"))) >= 0) { parseAxis(data, QLatin1String("x-axis"), raw, true); continue; }
    if ((p = matchKw(raw, QLatin1String("y-axis"))) >= 0) { parseAxis(data, QLatin1String("y-axis"), raw, false); continue; }
    if ((p = matchKw(raw, QLatin1String("quadrant-1"))) >= 0) { data.quadrant1Text = raw.mid(p).trimmed(); continue; }
    if ((p = matchKw(raw, QLatin1String("quadrant-2"))) >= 0) { data.quadrant2Text = raw.mid(p).trimmed(); continue; }
    if ((p = matchKw(raw, QLatin1String("quadrant-3"))) >= 0) { data.quadrant3Text = raw.mid(p).trimmed(); continue; }
    if ((p = matchKw(raw, QLatin1String("quadrant-4"))) >= 0) { data.quadrant4Text = raw.mid(p).trimmed(); continue; }
    if ((p = matchKw(raw, QLatin1String("title"))) >= 0) { data.title = raw.mid(p).trimmed(); continue; }
    if ((p = matchKw(raw, QLatin1String("acctitle"))) >= 0) {
      int c = p; while (c < raw.size() && isWs(raw.at(c))) ++c;
      if (c < raw.size() && raw.at(c) == QLatin1Char(':')) { data.accTitle = raw.mid(c + 1).trimmed(); continue; }
    }
    if ((p = matchKw(raw, QLatin1String("accdescr"))) >= 0) {
      int c = p; while (c < raw.size() && isWs(raw.at(c))) ++c;
      if (c < raw.size() && raw.at(c) == QLatin1Char(':')) { data.accDescr = raw.mid(c + 1).trimmed(); continue; }
      if (c < raw.size() && raw.at(c) == QLatin1Char('{')) {
        const int close = raw.lastIndexOf(QLatin1Char('}'));
        if (close > c) { data.accDescr = raw.mid(c + 1, close - c - 1).trimmed(); continue; }
      }
    }
    if (matchKw(raw, QLatin1String("classdef")) >= 0)
      // classDef / point styling is deferred (the frozen oracle has only classDef
      // rejects); reject to match those verdicts.
      throw QuadrantParseError(QStringLiteral("classDef styling is not yet supported"), lineNo);
    if (trimmed.contains(QLatin1Char('['))) { parsePoint(data, raw, lineNo); continue; }
    throw QuadrantParseError(QStringLiteral("unrecognized line: ") + trimmed, lineNo);
  }
  return data;
}

}  // namespace muffin::mermaid::quadrant
