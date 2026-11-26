#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include "mermaid/DiagramError.h"
#include <stdexcept>

namespace muffin::mermaid::treemap {

enum class TreemapErrorKind { Lexer, Parser, Runtime };

// The kind's name in the diagnostic code, "gantt-lexer-error" and the like (see kindErrorCode).
inline QStringView kindName(TreemapErrorKind kind) {
  switch (kind) {
  case TreemapErrorKind::Lexer:
    return u"lexer";
  case TreemapErrorKind::Parser:
    return u"parse";
  case TreemapErrorKind::Runtime:
    return u"runtime";
  }
  return u"parse";
}

class TreemapParseError final : public DiagramError {
public:
  DiagramErrorInfo info() const override;
  TreemapParseError(TreemapErrorKind kind, int line, int column, QString token, const QString& message);

  TreemapErrorKind kind = TreemapErrorKind::Parser;
  int line = 1;
  int column = 1;
  QString token;
};

struct TreemapClassDefinition {
  QString id;
  QStringList styles;
};

struct TreemapNode {
  QString name;
  int level = 0;
  bool hasValue = false;
  double value = 0.0;
  QString classSelector;
  QStringList cssCompiledStyles;
  QVector<int> children;
};

struct TreemapData {
  QString title;
  QString accTitle;
  QString accDescr;
  bool hasTitleDirective = false;
  QVector<TreemapNode> nodes;
  QVector<int> roots;
  QVector<TreemapClassDefinition> classes;
};

class TreemapDiagram {
public:
  static TreemapData parse(const QString& source);
};

} // namespace muffin::mermaid::treemap
