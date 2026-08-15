#pragma once

#include <QString>
#include <QVector>

#include <limits>
#include <stdexcept>

namespace muffin::mermaid::railroad {

enum class RailroadDialect { Direct, Ebnf, Abnf, Peg };
enum class RailroadErrorKind { Lexer, Parser, Runtime };

class RailroadParseError : public std::runtime_error {
public:
  RailroadParseError(RailroadErrorKind kind, int line, int column,
                     QString token, const QString& message);

  RailroadErrorKind kind;
  int line;
  int column;
  QString token;
};

enum class RailroadNodeType {
  Terminal,
  NonTerminal,
  Sequence,
  Choice,
  Optional,
  Repetition,
  Special
};

// The four source grammars normalize to this exact railroadDb AST. Compound
// nodes keep their children in source order; Optional/Repetition use child[0].
// Repetition.max and separator are retained even though Mermaid 11.16's
// renderer currently only branches on min == 0.
struct RailroadNode {
  RailroadNodeType type = RailroadNodeType::Terminal;
  QString text;
  QVector<RailroadNode> children;
  qreal min = 0.0;
  qreal max = std::numeric_limits<qreal>::infinity();
  QVector<RailroadNode> separator;
};

struct RailroadRule {
  QString name;
  RailroadNode definition;
  QString comment;
};

struct RailroadData {
  RailroadDialect dialect = RailroadDialect::Direct;
  QString title;
  QString accTitle;
  QString accDescr;
  QVector<RailroadRule> rules;
};

class RailroadDiagram {
public:
  static RailroadData parse(const QString& source, RailroadDialect dialect);
};

}  // namespace muffin::mermaid::railroad
