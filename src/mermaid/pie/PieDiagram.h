#pragma once

// Native recursive-descent parser + data model for the mermaid 11.16.0 pie
// family. Mirrors the data-struct style of requirement::RequirementDiagram /
// classdiagram::ClassDiagram: parse() returns an immutable PieData holding the
// sections (label + value, insertion order, first-write-wins dedup), the
// showData flag, and the title / accTitle / accDescr directives.
//
// Grammar (mermaid 11.16.0 @mermaid-js/parser PieGrammar, captured verbatim from
// the compiled Langium grammar in chunk-KEIR6QF5.mjs):
//   Pie            : NEWLINE* 'pie' showData?
//                     (TitleAndAccessibilities | PieSection | NEWLINE)*
//   showData       : '?=' keyword 'showData'        (case-sensitive)
//   TitleAndAccessibilities (fragment, +):
//                    (accDescr=ACC_DESCR | accTitle=ACC_TITLE | title=TITLE) EOL
//   PieSection     : label=STRING ':' value=NUMBER_PIE EOL
//   NUMBER_PIE     : FLOAT_PIE | INT_PIE
//                    FLOAT_PIE = /-?[0-9]+\.[0-9]+(?!\.)/
//                    INT_PIE   = /-?(0|[1-9][0-9]*)(?!\.)/
//   STRING         : /"([^"\\]|\\.)*"|'([^'\\]|\\.)*'/
//   TITLE          : /[ \t]*title(?:[ \t ][^\n\r]*?(?=%%)|[ \t ][^\n\r]*|)/
//   ACC_TITLE      : /[ \t]*accTitle[ \t]*:(?:[^\n\r]*?(?=%%)|[^\n\r]*)/
//   ACC_DESCR      : /[ \t]*accDescr(?:[ \t]*:([^\n\r]*?(?=%%)|[^\n\r]*)|\s*{([^}]*)})/
//   SINGLE_LINE_COMMENT (hidden) : /[ \t]*%%[^\n\r]*/
//
// Runtime contract (pieDb.addSection): a NEGATIVE value throws — the check runs
// before the first-write-wins dedup, so a duplicate negative still rejects.
//
// The 33-case accept/reject oracle is frozen in
// tests/fixtures/mermaid/pie-grammar.json; this parser must reproduce every
// verdict (and the accepted DB state). Exact Langium lexer/parser error messages
// are implementation artifacts and are NOT replicated — only the verdicts and
// the parsed section/showData/title/acc state are parity-relevant.

#include <QString>
#include <QVector>

#include <stdexcept>

namespace muffin::mermaid::pie {

// Per-section config that the renderer consumes. The parser does NOT populate
// this — it comes from the `%%{init}%%` directive via the preprocessor's
// config object (pre.config.pie), merged over these mermaid defaults by the
// adapter. Defaults mirror mermaid 11.16.0 defaultConfig.pie.
struct PieConfig {
  qreal textPosition = 0.75;
  qreal donutHole = 0.0;
  QString legendPosition = QStringLiteral("right");
  // "" (none) | "hover" | <label> — drives the pieCircle highlight class.
  QString highlightSlice;
  bool highlightSliceIsString = true;
  bool useMaxWidth = true;
};

struct PieSection {
  QString label;
  double value = 0.0;
};

struct PieData {
  QString title;
  QString accTitle;
  QString accDescr;
  QVector<PieSection> sections;  // insertion order; first-write-wins dedup
  bool showData = false;
};

class PieDiagram {
public:
  // Parse a pie diagram source (front matter / %%{init}%% directives already
  // stripped by the preprocessor; `%%` line/inline comments handled here).
  // Throws PieParseError on any input mermaid 11.16.0 rejects (including a
  // negative slice value, matching pieDb.addSection's runtime throw).
  static PieData parse(const QString& source);
};

// Thrown by parse() on invalid pie syntax or a negative slice value. `line`
// is 1-based for diagnostics (0 when unavailable).
struct PieParseError : std::runtime_error {
  int line = 0;
  PieParseError(const QString& message, int line = 0);
};

// Sum of every section value (the ORIGINAL total; the renderer's <1% filter and
// the percentage labels are both computed against this).
double pieOriginalSum(const QVector<PieSection>& sections);

// Number of sections mermaid would DRAW: those whose share of the original sum
// is >= 1% (value/origSum*100 >= 1). Zero when the original sum is 0.
int pieDrawCount(const QVector<PieSection>& sections);

}  // namespace muffin::mermaid::pie
