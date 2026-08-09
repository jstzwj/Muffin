#include "mermaid/radar/RadarDiagram.h"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace muffin::mermaid::radar {

RadarParseError::RadarParseError(const QString& message, int line, int column)
    : std::runtime_error(message.toUtf8().constData()),
      line(line),
      column(column) {}

namespace {

bool isInlineWs(QChar c) { return c == QLatin1Char(' ') || c == QLatin1Char('\t'); }
bool isAsciiWord(QChar c) {
  const ushort u = c.unicode();
  return (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') ||
         (u >= '0' && u <= '9') || u == '_';
}

bool reservedId(const QString& id) {
  static const QStringList reserved = {
      QStringLiteral("radar-beta"), QStringLiteral("axis"), QStringLiteral("curve"),
      QStringLiteral("showLegend"), QStringLiteral("ticks"), QStringLiteral("max"),
      QStringLiteral("min"), QStringLiteral("graticule"), QStringLiteral("circle"),
      QStringLiteral("polygon"), QStringLiteral("true"), QStringLiteral("false"),
      QStringLiteral("title"), QStringLiteral("accTitle"), QStringLiteral("accDescr")};
  return reserved.contains(id);
}

struct DetailedRef { QString axis; double value = 0.0; };
struct PendingCurve {
  QString name;
  QString label;
  QVector<double> numericEntries;
  QVector<DetailedRef> detailedEntries;
  bool detailed = false;
  int line = 0;
};

QString normalizedSingleLine(QString value) {
  static const QRegularExpression repeatedInlineWhitespace(
      QStringLiteral(R"([\t ]{2,})"));
  value = value.trimmed();
  value.replace(repeatedInlineWhitespace, QStringLiteral(" "));
  return value;
}

QString normalizedBlock(QString value) {
  static const QRegularExpression repeatedInlineWhitespace(
      QStringLiteral(R"([\t ]{2,})"));
  QStringList lines = value.split(QLatin1Char('\n'));
  QStringList normalized;
  for (QString& line : lines) {
    line = line.trimmed();
    line.replace(repeatedInlineWhitespace, QStringLiteral(" "));
    if (!line.isEmpty()) normalized.append(line);
  }
  return normalized.join(QLatin1Char('\n'));
}

class Cursor {
public:
  explicit Cursor(QString source) : text(std::move(source)) {}

  bool atEnd() const { return pos >= text.size(); }
  QChar peek() const { return atEnd() ? QChar() : text.at(pos); }
  int currentLine() const { return line; }
  int currentColumn() const {
    const qsizetype previousNewline =
        pos == 0 ? -1 : text.lastIndexOf(QLatin1Char('\n'), pos - 1);
    return int(pos - previousNewline);
  }

  void advance() {
    if (atEnd()) return;
    if (text.at(pos++) == QLatin1Char('\n')) ++line;
  }

  void inlineWs() {
    while (!atEnd() && isInlineWs(peek())) advance();
  }

  void hidden(bool includeNewlines) {
    for (;;) {
      inlineWs();
      if (!atEnd() && peek() == QLatin1Char('%') && pos + 1 < text.size() &&
          text.at(pos + 1) == QLatin1Char('%')) {
        if (pos + 2 < text.size() && text.at(pos + 2) == QLatin1Char('{')) {
          const int close = text.indexOf(QLatin1String("}%%"), pos + 3);
          if (close < 0) fail(QStringLiteral("unterminated directive"));
          while (pos < close + 3) advance();
        } else {
          while (!atEnd() && peek() != QLatin1Char('\n')) advance();
        }
        continue;
      }
      if (includeNewlines && !atEnd() && peek() == QLatin1Char('\n')) {
        advance();
        continue;
      }
      return;
    }
  }

  bool keyword(QLatin1String value) const {
    if (text.mid(pos, value.size()) != value) return false;
    const int after = pos + value.size();
    if (after >= text.size()) return true;
    const QChar next = text.at(after);
    return !isAsciiWord(next) && next != QLatin1Char('-');
  }

  void consumeKeyword(QLatin1String value) {
    if (!keyword(value)) fail(QStringLiteral("expected '") + value + QLatin1Char('\''));
    pos += value.size();
  }

  bool take(QChar c) {
    if (peek() != c) return false;
    advance();
    return true;
  }

  void require(QChar c, const QString& message) {
    if (!take(c)) fail(message);
  }

  QString id() {
    const int start = pos;
    if (atEnd() || !isAsciiWord(peek())) fail(QStringLiteral("expected radar ID"));
    advance();
    while (!atEnd() && (isAsciiWord(peek()) || peek() == QLatin1Char('-'))) advance();
    const QString result = text.mid(start, pos - start);
    if (!isAsciiWord(result.back()) || reservedId(result))
      fail(QStringLiteral("invalid radar ID: ") + result);
    return result;
  }

  QString string() {
    if (peek() != QLatin1Char('"') && peek() != QLatin1Char('\''))
      fail(QStringLiteral("expected quoted STRING"));
    const QChar quote = peek();
    advance();
    QString result;
    while (!atEnd()) {
      QChar c = peek();
      advance();
      if (c == quote) return result;
      if (c != QLatin1Char('\\')) {
        result.append(c);
        continue;
      }
      if (atEnd()) fail(QStringLiteral("unterminated string escape"));
      c = peek();
      advance();
      if (c == QLatin1Char('b')) result.append(QChar(u'\b'));
      else if (c == QLatin1Char('f')) result.append(QChar(u'\f'));
      else if (c == QLatin1Char('n')) result.append(QChar(u'\n'));
      else if (c == QLatin1Char('r')) result.append(QChar(u'\r'));
      else if (c == QLatin1Char('t')) result.append(QChar(u'\t'));
      else if (c == QLatin1Char('v')) result.append(QChar(u'\v'));
      else if (c == QLatin1Char('0')) result.append(QChar(u'\0'));
      else result.append(c);
    }
    fail(QStringLiteral("unterminated string"));
  }

  double number() {
    const int start = pos;
    while (!atEnd() && peek().unicode() >= '0' && peek().unicode() <= '9') advance();
    if (pos == start) fail(QStringLiteral("expected NUMBER"));
    if (!atEnd() && peek() == QLatin1Char('.')) {
      advance();
      const int fraction = pos;
      while (!atEnd() && peek().unicode() >= '0' && peek().unicode() <= '9') advance();
      if (pos == fraction) fail(QStringLiteral("expected digits after decimal"));
      if (!atEnd() && peek() == QLatin1Char('.')) fail(QStringLiteral("invalid NUMBER"));
    } else if (pos - start > 1 && text.at(start) == QLatin1Char('0')) {
      fail(QStringLiteral("invalid leading zero"));
    }
    bool ok = false;
    const double result = text.mid(start, pos - start).toDouble(&ok);
    if (!ok && !std::isinf(result)) fail(QStringLiteral("invalid NUMBER"));
    return result;
  }

  QString untilLineEnd() {
    const int start = pos;
    while (!atEnd() && peek() != QLatin1Char('\n') &&
           !(peek() == QLatin1Char('%') && pos + 1 < text.size() &&
             text.at(pos + 1) == QLatin1Char('%')))
      advance();
    return text.mid(start, pos - start);
  }

  QString blockText() {
    require(QLatin1Char('{'), QStringLiteral("expected '{'"));
    const int start = pos;
    while (!atEnd() && peek() != QLatin1Char('}')) advance();
    if (atEnd()) fail(QStringLiteral("unterminated accDescr block"));
    const QString result = text.mid(start, pos - start);
    advance();
    return result;
  }

  [[noreturn]] void fail(const QString& message) const {
    throw RadarParseError(message, line, currentColumn());
  }

private:
  QString text;
  int pos = 0;
  int line = 1;
};

struct NamedLabel { QString name; QString label; };

NamedLabel parseNamedLabel(Cursor& cursor) {
  NamedLabel result;
  result.name = cursor.id();
  result.label = result.name;
  cursor.inlineWs();
  if (cursor.take(QLatin1Char('['))) {
    cursor.inlineWs();
    result.label = cursor.string();
    cursor.inlineWs();
    cursor.require(QLatin1Char(']'), QStringLiteral("expected ']'"));
  }
  return result;
}

void parseAxes(Cursor& cursor, RadarData& data) {
  cursor.consumeKeyword(QLatin1String("axis"));
  cursor.inlineWs();
  for (;;) {
    const NamedLabel axis = parseNamedLabel(cursor);
    data.axes.append({axis.name, axis.label});
    cursor.inlineWs();
    if (!cursor.take(QLatin1Char(','))) return;
    cursor.inlineWs();  // NEWLINE is deliberately not accepted here.
  }
}

PendingCurve parseOneCurve(Cursor& cursor) {
  PendingCurve curve;
  curve.line = cursor.currentLine();
  const NamedLabel head = parseNamedLabel(cursor);
  curve.name = head.name;
  curve.label = head.label;
  cursor.inlineWs();
  cursor.require(QLatin1Char('{'), QStringLiteral("curve requires '{'"));
  cursor.hidden(true);
  if (cursor.peek() == QLatin1Char('}')) cursor.fail(QStringLiteral("curve entries cannot be empty"));
  curve.detailed = !(cursor.peek().unicode() >= '0' && cursor.peek().unicode() <= '9');
  for (;;) {
    if (curve.detailed) {
      const QString axis = cursor.id();
      cursor.inlineWs();
      cursor.take(QLatin1Char(':'));
      cursor.inlineWs();
      curve.detailedEntries.append({axis, cursor.number()});
    } else {
      curve.numericEntries.append(cursor.number());
    }
    cursor.hidden(true);
    if (!cursor.take(QLatin1Char(','))) break;
    cursor.hidden(true);
  }
  cursor.hidden(true);
  cursor.require(QLatin1Char('}'), QStringLiteral("expected '}'"));
  return curve;
}

void parseCurves(Cursor& cursor, QVector<PendingCurve>& curves) {
  cursor.consumeKeyword(QLatin1String("curve"));
  cursor.inlineWs();
  for (;;) {
    curves.append(parseOneCurve(cursor));
    cursor.inlineWs();
    if (!cursor.take(QLatin1Char(','))) return;
    cursor.inlineWs();
  }
}

void parseOption(Cursor& cursor, RadarOptions& options) {
  if (cursor.keyword(QLatin1String("showLegend"))) {
    cursor.consumeKeyword(QLatin1String("showLegend"));
    cursor.inlineWs();
    if (cursor.keyword(QLatin1String("true"))) {
      cursor.consumeKeyword(QLatin1String("true"));
      options.showLegend = true;
    } else if (cursor.keyword(QLatin1String("false"))) {
      cursor.consumeKeyword(QLatin1String("false"));
      options.showLegend = false;
    } else cursor.fail(QStringLiteral("showLegend requires boolean"));
  } else if (cursor.keyword(QLatin1String("ticks"))) {
    cursor.consumeKeyword(QLatin1String("ticks")); cursor.inlineWs(); options.ticks = cursor.number();
  } else if (cursor.keyword(QLatin1String("max"))) {
    cursor.consumeKeyword(QLatin1String("max")); cursor.inlineWs();
    options.max = cursor.number(); options.hasMax = true;
  } else if (cursor.keyword(QLatin1String("min"))) {
    cursor.consumeKeyword(QLatin1String("min")); cursor.inlineWs(); options.min = cursor.number();
  } else if (cursor.keyword(QLatin1String("graticule"))) {
    cursor.consumeKeyword(QLatin1String("graticule")); cursor.inlineWs();
    if (cursor.keyword(QLatin1String("circle"))) {
      cursor.consumeKeyword(QLatin1String("circle")); options.graticule = QStringLiteral("circle");
    } else if (cursor.keyword(QLatin1String("polygon"))) {
      cursor.consumeKeyword(QLatin1String("polygon")); options.graticule = QStringLiteral("polygon");
    } else cursor.fail(QStringLiteral("graticule requires circle or polygon"));
  } else cursor.fail(QStringLiteral("unknown radar option"));
}

QString normalizedSource(QString source, QString& frontmatterTitle) {
  if (!source.isEmpty() && source.front() == QChar(0xFEFF)) source.remove(0, 1);
  source.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  source.replace(QLatin1Char('\r'), QLatin1Char('\n'));
  if (!source.startsWith(QLatin1String("---"))) return source;
  const int firstEnd = source.indexOf(QLatin1Char('\n'));
  const int close = firstEnd < 0 ? -1 : source.indexOf(QLatin1String("\n---"), firstEnd);
  if (firstEnd < 0 || close < 0) return source;
  const QStringList yaml = source.mid(firstEnd + 1, close - firstEnd - 1).split(QLatin1Char('\n'));
  for (const QString& line : yaml) {
    const QString trimmed = line.trimmed();
    if (trimmed.startsWith(QLatin1String("title:"))) {
      frontmatterTitle = trimmed.mid(6).trimmed();
      if (frontmatterTitle.size() >= 2 &&
          ((frontmatterTitle.front() == QLatin1Char('"') && frontmatterTitle.back() == QLatin1Char('"')) ||
           (frontmatterTitle.front() == QLatin1Char('\'') && frontmatterTitle.back() == QLatin1Char('\''))))
        frontmatterTitle = frontmatterTitle.mid(1, frontmatterTitle.size() - 2);
    }
  }
  int after = close + 4;
  if (after < source.size() && source.at(after) == QLatin1Char('\n')) ++after;
  return source.mid(after);
}

}  // namespace

RadarData RadarDiagram::parse(const QString& rawSource) {
  RadarData data;
  QString frontmatterTitle;
  Cursor cursor(normalizedSource(rawSource, frontmatterTitle));
  cursor.hidden(true);
  if (!cursor.keyword(QLatin1String("radar-beta")))
    throw RadarParseError(QStringLiteral("missing radar-beta header"),
                          cursor.currentLine(), cursor.currentColumn());
  cursor.consumeKeyword(QLatin1String("radar-beta"));
  cursor.inlineWs();
  cursor.take(QLatin1Char(':'));

  QVector<PendingCurve> pendingCurves;
  while (true) {
    cursor.hidden(true);
    if (cursor.atEnd()) break;
    if (cursor.keyword(QLatin1String("axis"))) parseAxes(cursor, data);
    else if (cursor.keyword(QLatin1String("curve"))) parseCurves(cursor, pendingCurves);
    else if (cursor.keyword(QLatin1String("showLegend")) ||
             cursor.keyword(QLatin1String("ticks")) ||
             cursor.keyword(QLatin1String("max")) ||
             cursor.keyword(QLatin1String("min")) ||
             cursor.keyword(QLatin1String("graticule"))) {
      for (;;) {
        parseOption(cursor, data.options);
        cursor.inlineWs();
        if (!cursor.take(QLatin1Char(','))) break;
        cursor.inlineWs();
      }
    } else if (cursor.keyword(QLatin1String("title"))) {
      cursor.consumeKeyword(QLatin1String("title"));
      if (!cursor.atEnd() && cursor.peek() != QLatin1Char('\n') &&
          cursor.peek() != QLatin1Char('%') && !isInlineWs(cursor.peek()))
        cursor.fail(QStringLiteral("title text requires whitespace"));
      data.title = normalizedSingleLine(cursor.untilLineEnd());
    } else if (cursor.keyword(QLatin1String("accTitle"))) {
      cursor.consumeKeyword(QLatin1String("accTitle")); cursor.inlineWs();
      cursor.require(QLatin1Char(':'), QStringLiteral("accTitle requires ':'"));
      data.accTitle = normalizedSingleLine(cursor.untilLineEnd());
    } else if (cursor.keyword(QLatin1String("accDescr"))) {
      cursor.consumeKeyword(QLatin1String("accDescr")); cursor.inlineWs();
      if (cursor.take(QLatin1Char(':')))
        data.accDescr = normalizedSingleLine(cursor.untilLineEnd());
      else if (cursor.peek() == QLatin1Char('{'))
        data.accDescr = normalizedBlock(cursor.blockText());
      else cursor.fail(QStringLiteral("invalid accDescr"));
    } else {
      cursor.fail(QStringLiteral("unrecognized radar declaration"));
    }
  }

  // populateCommonDb applies source title after frontmatter metadata, so the
  // explicit diagram title wins; otherwise metadata supplies the title.
  if (data.title.isEmpty()) data.title = frontmatterTitle;

  // Upstream constructs the complete AST first, then calls setAxes followed by
  // setCurves. Resolve detailed entries only now, so curves may precede axes.
  for (const PendingCurve& pending : pendingCurves) {
    RadarCurve curve{pending.name, pending.label, pending.numericEntries};
    if (pending.detailed) {
      if (data.axes.isEmpty())
        throw RadarParseError(QStringLiteral("Axes must be populated before curves for reference entries"),
                              pending.line);
      for (const RadarAxis& axis : data.axes) {
        const auto found = std::find_if(pending.detailedEntries.cbegin(),
                                        pending.detailedEntries.cend(),
                                        [&](const DetailedRef& ref) { return ref.axis == axis.name; });
        if (found == pending.detailedEntries.cend())
          throw RadarParseError(QStringLiteral("Missing entry for axis ") + axis.label,
                                pending.line);
        curve.entries.append(found->value);
      }
    }
    data.curves.append(std::move(curve));
  }
  return data;
}

}  // namespace muffin::mermaid::radar
