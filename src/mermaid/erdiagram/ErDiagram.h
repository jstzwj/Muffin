#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <stdexcept>

namespace muffin::mermaid::er {

// Error taxonomy mirrors muffin::mermaid::classdiagram (ClassDiagram.h). The
// stage identifies which pipeline phase raised; the code is machine-readable
// for test assertions. ErSourceSpan is 1-based line / 0-based column to match
// ClassSourceSpan, so token spans map directly onto diagnostic spans.
enum class ErErrorStage { Detector, Lexer, Parser, Semantic, Resource };

enum class ErErrorCode {
  MissingHeader,
  UnexpectedToken,
  MissingClosingBrace,
  MissingRelationTarget,
  InvalidCardinality,
  InvalidAttribute,
  LimitExceeded,
};

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

class ErParseError final : public std::runtime_error {
public:
  explicit ErParseError(ErDiagnostic diagnostic);
  const ErDiagnostic& diagnostic() const noexcept;

private:
  ErDiagnostic diagnostic_;
};

// A single attribute key marker. The upstream `Attribute.keys` is an array, but
// the documented erDiagram subset uses one key per attribute line; the parser
// captures the first key token and a later extension can widen this to a list.
enum class ErAttributeKeyType { None, PrimaryKey, ForeignKey, UniqueKey };

struct ErAttribute {
  QString attributeType;
  QString attributeName;
  QString comment;
  ErAttributeKeyType keyType = ErAttributeKeyType::None;
};

// `id` is the identifier used in relationships (e.g. CUSTOMER). `name` is the
// display label: equals `id` unless an alias `ENTITY "Display Name"` is given,
// in which case id="ENTITY", name="Display Name" (matches upstream
// EntityNode.id / .alias semantics).
struct ErEntity {
  QString id;
  QString name;
  QVector<ErAttribute> attributes;
  QString link;
  QString linkTarget;
  QString tooltip;
  bool haveCallback = false;
  QString cssClasses;     // space-joined classDef names applied via class/cssClass
  QStringList styles;     // inline `style` declarations (key:value)
};

// Crow's-foot cardinality. ExactlyOne => "||" (two ticks), ZeroOrOne => "|o"/"o|"
// (circle + tick), OneOrMore => "}|"/"|{" (fork + tick), ZeroOrMore =>
// "}o"/"o{" (circle + fork). MD_PARENT (markdown parent, token 70 in the jison
// grammar) is a markdown-specific edge case and is intentionally deferred.
enum class ErCardinality { ExactlyOne, ZeroOrOne, OneOrMore, ZeroOrMore };

// `identifying` mirrors upstream Identification: true  => "--" solid
// (IDENTIFYING), false => ".." dotted (NON_IDENTIFYING). roleA/roleB are the
// optional quoted role strings adjacent to entityA/entityB
// (`A "roleA" }o--o{ "roleB" B : label`). `label` is the text after `:`. If a
// trailing quoted "comment" follows the label it is captured in `comment`.
struct ErRelationship {
  QString id;
  QString entityA;
  QString entityB;
  ErCardinality cardA = ErCardinality::ExactlyOne;
  ErCardinality cardB = ErCardinality::ExactlyOne;
  bool identifying = true;
  QString roleA;
  QString roleB;
  QString label;
  QString comment;
};

struct ErDiagramData {
  QString title;
  QString accTitle;
  QString accDescription;
  QVector<ErEntity> entities;
  QVector<ErRelationship> relationships;
  // classDef table (id -> declarations), exposed for the runtime style cascade.
  QHash<QString, QStringList> classDefs;
};

// Resource caps checked during parse, mirroring ClassLimits. Defaults are the
// documented mermaid hard limits scaled for the native subset.
struct ErLimits {
  int maxEntities = 100;
  int maxRelationships = 500;
  int maxAttributesPerEntity = 100;
  int maxTextSize = 100000;
};

class ErDiagram {
public:
  static ErDiagram parse(const QString& source, ErLimits limits = {});
  const ErDiagramData& data() const { return data_; }
  QJsonObject toJson() const;

private:
  ErDiagramData data_;
};

QString erErrorStageName(ErErrorStage stage);
QString erErrorCodeName(ErErrorCode code);
QString formatErDiagnostic(const ErDiagnostic& diagnostic);

}  // namespace muffin::mermaid::er
