#pragma once

// Native parser + data model for the mermaid 11.16.0 quadrantChart family.
// Mirrors pie::PieDiagram / requirement::RequirementDiagram: parse() returns an
// immutable QuadrantData. The frozen 21-case oracle is in
// tests/fixtures/mermaid/quadrant-grammar.json (accept/reject captured live from
// mermaid 11.16.0); this parser reproduces every verdict.
//
// Grammar (quadrantDiagram-ABIIQ3AL.mjs, case-insensitive jison lexer; the
// detector regex /^\s*quadrantChart/ is case-SENSITIVE so the header must be
// exactly "quadrantChart" — lowercase is a no-diagram reject at detection):
//   - Header `quadrantChart`.
//   - `x-axis Left` | `x-axis Left --> Right` (--> = two+ dashes + >).
//   - `y-axis Bottom` | `y-axis Bottom --> Top`.
//   - `quadrant-1`..`quadrant-4 <text>` (centered quadrant labels).
//   - `title <text>`, `accTitle: <text>`, `accDescr: <text>` | `accDescr {<text>}`.
//   - Points: `<label>: [ <x> , <y> ]` (optionally `: <className>`). x,y ∈ [0,1]:
//     exactly `1`, `0`, or `0.<digits>`. `1.5` / `-0.1` / `2` / `.5` are lexical
//     errors. Points render in REVERSE source order (quadrantBuilder.addPoints
//     prepends).
//   - `%%` line comments. `classDef`/style support is deferred (the frozen
//     oracle has only classDef rejects; valid classDef is a future enhancement).

#include <QString>
#include <QVector>

#include <stdexcept>

namespace muffin::mermaid::quadrant {

// Point styles (quadrantDb.parseStyles): comma-separated `key: value`. Each
// field is empty/unset unless declared. Validation rejects unknown keys and bad
// hex / non-numeric radius / non-Npx stroke-width, matching upstream errors.
// Used by both classDef and point-inline stylesOpt (inline overrides classDef).
struct QuadrantStyles {
  QString color;         // hex (fill) or empty
  QString strokeColor;   // hex or empty
  QString strokeWidth;   // "Npx" or empty
  int radius = -1;       // -1 = unset
};

struct QuadrantPoint {
  QString label;
  double x = 0.0;  // [0,1]
  double y = 0.0;  // [0,1]
  QString className;        // set via `<label>:::<class>: [...]`; "" when unclassed
  QuadrantStyles inlineStyles;  // set via `[x, y] key: value, ...` after the bracket
};

struct QuadrantClassDef {
  QString name;
  QuadrantStyles styles;
};

struct QuadrantData {
  QString title;
  QString accTitle;
  QString accDescr;
  QString xAxisLeftText, xAxisRightText;
  QString yAxisBottomText, yAxisTopText;
  QString quadrant1Text, quadrant2Text, quadrant3Text, quadrant4Text;
  QVector<QuadrantPoint> points;  // source order; the scene reverses for render
  QVector<QuadrantClassDef> classDefs;
};

class QuadrantDiagram {
public:
  static QuadrantData parse(const QString& source);
};

struct QuadrantParseError : std::runtime_error {
  int line = 0;
  QuadrantParseError(const QString& message, int line = 0);
};

}  // namespace muffin::mermaid::quadrant
