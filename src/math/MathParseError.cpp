#include "math/MathParseError.h"

namespace muffin::math {

MathParseError::MathParseError(QString message) : std::runtime_error(message.toStdString()), message_(std::move(message)) {}

MathParseError::MathParseError(QString message, QString tokenText, qsizetype position)
    : std::runtime_error(message.toStdString()),
      message_(std::move(message)),
      tokenText_(std::move(tokenText)),
      position_(position),
      endPosition_(position_ + tokenText_.size()) {}

MathParseError::MathParseError(QString message, QString tokenText, qsizetype position, qsizetype endPosition)
    : std::runtime_error(message.toStdString()),
      message_(std::move(message)),
      tokenText_(std::move(tokenText)),
      position_(position),
      endPosition_(endPosition) {}

QString MathParseError::message() const {
  return message_;
}

QString MathParseError::tokenText() const {
  return tokenText_;
}

qsizetype MathParseError::position() const {
  return position_;
}

qsizetype MathParseError::endPosition() const {
  return endPosition_;
}

}  // namespace muffin::math
