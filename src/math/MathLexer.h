#pragma once

#include <QString>

namespace muffin::math {

struct MathToken {
  QString text;
  qsizetype position = 0;
  qsizetype endPosition = 0;
};

class MathLexer {
public:
  explicit MathLexer(QString input);

  MathToken peek();
  MathToken next();
  void consume();
  bool atEnd();

private:
  MathToken readToken();

  QString input_;
  qsizetype pos_ = 0;
  bool hasLookahead_ = false;
  MathToken lookahead_;
};

}  // namespace muffin::math
