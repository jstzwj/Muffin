#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <stdexcept>

namespace muffin::mermaid::treemap {

enum class TreemapErrorKind { Lexer, Parser, Runtime };

class TreemapParseError final : public std::runtime_error {
public:
  TreemapParseError(TreemapErrorKind kind, int line, int column,
                    QString token, const QString &message);

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
  static TreemapData parse(const QString &source);
};

} // namespace muffin::mermaid::treemap
