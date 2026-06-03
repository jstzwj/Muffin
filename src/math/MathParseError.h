#pragma once

#include <QString>

#include <stdexcept>

namespace muffin::math {

class MathParseError : public std::runtime_error {
public:
  explicit MathParseError(QString message);
  MathParseError(QString message, QString tokenText, qsizetype position);
  MathParseError(QString message, QString tokenText, qsizetype position, qsizetype endPosition);

  QString message() const;
  QString tokenText() const;
  qsizetype position() const;
  qsizetype endPosition() const;

private:
  QString message_;
  QString tokenText_;
  qsizetype position_ = -1;
  qsizetype endPosition_ = -1;
};

}  // namespace muffin::math
