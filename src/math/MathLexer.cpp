#include "math/MathLexer.h"

namespace muffin::math {

MathLexer::MathLexer(QString input) : input_(std::move(input)) {}

MathToken MathLexer::peek() {
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
  hasLookahead_ = false;
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
  return {QString(ch), start, pos_};
}

}  // namespace muffin::math
