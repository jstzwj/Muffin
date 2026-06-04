#pragma once

#include <QString>
#include <QVector>

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
  void pushFront(MathToken token);
  QString readVerbBody(bool& starred, bool& ok, MathToken& delimiter);
  bool atEnd();

private:
  MathToken readToken();

  QString input_;
  qsizetype pos_ = 0;
  bool hasLookahead_ = false;
  MathToken lookahead_;
  QVector<MathToken> pushed_;
};

}  // namespace muffin::math
