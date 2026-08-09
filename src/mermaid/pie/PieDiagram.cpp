// Native pie parser implementation. See PieDiagram.h for the frozen Langium
// grammar contract and tests/fixtures/mermaid/pie-grammar.json for the
// accept/reject oracle this must reproduce.

#include "mermaid/pie/PieDiagram.h"

#include <QChar>
#include <QString>
#include <QStringList>

#include <cmath>

namespace muffin::mermaid::pie {

PieParseError::PieParseError(const QString& message, int line)
    : std::runtime_error(message.toUtf8().constData()), line(line) {}

namespace {

// Whitespace tokens are [ \t]+ (the WHITESPACE terminal); newlines are a
// separate token handled by line splitting. Match mermaid's definition.
inline bool isWsChar(QChar c) {
  return c == QLatin1Char(' ') || c == QLatin1Char('\t');
}

int skipWs(const QString& s, int i) {
  const int n = s.size();
  while (i < n && isWsChar(s.at(i))) ++i;
  return i;
}

// Split on \r\n, \r, or \n (the NEWLINE terminal is /\r?\n/, but a bare \r is
// also accepted as a line break so CRLF/CR/LF all normalize — verified by the
// g_crlf oracle case).
QStringList splitLines(const QString& source) {
  QStringList lines;
  QString current;
  const int n = source.size();
  for (int i = 0; i < n; ++i) {
    const QChar c = source.at(i);
    if (c == QLatin1Char('\n')) {
      lines.append(current);
      current.clear();
    } else if (c == QLatin1Char('\r')) {
      lines.append(current);
      current.clear();
      if (i + 1 < n && source.at(i + 1) == QLatin1Char('\n')) ++i;
    } else {
      current.append(c);
    }
  }
  lines.append(current);
  return lines;
}

// The hidden SINGLE_LINE_COMMENT token starts at the first `%%`, including
// after an opening quote; the resulting incomplete STRING is then rejected.
QString stripInlineComment(const QString& line) {
  const int comment = line.indexOf(QLatin1String("%%"));
  return comment < 0 ? line : line.left(comment);
}

// Does `s` starting at `pos` begin with the case-sensitive keyword `kw`,
// immediately followed by whitespace, a delimiter, or end-of-string? Matches a
// Langium keyword token boundary.
bool matchKeyword(const QString& s, int pos, QLatin1String kw) {
  const int n = s.size();
  const int len = kw.size();
  if (pos + len > n) return false;
  const char* k = kw.latin1();
  for (int i = 0; i < len; ++i)
    if (s.at(pos + i).toLatin1() != k[i]) return false;
  const int after = pos + kw.size();
  if (after == n) return true;
  const QChar c = s.at(after);
  return isWsChar(c) || c == QLatin1Char(':') || c == QLatin1Char('{');
}

// JS Number.prototype.toString for the finite double range pie values live in
// (used only in the negative-value runtime message). Matches mermaid's
// `${value}` interpolation for the oracle cases (-5 -> "-5", -0.5 -> "-0.5").
QString jsNumber(double v) {
  if (std::isnan(v) || std::isinf(v)) return QString::number(v);
  QString s = QString::number(v, 'g', 17);
  // Trim a trailing ".0" from integer-valued doubles so -5.0 -> "-5".
  if (s.endsWith(QStringLiteral(".0"))) s.chop(2);
  return s;
}

// addSection: negative -> throw (BEFORE dedup); otherwise first-write-wins.
// Mirrors pieDb.addSection exactly.
void addSection(PieData& data, const QString& label, double value, int line) {
  if (value < 0) {
    throw PieParseError(
        QStringLiteral("\"%1\" has invalid value: %2. Negative values are not "
                       "allowed in pie charts. All slice values must be >= 0.")
            .arg(label, jsNumber(value)),
        line);
  }
  for (const PieSection& existing : data.sections)
    if (existing.label == label) return;  // first-write-wins
  data.sections.append(PieSection{label, value});
}

// Match the NUMBER_PIE terminal at `trimmed[pos..]`: `-?(0|[1-9][0-9]*)(\.[0-9]+)?`.
// Returns the consumed length (0 if no match). The caller rejects trailing
// content, so the (?!\.) tail of the upstream regex is enforced structurally.
int matchNumber(const QString& s, int pos) {
  const int n = s.size();
  int i = pos;
  if (i < n && s.at(i) == QLatin1Char('-')) ++i;
  if (i >= n) return 0;
  const int digitsStart = i;
  while (i < n && s.at(i) >= QLatin1Char('0') && s.at(i) <= QLatin1Char('9')) ++i;
  if (i == digitsStart) return 0;
  if (i < n && s.at(i) == QLatin1Char('.')) {
    int j = i + 1;
    if (j >= n || s.at(j) < QLatin1Char('0') || s.at(j) > QLatin1Char('9')) return 0;
    while (j < n && s.at(j) >= QLatin1Char('0') && s.at(j) <= QLatin1Char('9')) ++j;
    i = j;
  } else if (i - digitsStart > 1 && s.at(digitsStart) == QLatin1Char('0')) {
    return 0;
  }
  return i - pos;
}

// Parse a quoted STRING label starting at trimmed[0]. Appends the decoded
// content to `label` and returns the index past the closing quote. Throws on an
// unterminated string.
int parseQuotedLabel(const QString& trimmed, QString& label, int line) {
  const QChar qc = trimmed.at(0);
  const int n = trimmed.size();
  int i = 1;
  while (i < n) {
    const QChar c = trimmed.at(i);
    if (c == QLatin1Char('\\')) {
      if (i + 1 < n) {
        const QChar escaped = trimmed.at(i + 1);
        if (escaped == QLatin1Char('b')) label.append(QChar(u'\b'));
        else if (escaped == QLatin1Char('f')) label.append(QChar(u'\f'));
        else if (escaped == QLatin1Char('n')) label.append(QChar(u'\n'));
        else if (escaped == QLatin1Char('r')) label.append(QChar(u'\r'));
        else if (escaped == QLatin1Char('t')) label.append(QChar(u'\t'));
        else if (escaped == QLatin1Char('v')) label.append(QChar(u'\v'));
        else if (escaped == QLatin1Char('0')) label.append(QChar(u'\0'));
        else label.append(escaped);
        i += 2;
        continue;
      }
      throw PieParseError(QStringLiteral("unterminated string escape"), line);
    }
    if (c == qc) return i + 1;
    label.append(c);
    ++i;
  }
  throw PieParseError(QStringLiteral("unterminated string literal"), line);
}

// Parse a section line `"label" : value` (or single-quoted). Throws on malformed
// input (missing colon / value, trailing garbage) — matching the g_missing_*,
// g_string_value, g_trailing_garbage rejects.
void parseSection(PieData& data, const QString& trimmed, int line) {
  QString label;
  int i = parseQuotedLabel(trimmed, label, line);
  i = skipWs(trimmed, i);
  if (i >= trimmed.size() || trimmed.at(i) != QLatin1Char(':'))
    throw PieParseError(QStringLiteral("Expecting token of type ':' "), line);
  ++i;
  i = skipWs(trimmed, i);
  const int numLen = matchNumber(trimmed, i);
  if (numLen == 0)
    throw PieParseError(QStringLiteral("Expecting token of type 'NUMBER_PIE'"), line);
  const QString valueStr = trimmed.mid(i, numLen);
  i += numLen;
  i = skipWs(trimmed, i);
  if (i < trimmed.size())
    throw PieParseError(QStringLiteral("unexpected trailing content after value"), line);
  bool ok = false;
  const double value = valueStr.toDouble(&ok);
  if (!ok) throw PieParseError(QStringLiteral("invalid number"), line);
  addSection(data, label, value, line);
}

// Dispatch one content segment (a header tail or a body line) to the matching
// production. `trimmed` has its inline comment stripped and is whitespace-trimmed.
void processContent(PieData& data, const QString& trimmed, int line) {
  if (trimmed.isEmpty()) return;
  const QChar first = trimmed.at(0);
  // A STRING-quoted label opens a PieSection (labels must be quoted — bare
  // identifiers are a lexer error per g_unquoted_label).
  if (first == QLatin1Char('"') || first == QLatin1Char('\'')) {
    parseSection(data, trimmed, line);
    return;
  }
  // title: TITLE = 'title' then a separating [ \t] + text, or bare 'title'.
  // 'title' immediately followed by a non-space (titleX / title:foo) is NOT a
  // title directive and falls through to the reject.
  if (trimmed.startsWith(QLatin1String("title"), Qt::CaseSensitive)) {
    const int after = 5;
    if (after == trimmed.size() || isWsChar(trimmed.at(after))) {
      data.title = trimmed.mid(skipWs(trimmed, after)).trimmed();
      return;
    }
  }
  // accTitle: ACC_TITLE = 'accTitle' [ \t]* ':' text.
  if (trimmed.startsWith(QLatin1String("accTitle"), Qt::CaseSensitive)) {
    int i = skipWs(trimmed, 8);  // past "accTitle"
    if (i < trimmed.size() && trimmed.at(i) == QLatin1Char(':')) {
      data.accTitle = trimmed.mid(i + 1).trimmed();
      return;
    }
  }
  // accDescr: ACC_DESCR = 'accDescr' [ \t]* ':' text  |  'accDescr' \s* '{' text '}'.
  if (trimmed.startsWith(QLatin1String("accDescr"), Qt::CaseSensitive)) {
    int i = skipWs(trimmed, 8);  // past "accDescr"
    if (i < trimmed.size() && trimmed.at(i) == QLatin1Char(':')) {
      data.accDescr = trimmed.mid(i + 1).trimmed();
      return;
    }
    if (i < trimmed.size() && trimmed.at(i) == QLatin1Char('{')) {
      const int close = trimmed.lastIndexOf(QLatin1Char('}'));
      if (close > i && trimmed.mid(close + 1).trimmed().isEmpty()) {
        data.accDescr = trimmed.mid(i + 1, close - i - 1).trimmed();
        return;
      }
    }
  }
  throw PieParseError(QStringLiteral("unrecognized line: ") + trimmed, line);
}

QString gatherMultilineAccDescr(QString content, const QStringList& lines, int& lineIndex) {
  const QString trimmed = content.trimmed();
  if (!trimmed.startsWith(QLatin1String("accDescr"), Qt::CaseSensitive)) return content;
  int i = skipWs(trimmed, 8);
  if (i >= trimmed.size() || trimmed.at(i) != QLatin1Char('{') ||
      trimmed.indexOf(QLatin1Char('}'), i + 1) >= 0)
    return content;
  while (++lineIndex < lines.size()) {
    content += QLatin1Char('\n') + stripInlineComment(lines.at(lineIndex));
    if (content.indexOf(QLatin1Char('}'), content.indexOf(QLatin1Char('{')) + 1) >= 0)
      return content;
  }
  return content;
}

}  // namespace

PieData PieDiagram::parse(const QString& source) {
  PieData data;
  const QStringList lines = splitLines(source);

  // Skip leading blank/comment-only lines (Pie = NEWLINE* 'pie' ...).
  int idx = 0;
  while (idx < lines.size() && stripInlineComment(lines.at(idx)).trimmed().isEmpty())
    ++idx;
  if (idx >= lines.size())
    throw PieParseError(QStringLiteral("missing 'pie' header"));

  const QString header = stripInlineComment(lines.at(idx));
  int pos = skipWs(header, 0);
  if (!matchKeyword(header, pos, QLatin1String("pie")))
    throw PieParseError(QStringLiteral("missing 'pie' header"), idx + 1);
  pos += 3;
  pos = skipWs(header, pos);
  // Optional case-sensitive `showData` keyword right after `pie`.
  if (matchKeyword(header, pos, QLatin1String("showData"))) {
    data.showData = true;
    pos += 8;
    pos = skipWs(header, pos);
  }
  // The remainder of the header line is a normal content segment (title or a
  // section, matching `pie "A":1` and `pie title X`).
  QString headerContent = header.mid(pos).trimmed();
  if (!headerContent.isEmpty()) {
    headerContent = gatherMultilineAccDescr(headerContent, lines, idx);
    processContent(data, headerContent.trimmed(), idx + 1);
  }

  // Body lines.
  for (int j = idx + 1; j < lines.size(); ++j) {
    QString stripped = stripInlineComment(lines.at(j));
    stripped = gatherMultilineAccDescr(stripped, lines, j);
    processContent(data, stripped.trimmed(), j + 1);
  }
  return data;
}

double pieOriginalSum(const QVector<PieSection>& sections) {
  double sum = 0.0;
  for (const PieSection& s : sections) sum += s.value;
  return sum;
}

int pieDrawCount(const QVector<PieSection>& sections) {
  const double sum = pieOriginalSum(sections);
  if (sum <= 0.0) return 0;
  int count = 0;
  for (const PieSection& s : sections)
    if (s.value / sum * 100.0 >= 1.0) ++count;
  return count;
}

}  // namespace muffin::mermaid::pie
