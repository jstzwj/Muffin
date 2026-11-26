#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include "mermaid/DiagramError.h"

namespace muffin::mermaid::er {

// Error taxonomy of the erDiagram parser. The stage tells which step raised;
// the code is machine-readable.
enum class ErErrorStage { Detector, Lexer, Parser, Resource };

enum class ErErrorCode {
  MissingHeader,   // the text does not begin with erDiagram
  UnexpectedToken, // a token the grammar does not allow here, or a character no lexer rule takes
  LimitExceeded,   // an ErLimits bound
};

// A source range. Line is 1-based, column 0-based in UTF-16 units: Mermaid's
// own counting, so a diagnostic can be compared with its.
struct ErSourceSpan {
  qsizetype offset = -1;
  qsizetype length = 0;
  int line = 1;
  int column = 0;
};

struct ErDiagnostic {
  ErErrorStage stage = ErErrorStage::Parser;
  ErErrorCode code = ErErrorCode::UnexpectedToken;
  ErSourceSpan span;
  QString production;
  QString actual;
  QString detail;
  QStringList expected;
};

class ErParseError final : public DiagramError {
public:
  DiagramErrorInfo info() const override;
  explicit ErParseError(ErDiagnostic diagnostic);
  const ErDiagnostic& diagnostic() const noexcept;

private:
  ErDiagnostic diagnostic_;
};

// One attribute row of an entity.
struct ErAttribute {
  QString attributeType;
  QString attributeName;
  QStringList keys; // PK, FK and UK in the order written, as written ("pk", "Pk")
  QString comment;
};

// An entity. `id` is the name relationships use. `alias` is what the table
// shows in its place: `a[Customer]` gives id "a" and alias "Customer".
struct ErEntity {
  QString id;   // entity-<name>-<n>: what the relationships and the drawing call it
  QString name;
  QString alias;
  QVector<ErAttribute> attributes;
  QString link;
  QString linkTarget;
  QString tooltip;
  bool haveCallback = false;
  QString cssClasses; // space-separated; empty when the entity has no class
  QStringList styles; // the declarations of a `style` statement

  QString label() const { return alias.isEmpty() ? name : alias; }
};

// Crow's-foot cardinality at one end of a relationship.
// ExactlyOne "||" (two ticks), ZeroOrOne "|o" / "o|" (circle and tick),
// OneOrMore "}|" / "|{" (fork and tick), ZeroOrMore "}o" / "o{" (circle and fork).
// MdParent is `u` before the connector, the marker of a Markdown parent; it
// parses only in one accidental form and Mermaid draws no marker for it.
enum class ErCardinality { ExactlyOne, ZeroOrOne, OneOrMore, ZeroOrMore, MdParent };

// A relationship `A <cardA> -- or .. <cardB> B : label`. `identifying` is true
// for the solid `--` line and false for the dotted `..` line.
// Both ends are entity ids. Mermaid numbers the relationships into edge ids
// only when it hands them to the layout (buildErLayoutInput).
struct ErRelationship {
  QString entityA;
  QString entityB;
  ErCardinality cardA = ErCardinality::ExactlyOne; // at entityA
  ErCardinality cardB = ErCardinality::ExactlyOne; // at entityB
  bool identifying = true;
  QString label; // the role: the text after the colon
};

struct ErDiagramData {
  QString accTitle;
  QString accDescription;
  QString direction = QStringLiteral("TB"); // TB, BT, RL or LR
  QVector<ErEntity> entities;
  QVector<ErRelationship> relationships;
  // classDef table: class id -> its accumulated declarations.
  QHash<QString, QStringList> classDefs;
};

// Resource bounds checked while parsing.
struct ErLimits {
  int maxEntities = 100;
  int maxRelationships = 500;
  int maxAttributesPerEntity = 100;
  int maxTextSize = 100000;
};

// Mermaid's erDiagram grammar (src/diagrams/er/parser/erDiagram.jison, as built
// in mermaid 11.16.0) and the database its actions fill.
//
// `source` is the text Mermaid hands the parser: the preprocessed diagram,
// with Diagram.fromText's preparation applied (see parse()).
class ErDiagram {
public:
  static ErDiagram parse(const QString& source, ErLimits limits = {});
  const ErDiagramData& data() const { return data_; }

private:
  ErDiagramData data_;
};

QString erErrorStageName(ErErrorStage stage);
QString erErrorCodeName(ErErrorCode code);
QString formatErDiagnostic(const ErDiagnostic& diagnostic);

} // namespace muffin::mermaid::er
