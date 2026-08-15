#pragma once

#include <QString>
#include <QVector>

#include <optional>
#include <stdexcept>

namespace muffin::mermaid::c4 {

enum class C4ErrorKind { Lexer, Parser, Runtime };

class C4ParseError : public std::runtime_error {
public:
  C4ParseError(C4ErrorKind kind, int line, int column, QString token,
               const QString& message);

  C4ErrorKind kind;
  int line;
  int column;
  QString token;
};

struct C4Element {
  QString alias;
  QString label;
  QString description;
  QString technology;
  QString type;
  QString typeC4Shape;
  QString parentBoundary;
  QString nodeType;
  std::optional<QString> sprite;
  std::optional<QString> tags;
  std::optional<QString> link;
  std::optional<QString> backgroundColor;
  std::optional<QString> fontColor;
  std::optional<QString> borderColor;
  std::optional<QString> shadowing;
  std::optional<QString> shape;
  std::optional<QString> legendText;
  std::optional<QString> legendSprite;
};

struct C4Relation {
  QString type;
  QString from;
  QString to;
  QString label;
  QString technology;
  QString description;
  std::optional<QString> sprite;
  std::optional<QString> tags;
  std::optional<QString> link;
  std::optional<QString> textColor;
  std::optional<QString> lineColor;
  std::optional<int> offsetX;
  std::optional<int> offsetY;
};

struct C4Data {
  QString c4Type;
  QString title;
  QString accTitle;
  QString accDescr;
  bool wrap = false;
  int shapeInRow = 4;
  int boundaryInRow = 2;
  QVector<C4Element> shapes;
  QVector<C4Element> boundaries;
  QVector<C4Relation> relations;
};

class C4Diagram {
public:
  static C4Data parse(const QString& source, bool wrap = false);
};

}  // namespace muffin::mermaid::c4
