#include "math/MathSettings.h"

#include "math/MathLexer.h"
#include "math/MathParseError.h"

#include <QDebug>

namespace muffin::math {

namespace {

QString messageWithCode(const QString& prefix, const QString& errorCode, const QString& errorMessage) {
  return QStringLiteral("%1: %2 [%3]").arg(prefix, errorMessage, errorCode);
}

}  // namespace

bool MathSettings::strictEnabled() const {
  return strict != MathStrictMode::Ignore || strictHandler;
}

void MathSettings::reportNonstrict(const QString& errorCode, const QString& errorMessage, const MathToken* token) const {
  MathStrictMode mode = strict;
  if (strictHandler) {
    mode = strictHandler(errorCode, errorMessage, token);
  }

  if (mode == MathStrictMode::Ignore) {
    return;
  }

  const QString message = messageWithCode(QStringLiteral("LaTeX-incompatible input"), errorCode, errorMessage);
  if (mode == MathStrictMode::Error) {
    if (token != nullptr) {
      throw MathParseError(message, token->text, token->position, token->endPosition);
    }
    throw MathParseError(message);
  }

  qWarning() << message;
}

bool MathSettings::shouldApplyStrictBehavior(const QString& errorCode, const QString& errorMessage, const MathToken* token) const {
  MathStrictMode mode = strict;
  if (strictHandler) {
    mode = strictHandler(errorCode, errorMessage, token);
  }

  if (mode == MathStrictMode::Warn) {
    qWarning() << messageWithCode(QStringLiteral("LaTeX-incompatible input"), errorCode, errorMessage);
  }

  return mode == MathStrictMode::Error;
}

bool MathSettings::isTrusted(const MathTrustContext& context) const {
  if (trustHandler) {
    return trustHandler(context);
  }
  return trust;
}

}  // namespace muffin::math
