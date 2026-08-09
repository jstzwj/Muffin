#include "mermaid/xychart/XYChartDiagram.h"

#include "blocks/html/HtmlSanitizer.h"

#include "mermaid/editor/MermaidRenderSupport.h"

#include <QByteArray>
#include <QChar>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>

namespace muffin::mermaid::xychart {

XYChartParseError::XYChartParseError(const QString& message, int line,
                                     int column)
    : std::runtime_error(message.toUtf8().constData()),
      line(line),
      column(column) {}

namespace {

bool asciiAlpha(QChar c) {
  const ushort u = c.unicode();
  return (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z');
}

bool asciiDigit(QChar c) {
  const ushort u = c.unicode();
  return u >= '0' && u <= '9';
}

bool asciiWord(QChar c) { return asciiAlpha(c) || asciiDigit(c) || c == QLatin1Char('_'); }

bool alphaNumPunctuation(QChar c) {
  return c == QLatin1Char('&') || c == QLatin1Char('+') ||
         c == QLatin1Char('=') || c == QLatin1Char('*') ||
         c == QLatin1Char('.') || c == QLatin1Char('#') ||
         c == QLatin1Char('-') || c == QLatin1Char('_') ||
         c == QLatin1Char('[') || c == QLatin1Char(']');
}

QString normalizeSource(QString source, QString& frontmatterTitle) {
  if (!source.isEmpty() && source.front() == QChar(0xFEFF)) source.remove(0, 1);
  source.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  source.replace(QLatin1Char('\r'), QLatin1Char('\n'));
  if (!source.startsWith(QLatin1String("---"))) return source;
  const qsizetype firstEnd = source.indexOf(QLatin1Char('\n'));
  const qsizetype close =
      firstEnd < 0 ? -1 : source.indexOf(QLatin1String("\n---"), firstEnd);
  if (firstEnd < 0 || close < 0) return source;
  const QStringList yaml =
      source.mid(firstEnd + 1, close - firstEnd - 1).split(QLatin1Char('\n'));
  for (const QString& raw : yaml) {
    const QString line = raw.trimmed();
    if (!line.startsWith(QLatin1String("title:"))) continue;
    frontmatterTitle = line.mid(6).trimmed();
    if (frontmatterTitle.size() >= 2 &&
        ((frontmatterTitle.front() == QLatin1Char('"') &&
          frontmatterTitle.back() == QLatin1Char('"')) ||
         (frontmatterTitle.front() == QLatin1Char('\'') &&
          frontmatterTitle.back() == QLatin1Char('\'')))) {
      frontmatterTitle =
          frontmatterTitle.mid(1, frontmatterTitle.size() - 2);
    }
  }
  qsizetype after = close + 4;
  if (after < source.size() && source.at(after) == QLatin1Char('\n')) ++after;
  return source.mid(after);
}

class Cursor {
public:
  explicit Cursor(QString source) : text_(std::move(source)) {}

  bool atEnd() const { return pos_ >= text_.size(); }
  QChar peek(qsizetype offset = 0) const {
    const qsizetype index = pos_ + offset;
    return index < text_.size() ? text_.at(index) : QChar();
  }
  int line() const { return line_; }
  int column() const { return column_; }

  void advance() {
    if (atEnd()) return;
    if (text_.at(pos_++) == QLatin1Char('\n')) {
      ++line_;
      column_ = 1;
    } else {
      ++column_;
    }
  }

  bool atComment() const {
    return peek() == QLatin1Char('%') && peek(1) == QLatin1Char('%') &&
           peek(2) != QLatin1Char('{');
  }

  void skipInlineWhitespace() {
    while (peek() == QLatin1Char(' ') || peek() == QLatin1Char('\t')) advance();
  }

  void skipEmptyQuotedStrings() {
    for (;;) {
      skipInlineWhitespace();
      if (peek() != QLatin1Char('"') || peek(1) != QLatin1Char('"')) return;
      advance();
      advance();
    }
  }

  void skipComment() {
    if (!atComment()) return;
    while (!atEnd() && peek() != QLatin1Char('\n')) advance();
  }

  void skipDirective() {
    if (!(peek() == QLatin1Char('%') && peek(1) == QLatin1Char('%') &&
          peek(2) == QLatin1Char('{')))
      return;
    const qsizetype close = text_.indexOf(QLatin1String("}%%"), pos_ + 3);
    if (close < 0) fail(QStringLiteral("unterminated directive"));
    while (pos_ < close + 3) advance();
  }

  void skipLeading() {
    for (;;) {
      while (peek().isSpace()) advance();
      if (atComment()) {
        skipComment();
        continue;
      }
      if (peek() == QLatin1Char('%') && peek(1) == QLatin1Char('%') &&
          peek(2) == QLatin1Char('{')) {
        skipDirective();
        continue;
      }
      return;
    }
  }

  void skipStatements() {
    for (;;) {
      while (peek().isSpace() || peek() == QLatin1Char(';')) advance();
      if (atComment()) {
        skipComment();
        continue;
      }
      if (peek() == QLatin1Char('%') && peek(1) == QLatin1Char('%') &&
          peek(2) == QLatin1Char('{')) {
        skipDirective();
        continue;
      }
      return;
    }
  }

  bool keyword(QLatin1String word) const {
    if (text_.mid(pos_, word.size()).compare(word, Qt::CaseInsensitive) != 0)
      return false;
    const qsizetype after = pos_ + word.size();
    return after >= text_.size() || !asciiWord(text_.at(after));
  }

  QString takeKeyword(QLatin1String word) {
    if (!keyword(word)) fail(QStringLiteral("expected ") + word);
    const QString result = text_.mid(pos_, word.size());
    for (qsizetype i = 0; i < word.size(); ++i) advance();
    return result;
  }

  bool take(QChar c) {
    if (peek() != c) return false;
    advance();
    return true;
  }

  void require(QChar c, const QString& message) {
    if (!take(c)) fail(message);
  }

  void requireArrow() {
    if (peek() != QLatin1Char('-') || peek(1) != QLatin1Char('-') ||
        peek(2) != QLatin1Char('>'))
      fail(QStringLiteral("expected '-->'"));
    advance();
    advance();
    advance();
  }

  QString quotedString() {
    require(QLatin1Char('"'), QStringLiteral("expected quoted string"));
    QString result;
    while (!atEnd() && peek() != QLatin1Char('"')) {
      result.append(peek());
      advance();
    }
    if (atEnd())
      throw XYChartParseError(QStringLiteral("unterminated string"), line_ + 1, 1);
    advance();
    return result.trimmed();
  }

  bool linearRangeStartsHere() const {
    qsizetype i = pos_;
    if (i < text_.size() &&
        (text_.at(i) == QLatin1Char('+') || text_.at(i) == QLatin1Char('-')))
      ++i;
    bool digits = false;
    while (i < text_.size() && asciiDigit(text_.at(i))) {
      digits = true;
      ++i;
    }
    if (i < text_.size() && text_.at(i) == QLatin1Char('.')) {
      if (i + 1 >= text_.size() || !asciiDigit(text_.at(i + 1)))
        return false;
      ++i;
      while (i < text_.size() && asciiDigit(text_.at(i))) {
        digits = true;
        ++i;
      }
    }
    if (!digits) return false;
    while (i < text_.size() &&
           (text_.at(i) == QLatin1Char(' ') ||
            text_.at(i) == QLatin1Char('\t')))
      ++i;
    return text_.mid(i, 3) == QLatin1String("-->");
  }

  QString alphaNumText(const QString& terminators = QString(),
                       bool stopBeforeLinearRange = false) {
    QString result;
    bool consumed = false;
    for (;;) {
      if (consumed && stopBeforeLinearRange && linearRangeStartsHere()) break;
      if (atEnd() || peek() == QLatin1Char('\n') ||
          peek() == QLatin1Char(';') || atComment() ||
          terminators.contains(peek()))
        break;
      if (peek() == QLatin1Char(' ') || peek() == QLatin1Char('\t')) {
        advance();
        continue;
      }
      if (asciiAlpha(peek())) {
        const qsizetype start = pos_;
        while (asciiAlpha(peek())) advance();
        const QString token = text_.mid(start, pos_ - start);
        if (reservedToken(token)) {
          throw XYChartParseError(QStringLiteral("reserved keyword in text: ") + token,
                                  line_ + 1, column_);
        }
        result += token;
        consumed = true;
        continue;
      }
      if (asciiDigit(peek()) || alphaNumPunctuation(peek())) {
        result.append(peek());
        advance();
        consumed = true;
        continue;
      }
      break;
    }
    if (!consumed) fail(QStringLiteral("expected text"));
    return result.trimmed();
  }

  QString text(const QString& terminators = QString()) {
    skipInlineWhitespace();
    return peek() == QLatin1Char('"') ? quotedString()
                                      : alphaNumText(terminators);
  }

  QString axisTitleText(const QString& terminators = QString()) {
    skipInlineWhitespace();
    return peek() == QLatin1Char('"')
               ? quotedString()
               : alphaNumText(terminators, true);
  }

  double number() {
    const qsizetype start = pos_;
    if (peek() == QLatin1Char('+') || peek() == QLatin1Char('-')) advance();
    bool digits = false;
    while (asciiDigit(peek())) {
      digits = true;
      advance();
    }
    if (peek() == QLatin1Char('.')) {
      if (!asciiDigit(peek(1))) {
        if (!digits) fail(QStringLiteral("expected number"));
        return parseNumber(start);
      }
      advance();
      while (asciiDigit(peek())) {
        digits = true;
        advance();
      }
    }
    if (!digits) fail(QStringLiteral("expected number"));
    return parseNumber(start);
  }

  [[noreturn]] void fail(const QString& message) const {
    throw XYChartParseError(message, line_, column_);
  }

private:
  static bool reservedToken(const QString& token) {
    static const QStringList reserved = {
        QStringLiteral("title"),       QStringLiteral("accTitle"),
        QStringLiteral("accDescr"),    QStringLiteral("xychart"),
        QStringLiteral("xychart-beta"), QStringLiteral("horizontal"),
        QStringLiteral("vertical"),    QStringLiteral("x-axis"),
        QStringLiteral("y-axis"),      QStringLiteral("line"),
        QStringLiteral("bar")};
    for (const QString& word : reserved)
      if (token.compare(word, Qt::CaseInsensitive) == 0) return true;
    return false;
  }

  double parseNumber(qsizetype start) const {
    const QByteArray bytes = text_.mid(start, pos_ - start).toLatin1();
    char* end = nullptr;
    return std::strtod(bytes.constData(), &end);
  }

  QString text_;
  qsizetype pos_ = 0;
  int line_ = 1;
  int column_ = 1;
};

struct ParsedPoint {
  double value = 0.0;
  QString label;
};

QString sanitizeDbText(QString text) {
  return HtmlSanitizer().sanitizedMermaidText(text.trimmed());
}

void setLinearAxis(XYChartAxisData& axis, double min, double max) {
  const QString title = axis.title;
  axis = XYChartAxisData{XYChartAxisType::Linear};
  axis.title = title;
  axis.min = min;
  axis.max = max;
}

void setBandAxis(XYChartAxisData& axis, QStringList categories) {
  const QString title = axis.title;
  axis = XYChartAxisData{XYChartAxisType::Band};
  axis.title = title;
  axis.categories = std::move(categories);
}

QString jsNumber(double value) {
  return editor::jsNumberToString(value);
}

void updateYAxisRange(XYChartAxisData& axis, const QVector<double>& values) {
  if (values.isEmpty()) return;
  double minValue = values.front();
  double maxValue = values.front();
  for (double value : values) {
    minValue = std::min(minValue, value);
    maxValue = std::max(maxValue, value);
  }
  axis.type = XYChartAxisType::Linear;
  axis.min = std::min(axis.min, minValue);
  axis.max = std::max(axis.max, maxValue);
}

QVector<ParsedPoint> parsePlotPoints(Cursor& cursor) {
  cursor.skipInlineWhitespace();
  cursor.require(QLatin1Char('['), QStringLiteral("plot requires '['"));
  cursor.skipInlineWhitespace();
  if (cursor.peek() == QLatin1Char(']'))
    cursor.fail(QStringLiteral("plot data cannot be empty"));
  QVector<ParsedPoint> result;
  for (;;) {
    ParsedPoint point;
    point.value = cursor.number();
    cursor.skipInlineWhitespace();
    cursor.skipEmptyQuotedStrings();
    cursor.skipInlineWhitespace();
    if (cursor.peek() == QLatin1Char('"'))
      point.label = sanitizeDbText(cursor.quotedString());
    result.append(std::move(point));
    cursor.skipInlineWhitespace();
    if (!cursor.take(QLatin1Char(','))) break;
    cursor.skipInlineWhitespace();
    if (cursor.peek() == QLatin1Char(']'))
      cursor.fail(QStringLiteral("trailing comma in plot data"));
  }
  cursor.require(QLatin1Char(']'), QStringLiteral("expected ']'"));
  return result;
}

QStringList parseBand(Cursor& cursor) {
  cursor.require(QLatin1Char('['), QStringLiteral("expected '['"));
  cursor.skipInlineWhitespace();
  if (cursor.peek() == QLatin1Char(']'))
    cursor.fail(QStringLiteral("band categories cannot be empty"));
  QStringList result;
  for (;;) {
    cursor.skipEmptyQuotedStrings();
    cursor.skipInlineWhitespace();
    result.append(sanitizeDbText(cursor.text(QStringLiteral(",]"))));
    cursor.skipInlineWhitespace();
    if (!cursor.take(QLatin1Char(','))) break;
    cursor.skipInlineWhitespace();
    if (cursor.peek() == QLatin1Char(']'))
      cursor.fail(QStringLiteral("trailing comma in band categories"));
  }
  cursor.require(QLatin1Char(']'), QStringLiteral("expected ']'"));
  return result;
}

void addPlot(XYChartData& data, XYChartPlotType type,
             QVector<ParsedPoint> parsed, bool& hasSetXAxis,
             bool hasSetYAxis) {
  QVector<double> values;
  QStringList labels;
  values.reserve(parsed.size());
  labels.reserve(parsed.size());
  bool hasAnyLabel = false;
  for (const ParsedPoint& point : parsed) {
    values.append(point.value);
    labels.append(point.label);
    hasAnyLabel = hasAnyLabel || !point.label.isEmpty();
  }

  if (!hasSetXAxis) {
    const double previousMin = data.xAxis.type == XYChartAxisType::Linear
                                   ? data.xAxis.min
                                   : std::numeric_limits<double>::infinity();
    const double previousMax = data.xAxis.type == XYChartAxisType::Linear
                                   ? data.xAxis.max
                                   : -std::numeric_limits<double>::infinity();
    setLinearAxis(data.xAxis, std::min(previousMin, 1.0),
                  std::max(previousMax, double(values.size())));
    hasSetXAxis = true;
  }

  if (data.xAxis.type == XYChartAxisType::Band &&
      values.size() > data.xAxis.categories.size()) {
    values.resize(data.xAxis.categories.size());
  }
  if (!hasSetYAxis) updateYAxisRange(data.yAxis, values);

  XYChartPlotData plot;
  plot.type = type;
  plot.paletteIndex = data.plots.size();
  plot.hasPointLabels = type == XYChartPlotType::Line && hasAnyLabel;
  if (plot.hasPointLabels) plot.pointLabels = labels;

  if (data.xAxis.type == XYChartAxisType::Band) {
    plot.points.reserve(data.xAxis.categories.size());
    for (qsizetype i = 0; i < data.xAxis.categories.size(); ++i) {
      if (i < values.size())
        plot.points.append({data.xAxis.categories.at(i), values.at(i), true});
      else
        plot.points.append({data.xAxis.categories.at(i), 0.0, false});
    }
  } else {
    const double step =
        (data.xAxis.max - data.xAxis.min) / double(values.size() - 1);
    double category = data.xAxis.min;
    qsizetype index = 0;
    // This is deliberately the upstream `for (i=min; i<=max; i+=step)`;
    // descending ranges yield no points and NaN step terminates after one.
    while (category <= data.xAxis.max) {
      const bool defined = index < values.size();
      plot.points.append(
          {jsNumber(category), defined ? values.at(index) : 0.0, defined});
      category += step;
      ++index;
      if (std::isnan(category)) break;
      if (index > values.size() + 2) break;  // finite-rounding safety only
    }
  }
  data.plots.append(std::move(plot));
}

QString readAccLine(Cursor& cursor) {
  QString result;
  while (!cursor.atEnd() && cursor.peek() != QLatin1Char('\n')) {
    result.append(cursor.peek());
    cursor.advance();
  }
  return result.trimmed();
}

QString readAccBlock(Cursor& cursor) {
  cursor.require(QLatin1Char('{'), QStringLiteral("expected '{'"));
  QString result;
  while (!cursor.atEnd() && cursor.peek() != QLatin1Char('}')) {
    result.append(cursor.peek());
    cursor.advance();
  }
  if (cursor.atEnd()) cursor.fail(QStringLiteral("unterminated accDescr block"));
  cursor.advance();
  QStringList lines = result.split(QLatin1Char('\n'));
  for (QString& line : lines) line = line.trimmed();
  while (!lines.isEmpty() && lines.constFirst().isEmpty()) lines.removeFirst();
  while (!lines.isEmpty() && lines.constLast().isEmpty()) lines.removeLast();
  return lines.join(QLatin1Char('\n'));
}

}  // namespace

XYChartData XYChartDiagram::parse(const QString& rawSource) {
  XYChartData data;
  QString frontmatterTitle;
  Cursor cursor(normalizeSource(rawSource, frontmatterTitle));
  cursor.skipLeading();
  if (cursor.keyword(QLatin1String("xychart-beta")))
    cursor.takeKeyword(QLatin1String("xychart-beta"));
  else if (cursor.keyword(QLatin1String("xychart")))
    cursor.takeKeyword(QLatin1String("xychart"));
  else
    cursor.fail(QStringLiteral("missing xychart header"));

  // chartConfig is a single token immediately after XYCHART. A newline ends
  // the opportunity to set it. The DB compares the lexeme case-sensitively,
  // so only exact lowercase `horizontal` selects horizontal mode.
  cursor.skipInlineWhitespace();
  if (cursor.keyword(QLatin1String("horizontal"))) {
    const QString token = cursor.takeKeyword(QLatin1String("horizontal"));
    data.hasOrientationDirective = true;
    data.orientation = token == QLatin1String("horizontal")
                           ? XYChartOrientation::Horizontal
                           : XYChartOrientation::Vertical;
  } else if (cursor.keyword(QLatin1String("vertical"))) {
    cursor.takeKeyword(QLatin1String("vertical"));
    data.hasOrientationDirective = true;
    data.orientation = XYChartOrientation::Vertical;
  }

  bool hasSetXAxis = false;
  bool hasSetYAxis = false;
  cursor.skipStatements();
  while (!cursor.atEnd()) {
    if (cursor.keyword(QLatin1String("title"))) {
      cursor.takeKeyword(QLatin1String("title"));
      cursor.skipInlineWhitespace();
      cursor.skipEmptyQuotedStrings();
      cursor.skipInlineWhitespace();
      if (cursor.peek() == QLatin1Char('\n') || cursor.atEnd())
        throw XYChartParseError("title requires non-empty text",
                                cursor.line() + 1, 1);
      data.title = cursor.text();
      data.hasTitleDirective = true;
    } else if (cursor.keyword(QLatin1String("accTitle"))) {
      cursor.takeKeyword(QLatin1String("accTitle"));
      cursor.skipInlineWhitespace();
      cursor.require(QLatin1Char(':'), QStringLiteral("accTitle requires ':'"));
      cursor.skipInlineWhitespace();
      data.accTitle = readAccLine(cursor);
    } else if (cursor.keyword(QLatin1String("accDescr"))) {
      cursor.takeKeyword(QLatin1String("accDescr"));
      cursor.skipInlineWhitespace();
      if (cursor.take(QLatin1Char(':'))) {
        cursor.skipInlineWhitespace();
        data.accDescr = readAccLine(cursor);
      } else if (cursor.peek() == QLatin1Char('{')) {
        data.accDescr = readAccBlock(cursor);
      } else {
        cursor.fail(QStringLiteral("invalid accDescr"));
      }
    } else if (cursor.keyword(QLatin1String("x-axis"))) {
      cursor.takeKeyword(QLatin1String("x-axis"));
      cursor.skipInlineWhitespace();
      cursor.skipEmptyQuotedStrings();
      cursor.skipInlineWhitespace();
      if (cursor.peek() == QLatin1Char('\n')) {
        cursor.skipStatements();
        cursor.fail(QStringLiteral("x-axis requires data or title"));
      }
      bool hasTitle = false;
      if (cursor.peek() == QLatin1Char('"') ||
          (!asciiDigit(cursor.peek()) && cursor.peek() != QLatin1Char('+') &&
           cursor.peek() != QLatin1Char('-') && cursor.peek() != QLatin1Char('['))) {
        data.xAxis.title =
            sanitizeDbText(cursor.axisTitleText(QStringLiteral("[")));
        hasTitle = true;
        cursor.skipInlineWhitespace();
      }
      if (cursor.peek() == QLatin1Char('[')) {
        setBandAxis(data.xAxis, parseBand(cursor));
        hasSetXAxis = true;
      } else if (asciiDigit(cursor.peek()) || cursor.peek() == QLatin1Char('+') ||
                 cursor.peek() == QLatin1Char('-')) {
        const double min = cursor.number();
        cursor.skipInlineWhitespace();
        cursor.requireArrow();
        cursor.skipInlineWhitespace();
        const double max = cursor.number();
        setLinearAxis(data.xAxis, min, max);
        hasSetXAxis = true;
      } else if (!hasTitle) {
        cursor.fail(QStringLiteral("x-axis requires data or title"));
      }
    } else if (cursor.keyword(QLatin1String("y-axis"))) {
      cursor.takeKeyword(QLatin1String("y-axis"));
      cursor.skipInlineWhitespace();
      cursor.skipEmptyQuotedStrings();
      cursor.skipInlineWhitespace();
      if (cursor.peek() == QLatin1Char('\n')) {
        cursor.skipStatements();
        cursor.fail(QStringLiteral("y-axis requires data or title"));
      }
      bool hasTitle = false;
      if (cursor.peek() == QLatin1Char('"') ||
          (!asciiDigit(cursor.peek()) && cursor.peek() != QLatin1Char('+') &&
           cursor.peek() != QLatin1Char('-'))) {
        data.yAxis.title = sanitizeDbText(cursor.axisTitleText());
        hasTitle = true;
        cursor.skipInlineWhitespace();
      }
      if (asciiDigit(cursor.peek()) || cursor.peek() == QLatin1Char('+') ||
          cursor.peek() == QLatin1Char('-')) {
        const double min = cursor.number();
        cursor.skipInlineWhitespace();
        cursor.requireArrow();
        cursor.skipInlineWhitespace();
        const double max = cursor.number();
        setLinearAxis(data.yAxis, min, max);
        hasSetYAxis = true;
      } else if (!hasTitle) {
        cursor.fail(QStringLiteral("y-axis requires data or title"));
      }
    } else if (cursor.keyword(QLatin1String("line")) ||
               cursor.keyword(QLatin1String("bar"))) {
      const bool line = cursor.keyword(QLatin1String("line"));
      cursor.takeKeyword(line ? QLatin1String("line") : QLatin1String("bar"));
      cursor.skipInlineWhitespace();
      cursor.skipEmptyQuotedStrings();
      cursor.skipInlineWhitespace();
      if (cursor.peek() != QLatin1Char('[')) {
        (void)cursor.text(QStringLiteral("["));  // plot titles are parsed then discarded
        cursor.skipInlineWhitespace();
      }
      addPlot(data, line ? XYChartPlotType::Line : XYChartPlotType::Bar,
              parsePlotPoints(cursor), hasSetXAxis, hasSetYAxis);
    } else if (cursor.keyword(QLatin1String("horizontal")) ||
               cursor.keyword(QLatin1String("vertical"))) {
      cursor.fail(QStringLiteral("orientation must follow xychart on the same line"));
    } else {
      cursor.fail(QStringLiteral("unrecognized xychart statement"));
    }
    cursor.skipStatements();
  }

  if (!data.hasTitleDirective) data.title = frontmatterTitle;
  return data;
}

}  // namespace muffin::mermaid::xychart
