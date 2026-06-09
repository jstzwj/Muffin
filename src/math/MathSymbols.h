#pragma once

#include "math/MathParseNode.h"

#include <QString>

namespace muffin::math {

struct MathSymbolInfo {
  QString replacement;
  MathNodeType type = MathNodeType::Ord;
  QString fontClass = QStringLiteral("mathnormal");
  bool known = false;
};

struct WideCharMapping {
  QString baseChar;
  QString fontClass;
};

MathSymbolInfo lookupSymbol(const QString& token);
WideCharMapping wideCharacterFont(uint codePoint);

}  // namespace muffin::math
