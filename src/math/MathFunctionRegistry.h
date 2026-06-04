#pragma once

#include "math/MathParseNode.h"

#include <QHash>
#include <QString>
#include <QVector>

namespace muffin::math {

enum class MathFunctionArgType {
  Color,
  Size,
  Url,
  Raw,
  Original,
  HBox,
  Primitive,
  Math,
  Text
};

enum class MathFunctionHandlerKind {
  Accent,
  AccentUnder,
  BeginEnvironment,
  Color,
  DelimSizing,
  Enclose,
  Fraction,
  HorizBrace,
  Href,
  Html,
  IncludeGraphics,
  Kern,
  Lap,
  LeftRight,
  MathChoice,
  MathClass,
  Operator,
  OperatorName,
  Overline,
  Phantom,
  RaiseBox,
  Rule,
  Sizing,
  Smash,
  Sqrt,
  Stack,
  Styling,
  Tag,
  Text,
  Underline,
  Url,
  VCenter,
  Verb,
  XArrow
};

enum class MathFunctionBuilderKind {
  Accent,
  AccentUnder,
  Array,
  Color,
  DelimSizing,
  Enclose,
  Fraction,
  HorizBrace,
  Href,
  Html,
  IncludeGraphics,
  Kern,
  Lap,
  LeftRight,
  MathChoice,
  Operator,
  Overline,
  Phantom,
  RaiseBox,
  Rule,
  Sizing,
  Smash,
  Sqrt,
  SupSub,
  Styling,
  Tag,
  Text,
  Underline,
  VCenter,
  Verb,
  XArrow
};

struct MathFunctionSpec {
  QString typeName;
  QVector<QString> names;
  int numArgs = 0;
  int numOptionalArgs = 0;
  QVector<MathFunctionArgType> argTypes;
  bool allowedInArgument = false;
  bool allowedInText = false;
  bool allowedInMath = true;
  bool infix = false;
  bool primitive = false;
  bool requiresTrust = false;
  QString strictCategory;
  MathFunctionHandlerKind handlerKind = MathFunctionHandlerKind::Text;
  MathFunctionBuilderKind builderKind = MathFunctionBuilderKind::Text;
  MathNodeType resultType = MathNodeType::Ord;
  int delimiterSize = 0;
  MathNodeType delimiterNodeType = MathNodeType::Ord;
};

class MathFunctionRegistry {
public:
  static const MathFunctionSpec* lookup(const QString& name);
  static QVector<MathFunctionSpec> specs();
  static QVector<QString> names();

private:
  static const QHash<QString, MathFunctionSpec>& functions();
};

}  // namespace muffin::math
