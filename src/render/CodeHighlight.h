#pragma once

#include <QColor>
#include <QString>
#include <QVector>

namespace muffin {

enum class CodeHighlightRole {
  Plain,
  Comment,
  Keyword,
  String,
  Number,
  Function,
  Type,
  Constant,
  Variable,
  Property,
  Operator,
  Punctuation,
  Preprocessor,
  Escape,
};

struct CodeHighlightSpan {
  qsizetype start = 0;
  qsizetype end = 0;
  CodeHighlightRole role = CodeHighlightRole::Plain;
};

}  // namespace muffin
