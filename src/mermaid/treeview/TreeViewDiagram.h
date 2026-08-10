#pragma once

#include <QString>
#include <QVector>

#include <stdexcept>

namespace muffin::mermaid::treeview {

enum class TreeViewErrorKind { Lexer, Parser, Preprocess };

class TreeViewParseError : public std::runtime_error {
public:
  TreeViewParseError(const QString& message, int line, int column,
                     TreeViewErrorKind kind);

  int line = 0;
  int column = 0;
  TreeViewErrorKind kind = TreeViewErrorKind::Parser;
};

struct TreeViewNode {
  int id = 0;
  int level = 0;
  QString name;
  bool directory = false;
  bool hasIcon = false;
  QString icon;
  QString cssClass;
  QString description;
  QVector<int> children;
};

struct TreeViewData {
  QString title;
  QString accTitle;
  QString accDescr;
  QVector<TreeViewNode> nodes;
  int rootIndex = 0;
};

class TreeViewDiagram {
public:
  static TreeViewData parse(const QString& source);
};

}  // namespace muffin::mermaid::treeview
