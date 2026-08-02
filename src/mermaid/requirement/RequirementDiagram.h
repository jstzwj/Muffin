#pragma once

// Native recursive-descent parser + DB model for the mermaid 11.16.0
// requirementDiagram family. Mirrors the data-struct style of
// classdiagram::ClassDiagram: parse() returns an immutable RequirementDiagramData
// holding requirements, elements, relationships and the front-matter fields.
//
// Grammar (requirementDiagram-TGXJPOKE.mjs, simplified to the productions that
// affect geometry):
//   - Opener `requirementDiagram`. Optional `direction TB|BT|RL|LR`.
//   - `accTitle:`, `accDescr:` / `accDescr { ... }`. (There is NO inline `title`
//     token — it is a Parse error. Diagram titles come from frontmatter
//     `title:`, handled by the preprocessor's metadata path, not the grammar.)
//   - `#` / `%` line comments.
//   - 6 requirement type keywords → display type strings:
//       requirement→"Requirement", functionalRequirement→"Functional Requirement",
//       interfaceRequirement→"Interface Requirement",
//       performanceRequirement→"Performance Requirement",
//       physicalRequirement→"Physical Requirement",
//       designConstraint→"Design Constraint".
//     Form: `<type> <name> [{ body }]` (optionally `<type> <name> ::: <class>`).
//     Body: `id:`, `text:`, `risk:` (low|medium|high),
//     `verifyMethod:` (analysis|demonstration|inspection|test).
//   - `element <name> [{ body }]` (optionally `::: <class>`). Body: `type:`,
//     `docref:`.
//   - Relationship `src -<type>-> dst` (→ form, src=left) or
//     `src <-<type>- dst` (← form, src=right). 7 types: contains, copies,
//     derives, satisfies, verifies, refines, traces.
//   - `classDef <name> <style>`, `class <node> <class>`,
//     `style <node> <css>` — parsed and stored (classDef/style fully applied is
//     deferred to the expansion phase; this pilot stores them for parity).

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <stdexcept>

namespace muffin::mermaid::requirement {

// Display-type strings emitted by the parser, matching RequirementDB's
// RequirementType enum values exactly.
struct RequirementTypeDisplay {
  static QString requirement() { return QStringLiteral("Requirement"); }
  static QString functionalRequirement() { return QStringLiteral("Functional Requirement"); }
  static QString interfaceRequirement() { return QStringLiteral("Interface Requirement"); }
  static QString performanceRequirement() { return QStringLiteral("Performance Requirement"); }
  static QString physicalRequirement() { return QStringLiteral("Physical Requirement"); }
  static QString designConstraint() { return QStringLiteral("Design Constraint"); }
};

// Risk / verify display strings (RequirementDB.RiskLevel / VerifyType values).
struct RequirementEnumDisplay {
  static QString risk(const QString& keyword);
  static QString verifyMethod(const QString& keyword);
};

struct RequirementNode {
  QString name;
  QString type;             // display type ("Functional Requirement", ...)
  QString requirementId;
  QString text;
  QString risk;             // "" / "Low" / "Medium" / "High"
  QString verifyMethod;     // "" / "Analysis" / "Demonstration" / "Inspection" / "Test"
  QStringList cssClasses = {QStringLiteral("default")};
  QStringList cssStyles;
};

struct ElementNode {
  QString name;
  QString type;             // user-supplied element type (free text)
  QString docRef;
  QStringList cssClasses = {QStringLiteral("default")};
  QStringList cssStyles;
};

struct Relationship {
  QString type;             // "contains" / "copies" / "derives" / ...
  QString src;
  QString dst;
};

struct RequirementDiagramData {
  QString accTitle;
  QString accDescription;
  QString direction = QStringLiteral("TB");
  QVector<RequirementNode> requirements;
  QVector<ElementNode> elements;
  QVector<Relationship> relations;
  // classDef table (id -> declarations). Inline `style` declarations are folded
  // into the node's cssStyles at parse time; classDef resolution is stored for
  // the future expansion phase.
  QHash<QString, QStringList> classDefs;
};

class RequirementDiagram {
public:
  static RequirementDiagram parse(const QString& source);
  const RequirementDiagramData& data() const { return data_; }

private:
  RequirementDiagramData data_;
};

// Thrown by parse() on invalid requirementDiagram syntax — an unrecognized
// line, a duplicate declaration that breaks Map semantics is tolerated, but a
// missing/unclosed body or a stray token surfaces as a parse error (matching
// mermaid's "Parse error" instead of silently producing a Ready scene). Carries
// the 1-based source line for the diagnostic.
struct RequirementParseError : std::runtime_error {
  int line = 0;
  RequirementParseError(const QString& message, int line);
};

// Returns the display type string for a requirement keyword, or empty if the
// keyword is not a recognized requirement type. Used by the parser.
QString requirementTypeDisplay(const QString& keyword);

}  // namespace muffin::mermaid::requirement
