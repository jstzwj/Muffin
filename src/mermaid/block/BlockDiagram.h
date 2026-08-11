#pragma once

// Native parser/database projection for Mermaid 11.16.0 block diagrams.

#include <QString>
#include <QStringList>
#include <QVector>

#include <stdexcept>

namespace muffin::mermaid::block {

enum class BlockErrorKind { Lexer, Parser, Runtime };

class BlockParseError final : public std::runtime_error {
public:
  BlockParseError(BlockErrorKind kind, int line, int column, QString token,
                  const QString& message);

  BlockErrorKind kind;
  int line;
  int column;
  QString token;
};

struct BlockNode {
  QString id;
  QString type = QStringLiteral("na");
  QString label;
  bool hasLabel = false;
  int width = 0;
  bool hasWidth = false;
  int widthInColumns = 1;
  bool hasWidthInColumns = false;
  int columns = -1;
  bool hasColumns = false;
  QStringList directions;
  QStringList classes;
  QStringList styles;
  QVector<BlockNode> children;
};

struct BlockEdge {
  QString id;
  QString start;
  QString end;
  QString label;
  QString thickness = QStringLiteral("normal");
  QString pattern = QStringLiteral("solid");
  QString arrowTypeStart = QStringLiteral("arrow_open");
  QString arrowTypeEnd;
};

struct BlockClass {
  QString id;
  QStringList styles;
  QStringList textStyles;
};

struct BlockData {
  BlockNode root;
  QVector<BlockNode> blocks;
  QVector<BlockNode> flat;
  QVector<BlockEdge> edges;
  QVector<BlockClass> classes;
};

class BlockDiagram {
public:
  static BlockData parse(const QString& source);
};

}  // namespace muffin::mermaid::block
