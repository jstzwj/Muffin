#include "mermaid/flowchart/FlowchartTokenizer.h"

#include <QRegularExpression>

#include <functional>
#include <stdexcept>

namespace muffin::mermaid::flowchart {
namespace {

using K = FlowTokenKind;

bool isAsciiNodeChar(QChar ch) {
  if (ch.isLetterOrNumber()) return ch.unicode() < 128;
  return QStringLiteral("!\"#$%&'*+.`?\\_/").contains(ch);
}

bool isUnitStart(QChar ch) {
  return ch.isLetter();
}

bool isNodeHyphen(const QString& source, qsizetype index) {
  if (index < 0 || index >= source.size() || source.at(index) != QLatin1Char('-') ||
      index + 1 >= source.size()) return false;
  return !QStringLiteral(">-.").contains(source.at(index + 1));
}

}  // namespace

QString flowTokenName(FlowTokenKind kind) {
  switch (kind) {
#define FLOW_TOKEN_NAME(name) case K::name: return QStringLiteral(#name)
    FLOW_TOKEN_NAME(Eof); FLOW_TOKEN_NAME(Semi); FLOW_TOKEN_NAME(Newline);
    FLOW_TOKEN_NAME(Space); FLOW_TOKEN_NAME(Graph); FLOW_TOKEN_NAME(NoDir);
    FLOW_TOKEN_NAME(Dir); FLOW_TOKEN_NAME(Subgraph); FLOW_TOKEN_NAME(End);
    FLOW_TOKEN_NAME(AccTitle); FLOW_TOKEN_NAME(AccTitleValue); FLOW_TOKEN_NAME(AccDescr);
    FLOW_TOKEN_NAME(AccDescrValue); FLOW_TOKEN_NAME(AccDescrMultilineValue);
    FLOW_TOKEN_NAME(ShapeData); FLOW_TOKEN_NAME(Amp); FLOW_TOKEN_NAME(StyleSeparator);
    FLOW_TOKEN_NAME(DoubleCircleStart); FLOW_TOKEN_NAME(DoubleCircleEnd);
    FLOW_TOKEN_NAME(Sqs); FLOW_TOKEN_NAME(Sqe); FLOW_TOKEN_NAME(Ps); FLOW_TOKEN_NAME(Pe); FLOW_TOKEN_NAME(EllipseStart);
    FLOW_TOKEN_NAME(EllipseEnd); FLOW_TOKEN_NAME(StadiumStart); FLOW_TOKEN_NAME(StadiumEnd);
    FLOW_TOKEN_NAME(SubroutineStart); FLOW_TOKEN_NAME(SubroutineEnd);
    FLOW_TOKEN_NAME(VertexWithPropsStart); FLOW_TOKEN_NAME(Colon); FLOW_TOKEN_NAME(Pipe);
    FLOW_TOKEN_NAME(CylinderStart); FLOW_TOKEN_NAME(CylinderEnd);
    FLOW_TOKEN_NAME(DiamondStart); FLOW_TOKEN_NAME(DiamondStop);
    FLOW_TOKEN_NAME(TagStart); FLOW_TOKEN_NAME(TagEnd); FLOW_TOKEN_NAME(TrapStart);
    FLOW_TOKEN_NAME(TrapEnd); FLOW_TOKEN_NAME(InvTrapStart); FLOW_TOKEN_NAME(InvTrapEnd);
    FLOW_TOKEN_NAME(StartLink); FLOW_TOKEN_NAME(Link); FLOW_TOKEN_NAME(LinkId);
    FLOW_TOKEN_NAME(Str); FLOW_TOKEN_NAME(MarkdownStr); FLOW_TOKEN_NAME(Style);
    FLOW_TOKEN_NAME(LinkStyle); FLOW_TOKEN_NAME(ClassDef); FLOW_TOKEN_NAME(Class);
    FLOW_TOKEN_NAME(Click); FLOW_TOKEN_NAME(Down); FLOW_TOKEN_NAME(Up);
    FLOW_TOKEN_NAME(CallbackName); FLOW_TOKEN_NAME(CallbackArgs); FLOW_TOKEN_NAME(Href);
    FLOW_TOKEN_NAME(LinkTarget); FLOW_TOKEN_NAME(Default); FLOW_TOKEN_NAME(Interpolate);
    FLOW_TOKEN_NAME(Number); FLOW_TOKEN_NAME(Comma); FLOW_TOKEN_NAME(NodeString);
    FLOW_TOKEN_NAME(Unit); FLOW_TOKEN_NAME(Bracket); FLOW_TOKEN_NAME(Percent);
    FLOW_TOKEN_NAME(Minus); FLOW_TOKEN_NAME(Multiply); FLOW_TOKEN_NAME(UnicodeText);
    FLOW_TOKEN_NAME(Text); FLOW_TOKEN_NAME(EdgeText); FLOW_TOKEN_NAME(DirectionTb);
    FLOW_TOKEN_NAME(DirectionBt); FLOW_TOKEN_NAME(DirectionRl); FLOW_TOKEN_NAME(DirectionLr);
    FLOW_TOKEN_NAME(DirectionTd); FLOW_TOKEN_NAME(Quote); FLOW_TOKEN_NAME(Invalid);
    FLOW_TOKEN_NAME(Unknown);
#undef FLOW_TOKEN_NAME
  }
  return QStringLiteral("Unknown");
}

FlowchartTokenizer::FlowchartTokenizer(QString source, bool expectGraphHeader)
    : source_(std::move(source)), firstGraph_(expectGraphHeader) {
  source_.replace(QRegularExpression(QStringLiteral("\\r\\n?")), QStringLiteral("\n"));
}

bool FlowchartTokenizer::atEnd() const { return offset_ >= source_.size(); }

QChar FlowchartTokenizer::peek(qsizetype ahead) const {
  const qsizetype at = offset_ + ahead;
  return at >= 0 && at < source_.size() ? source_.at(at) : QChar();
}

bool FlowchartTokenizer::startsWith(QStringView value, Qt::CaseSensitivity cs) const {
  return QStringView(source_).mid(offset_, value.size()).compare(value, cs) == 0;
}

void FlowchartTokenizer::advance(const QString& consumed) {
  for (QChar ch : consumed) {
    if (ch == QLatin1Char('\n')) { ++line_; column_ = 1; }
    else ++column_;
  }
  offset_ += consumed.size();
}

FlowToken FlowchartTokenizer::take(FlowTokenKind kind, qsizetype length, QString text) {
  FlowToken token{kind, {}, line_, column_, offset_};
  const QString consumed = source_.mid(offset_, length);
  token.text = text.isNull() ? consumed : std::move(text);
  advance(consumed);
  return token;
}

FlowToken FlowchartTokenizer::takeInvalid(FlowchartErrorCode code, qsizetype length) {
  FlowToken token = take(K::Invalid, length);
  token.diagnosticCode = code;
  return token;
}

FlowToken FlowchartTokenizer::takeWhile(FlowTokenKind kind,
                                        const std::function<bool(QChar)>& predicate) {
  qsizetype length = 0;
  while (offset_ + length < source_.size() && predicate(source_.at(offset_ + length))) ++length;
  return take(kind, length);
}

FlowToken FlowchartTokenizer::lexDir() {
  const int startLine = line_, startColumn = column_;
  const qsizetype startOffset = offset_;
  qsizetype whitespace = 0;
  while (peek(whitespace).isSpace() && peek(whitespace) != QLatin1Char('\n')) ++whitespace;
  if (peek(whitespace) == QLatin1Char('\n')) {
    while (peek(whitespace) == QLatin1Char('\n') || peek(whitespace).isSpace()) ++whitespace;
    modes_.last() = Mode::Initial;
    FlowToken token{K::NoDir, source_.mid(offset_, whitespace), startLine, startColumn, startOffset};
    advance(token.text);
    return token;
  }
  static const QStringList directions = {
      QStringLiteral("LR"), QStringLiteral("RL"), QStringLiteral("TB"), QStringLiteral("BT"),
      QStringLiteral("TD"), QStringLiteral("BR"), QStringLiteral("<"), QStringLiteral(">"),
      QStringLiteral("^"), QStringLiteral("v")};
  for (const QString& direction : directions) {
    if (QStringView(source_).mid(offset_ + whitespace).startsWith(direction)) {
      const QString consumed = source_.mid(offset_, whitespace + direction.size());
      modes_.last() = Mode::Initial;
      FlowToken token{K::Dir, consumed, startLine, startColumn, startOffset};
      advance(consumed);
      return token;
    }
  }
  return takeInvalid(FlowchartErrorCode::InvalidDirection,
                     std::max<qsizetype>(1, whitespace));
}

FlowToken FlowchartTokenizer::lexQuoted() {
  const int startLine = line_, startColumn = column_;
  const qsizetype startOffset = offset_;
  if (startsWith(QStringLiteral("\"`"))) {
    qsizetype end = source_.indexOf(QStringLiteral("`\""), offset_ + 2);
    if (end < 0)
      return takeInvalid(FlowchartErrorCode::UnterminatedString,
                         source_.size() - offset_);
    const QString value = source_.mid(offset_ + 2, end - offset_ - 2);
    const QString consumed = source_.mid(offset_, std::min(source_.size(), end + 2) - offset_);
    advance(consumed);
    if (value.isEmpty()) return next();
    return {K::MarkdownStr, value, startLine, startColumn, startOffset};
  }
  qsizetype end = offset_ + 1;
  bool escaped = false;
  while (end < source_.size()) {
    const QChar ch = source_.at(end);
    if (!escaped && ch == QLatin1Char('"')) break;
    escaped = !escaped && ch == QLatin1Char('\\');
    if (ch != QLatin1Char('\\')) escaped = false;
    ++end;
  }
  if (end >= source_.size())
    return takeInvalid(FlowchartErrorCode::UnterminatedString,
                       source_.size() - offset_);
  const QString value = source_.mid(offset_ + 1, end - offset_ - 1);
  const qsizetype length = end < source_.size() ? end - offset_ + 1 : source_.size() - offset_;
  const QString consumed = source_.mid(offset_, length);
  advance(consumed);
  if (value.isEmpty()) return next();
  return {K::Str, value, startLine, startColumn, startOffset};
}

FlowToken FlowchartTokenizer::lexShapeData() {
  const int startLine = line_, startColumn = column_;
  const qsizetype startOffset = offset_;
  qsizetype at = offset_ + 2;
  bool quoted = false;
  bool escaped = false;
  for (; at < source_.size(); ++at) {
    const QChar ch = source_.at(at);
    if (escaped) { escaped = false; continue; }
    if (ch == QLatin1Char('\\') && quoted) { escaped = true; continue; }
    if (ch == QLatin1Char('"')) quoted = !quoted;
    if (ch == QLatin1Char('}') && !quoted) break;
  }
  if (at >= source_.size())
    return takeInvalid(FlowchartErrorCode::UnterminatedShapeData,
                       source_.size() - offset_);
  QString value = source_.mid(offset_ + 2, at - offset_ - 2);
  value.replace(QRegularExpression(QStringLiteral("\\n\\s*")), QStringLiteral("<br/>"));
  const qsizetype length = at < source_.size() ? at - offset_ + 1 : source_.size() - offset_;
  const QString consumed = source_.mid(offset_, length);
  advance(consumed);
  return {K::ShapeData, value, startLine, startColumn, startOffset};
}

FlowToken FlowchartTokenizer::tryKeyword() {
  struct Keyword { const char* text; K kind; };
  static const Keyword keywords[] = {
      {"flowchart-elk", K::Graph}, {"swimlane-beta", K::Graph}, {"flowchart", K::Graph},
      {"linkStyle", K::LinkStyle}, {"interpolate", K::Interpolate}, {"classDef", K::ClassDef},
      {"subgraph", K::Subgraph}, {"accTitle", K::AccTitle}, {"accDescr", K::AccDescr},
      {"default", K::Default}, {"graph", K::Graph}, {"style", K::Style},
      {"class", K::Class}, {"click", K::Click}, {"href", K::Href}, {"end", K::End},
  };
  for (const auto& keyword : keywords) {
    const QString value = QString::fromLatin1(keyword.text);
    if (!startsWith(value)) continue;
    if (isAsciiNodeChar(peek(value.size()))) continue;
    if (keyword.kind == K::Graph && firstGraph_) {
      firstGraph_ = false;
      modes_.last() = Mode::Dir;
    }
    return take(keyword.kind, value.size());
  }
  for (const QString& target : {QStringLiteral("_self"), QStringLiteral("_blank"),
                                QStringLiteral("_parent"), QStringLiteral("_top")})
    if (startsWith(target)) return take(K::LinkTarget, target.size());
  return {};
}

FlowToken FlowchartTokenizer::tryLink() {
  const QStringView rest = QStringView(source_).mid(offset_);
  static const QVector<QRegularExpression> complete = {
      QRegularExpression(QStringLiteral(R"(^\s*[xo<]?\-\-+[-xo>]\s*)")),
      QRegularExpression(QStringLiteral(R"(^\s*[xo<]?\=\=+[=xo>]\s*)")),
      QRegularExpression(QStringLiteral(R"(^\s*[xo<]?\-?\.+\-[xo>]?\s*)")),
      QRegularExpression(QStringLiteral(R"(^\s*\~\~[\~]+\s*)")),
  };
  for (const QRegularExpression& expression : complete) {
    const auto match = expression.matchView(rest);
    if (match.hasMatch()) return take(K::Link, match.capturedLength(), match.captured());
  }
  struct Start { QRegularExpression expression; Mode mode; };
  static const QVector<Start> starts = {
      {QRegularExpression(QStringLiteral(R"(^\s*[xo<]?\-\-\s*)")), Mode::EdgeText},
      {QRegularExpression(QStringLiteral(R"(^\s*[xo<]?\=\=\s*)")), Mode::ThickEdgeText},
      {QRegularExpression(QStringLiteral(R"(^\s*[xo<]?\-\.\s*)")), Mode::DottedEdgeText},
  };
  for (const Start& start : starts) {
    const auto match = start.expression.matchView(rest);
    if (!match.hasMatch()) continue;
    modes_.push_back(start.mode);
    return take(K::StartLink, match.capturedLength(), match.captured());
  }
  return {};
}

FlowToken FlowchartTokenizer::lexText() {
  if (startsWith(QStringLiteral("-)"))) { modes_.removeLast(); return take(K::EllipseEnd, 2); }
  if (peek() == QLatin1Char('(')) { modes_.push_back(Mode::Text); return take(K::Ps, 1); }
  if (peek() == QLatin1Char('[')) { modes_.push_back(Mode::Text); return take(K::Sqs, 1); }
  if (peek() == QLatin1Char('{')) { modes_.push_back(Mode::Text); return take(K::DiamondStart, 1); }
  struct Delimiter { const char* text; K kind; };
  static const Delimiter delimiters[] = {
      {")))", K::DoubleCircleEnd}, {"]]", K::SubroutineEnd}, {"])", K::StadiumEnd},
      {")]", K::CylinderEnd}, {"}", K::DiamondStop}, {"]", K::Sqe},
      {")", K::Pe}, {"|", K::Pipe},
  };
  for (const auto& delimiter : delimiters) {
    const QString value = QString::fromLatin1(delimiter.text);
    if (!startsWith(value)) continue;
    modes_.removeLast();
    return take(delimiter.kind, value.size());
  }
  if (peek() == QLatin1Char('"')) return lexQuoted();
  qsizetype length = 0;
  while (offset_ + length < source_.size() &&
         !QStringLiteral("[](){}|\"").contains(source_.at(offset_ + length))) {
    if (QStringView(source_).mid(offset_ + length).startsWith(QStringLiteral("-)"))) break;
    ++length;
  }
  return take(K::Text, std::max<qsizetype>(1, length));
}

FlowToken FlowchartTokenizer::lexTrapText() {
  if (startsWith(QStringLiteral("\\]"))) { modes_.removeLast(); return take(K::TrapEnd, 2); }
  if (startsWith(QStringLiteral("/]"))) { modes_.removeLast(); return take(K::InvTrapEnd, 2); }
  if (peek() == QLatin1Char('"')) return lexQuoted();
  qsizetype length = 0;
  while (offset_ + length < source_.size() &&
         !QStringLiteral("\\/[](){}\"").contains(source_.at(offset_ + length))) ++length;
  return take(K::Text, std::max<qsizetype>(1, length));
}

FlowToken FlowchartTokenizer::lexEdgeText(QChar delimiter, Mode mode) {
  if (peek() == QLatin1Char('"')) return lexQuoted();
  FlowToken link = tryLink();
  if (link.kind == K::Link) { modes_.removeLast(); return link; }
  qsizetype length = 0;
  while (offset_ + length < source_.size()) {
    const QChar ch = source_.at(offset_ + length);
    if (ch == delimiter) break;
    ++length;
  }
  if (length == 0) length = 1;
  (void)mode;
  return take(K::EdgeText, length);
}

FlowToken FlowchartTokenizer::lexAccessibilityValue(FlowTokenKind kind) {
  qsizetype length = 0;
  while (offset_ + length < source_.size() &&
         source_.at(offset_ + length) != QLatin1Char('\n')) ++length;
  modes_.removeLast();
  if (length == 0) return next();
  return take(kind, length);
}

FlowToken FlowchartTokenizer::lexCallbackName() {
  static const QRegularExpression emptyArgs(QStringLiteral(R"(^\(\s*\))"));
  const auto emptyMatch = emptyArgs.matchView(QStringView(source_).mid(offset_));
  if (emptyMatch.hasMatch()) {
    modes_.removeLast();
    take(K::Unknown, emptyMatch.capturedLength());
    return next();
  }
  if (peek() == QLatin1Char('(')) {
    if (source_.indexOf(QLatin1Char(')'), offset_ + 1) < 0)
      return takeInvalid(FlowchartErrorCode::UnterminatedCallbackArguments,
                         source_.size() - offset_);
    modes_.last() = Mode::CallbackArgs;
    take(K::Unknown, 1);
    return next();
  }
  qsizetype length = 0;
  while (offset_ + length < source_.size() &&
         source_.at(offset_ + length) != QLatin1Char('(')) ++length;
  return take(K::CallbackName, length);
}

FlowToken FlowchartTokenizer::lexCallbackArgs() {
  if (peek() == QLatin1Char(')')) {
    modes_.removeLast();
    take(K::Unknown, 1);
    return next();
  }
  qsizetype length = 0;
  while (offset_ + length < source_.size() &&
         source_.at(offset_ + length) != QLatin1Char(')')) ++length;
  return take(K::CallbackArgs, length);
}

FlowToken FlowchartTokenizer::lexInitial() {
  if (atEnd()) return {K::Eof, {}, line_, column_, offset_};
  const QChar ch = peek();
  if (ch == QLatin1Char('\n')) return takeWhile(K::Newline, [](QChar c) { return c == QLatin1Char('\n'); });
  if (ch.isSpace()) return take(K::Space, 1);
  static const QRegularExpression callPrefix(QStringLiteral(R"(^call\s+)"));
  const auto callMatch = callPrefix.matchView(QStringView(source_).mid(offset_));
  if (callMatch.hasMatch()) {
    modes_.push_back(Mode::CallbackName);
    take(K::Unknown, callMatch.capturedLength());
    return next();
  }
  static const QRegularExpression accTitlePrefix(QStringLiteral(R"(^accTitle\s*:\s*)"));
  static const QRegularExpression accDescrPrefix(QStringLiteral(R"(^accDescr\s*:\s*)"));
  const auto accTitleMatch = accTitlePrefix.matchView(QStringView(source_).mid(offset_));
  if (accTitleMatch.hasMatch()) {
    modes_.push_back(Mode::AccTitleValue);
    return take(K::AccTitle, accTitleMatch.capturedLength(), accTitleMatch.captured());
  }
  const auto accDescrMatch = accDescrPrefix.matchView(QStringView(source_).mid(offset_));
  if (accDescrMatch.hasMatch()) {
    modes_.push_back(Mode::AccDescrValue);
    return take(K::AccDescr, accDescrMatch.capturedLength(), accDescrMatch.captured());
  }
  static const QRegularExpression direction(
      QStringLiteral(R"(^direction\s+(TB|BT|RL|LR|TD)[^\n]*)"));
  const auto directionMatch = direction.matchView(QStringView(source_).mid(offset_));
  if (directionMatch.hasMatch()) {
    const QString value = directionMatch.captured(1);
    K kind = K::DirectionTd;
    if (value == QLatin1String("TB")) kind = K::DirectionTb;
    else if (value == QLatin1String("BT")) kind = K::DirectionBt;
    else if (value == QLatin1String("RL")) kind = K::DirectionRl;
    else if (value == QLatin1String("LR")) kind = K::DirectionLr;
    return take(kind, directionMatch.capturedLength(), directionMatch.captured());
  }
  if (startsWith(QStringLiteral("@{"))) return lexShapeData();
  if (peek() == QLatin1Char('"')) return lexQuoted();

  qsizetype linkIdLength = 0;
  while (offset_ + linkIdLength < source_.size() &&
         !source_.at(offset_ + linkIdLength).isSpace() &&
         source_.at(offset_ + linkIdLength) != QLatin1Char('"')) {
    if (source_.at(offset_ + linkIdLength) == QLatin1Char('@')) {
      const QChar after = offset_ + linkIdLength + 1 < source_.size()
                              ? source_.at(offset_ + linkIdLength + 1) : QChar();
      if (linkIdLength > 0 && after != QLatin1Char('{') && after != QLatin1Char('"'))
        return take(K::LinkId, linkIdLength + 1);
      break;
    }
    ++linkIdLength;
  }

  FlowToken keyword = tryKeyword();
  if (keyword.kind != K::Unknown) return keyword;
  if (QStringLiteral("-=~xo<").contains(ch)) {
    FlowToken link = tryLink();
    if (link.kind != K::Unknown) return link;
  }

  if (startsWith(QStringLiteral(":::"))) return take(K::StyleSeparator, 3);
  if (startsWith(QStringLiteral("((("))) { modes_.push_back(Mode::Text); return take(K::DoubleCircleStart, 3); }
  if (startsWith(QStringLiteral("[["))) { modes_.push_back(Mode::Text); return take(K::SubroutineStart, 2); }
  if (startsWith(QStringLiteral("(["))) { modes_.push_back(Mode::Text); return take(K::StadiumStart, 2); }
  if (startsWith(QStringLiteral("[("))) { modes_.push_back(Mode::Text); return take(K::CylinderStart, 2); }
  if (startsWith(QStringLiteral("[/"))) { modes_.push_back(Mode::TrapText); return take(K::TrapStart, 2); }
  if (startsWith(QStringLiteral("[\\"))) { modes_.push_back(Mode::TrapText); return take(K::InvTrapStart, 2); }
  if (startsWith(QStringLiteral("[|"))) return take(K::VertexWithPropsStart, 2);
  if (startsWith(QStringLiteral("(-"))) { modes_.push_back(Mode::Text); return take(K::EllipseStart, 2); }

  if (ch == QLatin1Char(';')) return take(K::Semi, 1);
  if (ch == QLatin1Char(',')) return take(K::Comma, 1);
  if (ch == QLatin1Char(':')) return take(K::Colon, 1);
  if (ch == QLatin1Char('&')) return take(K::Amp, 1);
  if (ch == QLatin1Char('#')) return take(K::Bracket, 1);
  if (ch == QLatin1Char('%')) return take(K::Percent, 1);
  if (ch == QLatin1Char('*')) return take(K::Multiply, 1);
  if (ch == QLatin1Char('^')) return take(K::Up, 1);
  if (ch == QLatin1Char('v')) return take(K::Down, 1);
  if (ch == QLatin1Char('|')) { modes_.push_back(Mode::Text); return take(K::Pipe, 1); }
  if (ch == QLatin1Char('(')) { modes_.push_back(Mode::Text); return take(K::Ps, 1); }
  if (ch == QLatin1Char('[')) { modes_.push_back(Mode::Text); return take(K::Sqs, 1); }
  if (ch == QLatin1Char('{')) { modes_.push_back(Mode::Text); return take(K::DiamondStart, 1); }
  if (ch == QLatin1Char('<')) return take(K::TagStart, 1);
  if (ch == QLatin1Char('>')) { modes_.push_back(Mode::Text); return take(K::TagEnd, 1); }
  const auto nodeStringLength = [&] {
    qsizetype length = 0;
    while (offset_ + length < source_.size()) {
      const qsizetype at = offset_ + length;
      if (!isAsciiNodeChar(source_.at(at)) && !isNodeHyphen(source_, at)) break;
      ++length;
    }
    return length;
  };
  if (ch.isDigit()) return takeWhile(K::Number, [](QChar c) { return c.isDigit(); });
  if (isAsciiNodeChar(ch) || isNodeHyphen(source_, offset_))
    return take(K::NodeString, nodeStringLength());
  if (ch == QLatin1Char('-')) return take(K::Minus, 1);
  if (ch.unicode() >= 128 && ch.isLetter()) return take(K::UnicodeText, 1);
  if (isUnitStart(ch)) return takeWhile(K::Unit, [](QChar c) { return c.isLetter(); });
  return takeInvalid(FlowchartErrorCode::UnexpectedCharacter, 1);
}

FlowToken FlowchartTokenizer::next() {
  if (atEnd()) return {K::Eof, {}, line_, column_, offset_};
  switch (modes_.last()) {
    case Mode::Dir: return lexDir();
    case Mode::Text: return lexText();
    case Mode::TrapText: return lexTrapText();
    case Mode::EdgeText: return lexEdgeText(QLatin1Char('-'), Mode::EdgeText);
    case Mode::ThickEdgeText: return lexEdgeText(QLatin1Char('='), Mode::ThickEdgeText);
    case Mode::DottedEdgeText: return lexEdgeText(QLatin1Char('.'), Mode::DottedEdgeText);
    case Mode::AccTitleValue: return lexAccessibilityValue(K::AccTitleValue);
    case Mode::AccDescrValue: return lexAccessibilityValue(K::AccDescrValue);
    case Mode::CallbackName: return lexCallbackName();
    case Mode::CallbackArgs: return lexCallbackArgs();
    case Mode::Initial: return lexInitial();
  }
  return takeInvalid(FlowchartErrorCode::UnexpectedCharacter, 1);
}

QVector<FlowToken> FlowchartTokenizer::tokenize() {
  QVector<FlowToken> result;
  for (qsizetype guard = 0; guard < 100000; ++guard) {
    const qsizetype before = offset_;
    FlowToken token = next();
    if (token.kind != K::Eof && offset_ == before)
      throw std::runtime_error((QStringLiteral("flowchart tokenizer stalled at %1:%2 (%3)")
                                    .arg(line_).arg(column_).arg(flowTokenName(token.kind))).toStdString());
    result.push_back(token);
    if (token.kind == K::Eof) break;
    if (guard == 99999)
      throw std::runtime_error("flowchart tokenizer did not make progress");
  }
  return result;
}

}  // namespace muffin::mermaid::flowchart
