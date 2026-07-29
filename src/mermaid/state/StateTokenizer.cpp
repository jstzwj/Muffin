#include "mermaid/state/StateTokenizer.h"

#include <QHash>

#include <stdexcept>

namespace muffin::mermaid::state {
namespace {
const QHash<QString, StateTokenKind>& keywords() {
  static const QHash<QString, StateTokenKind> values = {
      {QStringLiteral("state"), StateTokenKind::State},
      {QStringLiteral("direction"), StateTokenKind::Direction},
      {QStringLiteral("acctitle"), StateTokenKind::AccTitle},
      {QStringLiteral("accdescr"), StateTokenKind::AccDescr},
      {QStringLiteral("scale"), StateTokenKind::Scale},
      {QStringLiteral("width"), StateTokenKind::Width},
      {QStringLiteral("note"), StateTokenKind::Note},
      {QStringLiteral("end"), StateTokenKind::End},
      {QStringLiteral("left"), StateTokenKind::Left},
      {QStringLiteral("right"), StateTokenKind::Right},
      {QStringLiteral("of"), StateTokenKind::Of},
      {QStringLiteral("as"), StateTokenKind::As},
      {QStringLiteral("classdef"), StateTokenKind::ClassDef},
      {QStringLiteral("class"), StateTokenKind::Class},
      {QStringLiteral("style"), StateTokenKind::Style},
      {QStringLiteral("linkstyle"), StateTokenKind::LinkStyle},
      {QStringLiteral("click"), StateTokenKind::Click},
      {QStringLiteral("href"), StateTokenKind::Href},
      {QStringLiteral("default"), StateTokenKind::Default},
  };
  return values;
}
}

QChar StateTokenizer::peek(qsizetype ahead) const {
  const qsizetype index = offset_ + ahead;
  return index < source_.size() ? source_.at(index) : QChar{};
}

void StateTokenizer::advance(QStringView text) {
  for (const QChar ch : text) {
    if (ch == QLatin1Char('\n')) { ++line_; column_ = 0; }
    else ++column_;
  }
  offset_ += text.size();
}

StateToken StateTokenizer::take(StateTokenKind kind, qsizetype length,
                                bool stripQuotes) {
  StateToken token{kind, source_.mid(offset_, length), offset_, length, line_, column_};
  advance(QStringView(source_).mid(offset_, length));
  if (kind == StateTokenKind::Newline) lineHasContent_ = false;
  else if (kind != StateTokenKind::Semi) lineHasContent_ = true;
  if (stripQuotes && token.text.size() >= 2)
    token.text = token.text.mid(1, token.text.size() - 2);
  return token;
}

StateToken StateTokenizer::next() {
  while (!atEnd()) {
    if (peek() == QLatin1Char('\r'))
      return take(StateTokenKind::Newline, peek(1) == QLatin1Char('\n') ? 2 : 1);
    if (peek() == QLatin1Char('\n')) return take(StateTokenKind::Newline, 1);
    if (peek() == QLatin1Char(';')) return take(StateTokenKind::Semi, 1);
    if (peek().isSpace()) {
      const qsizetype start = offset_;
      while (!atEnd() && peek().isSpace() && peek() != QLatin1Char('\r') &&
             peek() != QLatin1Char('\n'))
        advance(QStringView(source_).mid(offset_, 1));
      if (offset_ != start) continue;
    }
    if ((!lineHasContent_ && peek() == QLatin1Char('#')) ||
        (peek() == QLatin1Char('%') && peek(1) == QLatin1Char('%') &&
         peek(2) != QLatin1Char('{'))) {
      while (!atEnd() && peek() != QLatin1Char('\r') && peek() != QLatin1Char('\n'))
        advance(QStringView(source_).mid(offset_, 1));
      continue;
    }
    const QStringView rest = QStringView(source_).mid(offset_);
    if (rest.startsWith(QLatin1String("stateDiagram-v2"), Qt::CaseInsensitive))
      return take(StateTokenKind::Header, 15);
    if (rest.startsWith(QLatin1String("stateDiagram"), Qt::CaseInsensitive))
      return take(StateTokenKind::Header, 12);
    if (rest.startsWith(QLatin1String("hide empty description"), Qt::CaseInsensitive))
      return take(StateTokenKind::HideEmpty, 22);
    if (rest.startsWith(QLatin1String("-->"))) return take(StateTokenKind::Arrow, 3);
    if (rest.startsWith(QLatin1String(":::")))
      return take(StateTokenKind::StyleSeparator, 3);
    if (rest.startsWith(QLatin1String("[*]"))) return take(StateTokenKind::StartEnd, 3);
    if (rest.startsWith(QLatin1String("--"))) return take(StateTokenKind::Concurrent, 2);
    if (peek() == QLatin1Char('"')) {
      qsizetype length = 1;
      while (offset_ + length < source_.size() && source_.at(offset_ + length) != QLatin1Char('"'))
        ++length;
      if (offset_ + length < source_.size()) ++length;
      return take(StateTokenKind::String, length);
    }
    switch (peek().unicode()) {
      case '{': return take(StateTokenKind::LBrace, 1);
      case '}': return take(StateTokenKind::RBrace, 1);
      case ':': return take(StateTokenKind::Colon, 1);
      case ',': return take(StateTokenKind::Comma, 1);
      default: break;
    }
    const qsizetype start = offset_;
    while (!atEnd() && !peek().isSpace() && peek() != QLatin1Char(';') &&
           peek() != QLatin1Char('{') && peek() != QLatin1Char('}') &&
           peek() != QLatin1Char(':') && peek() != QLatin1Char(',') &&
           peek() != QLatin1Char('"')) {
      const QStringView current = QStringView(source_).mid(offset_);
      if (current.startsWith(QLatin1String("-->")) ||
          current.startsWith(QLatin1String(":::")) ||
          current.startsWith(QLatin1String("--"))) break;
      advance(QStringView(source_).mid(offset_, 1));
    }
    if (offset_ == start) return take(StateTokenKind::Invalid, 1);
    StateToken token{StateTokenKind::Identifier, source_.mid(start, offset_ - start),
                     start, offset_ - start, line_,
                     column_ - static_cast<int>(offset_ - start)};
    const auto it = keywords().constFind(token.text.toLower());
    if (it != keywords().cend()) token.kind = it.value();
    lineHasContent_ = true;
    return token;
  }
  return {StateTokenKind::Eof, {}, offset_, 0, line_, column_};
}

QVector<StateToken> StateTokenizer::tokenize() {
  QVector<StateToken> tokens;
  do { tokens.append(next()); } while (tokens.last().kind != StateTokenKind::Eof);
  return tokens;
}

StateTokenCursor::StateTokenCursor(const QVector<StateToken>& tokens,
                                   qsizetype begin, qsizetype end)
    : tokens_(&tokens), position_(begin),
      end_(end < 0 ? tokens.size() : std::min(end, tokens.size())) {}

const StateToken& StateTokenCursor::peek(qsizetype lookahead) const {
  const qsizetype index = std::min(position_ + lookahead, end_ - 1);
  return tokens_->at(std::max<qsizetype>(0, index));
}
const StateToken& StateTokenCursor::consume() { return tokens_->at(position_++); }
const StateToken& StateTokenCursor::expect(StateTokenKind kind) {
  if (atEnd() || peek().kind != kind) throw std::runtime_error("unexpected state token");
  return consume();
}
bool StateTokenCursor::match(StateTokenKind kind) {
  if (atEnd() || peek().kind != kind) return false;
  ++position_; return true;
}
bool StateTokenCursor::atEnd() const {
  return position_ >= end_ || peek().kind == StateTokenKind::Eof;
}
void StateTokenCursor::skipSeparators() {
  while (!atEnd() && (peek().kind == StateTokenKind::Newline ||
                      peek().kind == StateTokenKind::Semi)) ++position_;
}
StateTokenCursor StateTokenCursor::consumeLine() {
  const qsizetype begin = position_;
  while (position_ < end_ && peek().kind != StateTokenKind::Newline &&
         peek().kind != StateTokenKind::Semi && peek().kind != StateTokenKind::Eof)
    ++position_;
  return StateTokenCursor(*tokens_, begin, position_);
}
QString StateTokenCursor::raw(const QString& source) const {
  return rawFrom(source, 0);
}
QString StateTokenCursor::rawFrom(const QString& source, qsizetype position) const {
  const qsizetype begin = std::min(position_ + position, end_);
  if (begin >= end_) return {};
  const StateToken& first = tokens_->at(begin);
  const StateToken& last = tokens_->at(end_ - 1);
  return source.mid(first.offset, last.offset + last.length - first.offset).trimmed();
}
QStringList StateTokenCursor::parseList(const QString& source) {
  QStringList values;
  while (!atEnd()) {
    const qsizetype begin = position_;
    while (!atEnd() && peek().kind != StateTokenKind::Comma) ++position_;
    StateTokenCursor item(*tokens_, begin, position_);
    values.append(item.raw(source).trimmed());
    match(StateTokenKind::Comma);
  }
  return values;
}

QString stateTokenName(StateTokenKind kind) {
  switch (kind) {
    case StateTokenKind::Eof: return QStringLiteral("EOF");
    case StateTokenKind::Newline: return QStringLiteral("NL");
    case StateTokenKind::Header: return QStringLiteral("SD");
    case StateTokenKind::Identifier: return QStringLiteral("ID");
    case StateTokenKind::String: return QStringLiteral("STRING");
    case StateTokenKind::Arrow: return QStringLiteral("-->");
    case StateTokenKind::Invalid: return QStringLiteral("INVALID");
    default: return QStringLiteral("KEYWORD");
  }
}

}  // namespace muffin::mermaid::state
