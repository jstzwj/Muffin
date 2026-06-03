#pragma once

#include <QString>
#include <QVector>

#include <memory>

namespace muffin::math {

enum class MathNodeType {
  Ord,
  Operator,
  Binary,
  Relation,
  Open,
  Close,
  Punct,
  Spacing,
  Group,
  SupSub,
  Fraction,
  Sqrt,
  Accent,
  AccentUnder,
  HorizBrace,
  Underline,
  Overline,
  DelimSizing,
  LeftRight,
  Array,
  Color,
  Styling,
  Sizing,
  Phantom,
  Smash,
  Rule,
  Kern,
  Class,
  Lap,
  RaiseBox,
  VCenter,
  Enclose,
  IncludeGraphics,
  MathChoice,
  Href,
  Html,
  Tag,
  Verb,
  Text,
  Error
};

struct MathParseNode;

struct MathArrayCell;
struct MathArrayColumn;
struct MathArrayLine;

struct MathArrayCell {
  QVector<std::shared_ptr<MathParseNode>> body;
};

struct MathArrayColumn {
  enum class Type { Align, Separator };
  Type type = Type::Align;
  QChar align = QLatin1Char('c');
  QChar separator = QLatin1Char('|');
  qreal pregap = -1.0;
  qreal postgap = -1.0;
};

struct MathArrayLine {
  int beforeRow = 0;
  bool dashed = false;
};

struct MathParseNode {
  MathNodeType type = MathNodeType::Ord;
  QString text;
  QString label;
  QVector<MathParseNode> body;
  QVector<MathParseNode> numerator;
  QVector<MathParseNode> denominator;
  QVector<MathParseNode> base;
  QVector<MathParseNode> sup;
  QVector<MathParseNode> sub;
  QVector<MathParseNode> rootIndex;
  QVector<MathParseNode> display;
  QVector<MathParseNode> script;
  QVector<MathParseNode> scriptScript;
  QVector<MathParseNode> tag;
  QVector<QVector<MathArrayCell>> rows;
  QVector<MathArrayColumn> columns;
  QVector<MathArrayLine> arrayLines;
  QVector<qreal> rowGaps;
  QVector<int> horizontalLines;
  QString columnAlignments;
  QString colSeparationType;
  QString leftDelim;
  QString rightDelim;
  QString color;
  QString style;
  QString size;
  QString width;
  QString height;
  QString totalHeight;
  QString shift;
  QString mathClass;
  QString sourceMathClass;
  QString href;
  QString alt;
  QString borderColor;
  QString backgroundColor;
  QString fontClass;
  int delimiterSize = 0;
  qreal lineThickness = -1.0;
  bool limits = false;
  bool explicitLimits = false;
  bool alwaysHandleSupSub = false;
  bool opSymbol = false;
  bool smashHeight = false;
  bool smashDepth = false;
  bool isOver = false;
  bool addJot = false;
  bool hskipBeforeAndAfter = false;
  qreal arrayStretch = 1.0;
  QString error;
};

}  // namespace muffin::math
