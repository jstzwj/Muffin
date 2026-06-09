#include "math/MathLexer.h"

namespace muffin::math {

MathLexer::MathLexer(QString input) : input_(std::move(input)) {}

MathToken MathLexer::peek() {
  if (!pushed_.isEmpty()) {
    return pushed_.last();
  }
  if (!hasLookahead_) {
    lookahead_ = readToken();
    hasLookahead_ = true;
  }
  return lookahead_;
}

MathToken MathLexer::next() {
  const MathToken token = peek();
  consume();
  return token;
}

void MathLexer::consume() {
  if (!pushed_.isEmpty()) {
    pushed_.removeLast();
    return;
  }
  hasLookahead_ = false;
}

void MathLexer::pushFront(MathToken token) {
  pushed_.push_back(std::move(token));
}

QString MathLexer::readVerbBody(bool& starred, bool& ok, MathToken& delimiter) {
  starred = false;
  ok = false;
  if (hasLookahead_ || !pushed_.isEmpty() || pos_ >= input_.size()) {
    delimiter = {QStringLiteral("EOF"), pos_, pos_};
    return {};
  }
  if (input_.at(pos_) == QLatin1Char('*')) {
    starred = true;
    ++pos_;
  }
  if (pos_ >= input_.size()) {
    delimiter = {QStringLiteral("EOF"), pos_, pos_};
    return {};
  }

  const qsizetype delimiterStart = pos_;
  const QChar delimiterChar = input_.at(pos_++);
  delimiter = {QString(delimiterChar), delimiterStart, pos_};
  const qsizetype bodyStart = pos_;
  while (pos_ < input_.size() && input_.at(pos_) != delimiterChar && input_.at(pos_) != QLatin1Char('\n')) {
    ++pos_;
  }
  if (pos_ < input_.size() && input_.at(pos_) == delimiterChar) {
    const QString body = input_.mid(bodyStart, pos_ - bodyStart);
    ++pos_;
    ok = true;
    return body;
  }
  return input_.mid(bodyStart, pos_ - bodyStart);
}

bool MathLexer::atEnd() {
  return peek().text == QStringLiteral("EOF");
}

MathToken MathLexer::readToken() {
  while (pos_ < input_.size() && input_.at(pos_).isSpace()) {
    ++pos_;
  }
  if (pos_ >= input_.size()) {
    return {QStringLiteral("EOF"), pos_, pos_};
  }

  const qsizetype start = pos_;
  const QChar ch = input_.at(pos_++);
  if (ch == QLatin1Char('\\')) {
    if (pos_ < input_.size() && (input_.at(pos_).isLetter() || input_.at(pos_) == QLatin1Char('@'))) {
      while (pos_ < input_.size() && (input_.at(pos_).isLetter() || input_.at(pos_) == QLatin1Char('@'))) {
        ++pos_;
      }
      return {input_.mid(start, pos_ - start), start, pos_};
    }
    if (pos_ < input_.size()) {
      ++pos_;
      return {input_.mid(start, pos_ - start), start, pos_};
    }
  }
  // Combine surrogate pairs into a single token, matching KaTeX's Lexer.ts
  // regex [\uD800-\uDBFF][\uDC00-\uDFFF] which matches them as one token.
  if (ch.isHighSurrogate() && pos_ < input_.size() && input_.at(pos_).isLowSurrogate()) {
    ++pos_;
    return {input_.mid(start, 2), start, pos_};
  }
  return {QString(ch), start, pos_};
}

}  // namespace muffin::math
