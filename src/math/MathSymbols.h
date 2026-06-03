#pragma once

#include "math/MathParseNode.h"

#include <QString>

namespace muffin::math {

struct MathSymbolInfo {
  QString replacement;
  MathNodeType type = MathNodeType::Ord;
  QString fontClass = QStringLiteral("mathit");
  bool known = false;
};

MathSymbolInfo lookupSymbol(const QString& token);

}  // namespace muffin::math
