#pragma once

// Requirement text-style resolution + text-transform engine (Commit 3).
//
// Resolves the upstream labelStyle text properties (font-size / font-weight /
// font-style / font-family / line-height / letter-spacing / word-spacing /
// text-decoration / text-transform / color) from a node's event-ordered
// `cssStyles` (last value per key wins) into a typed RequirementTextStyle that
// feeds the Commit-2 FlowLabelDocument fields + the measure/paint font args.
//
// The font-size + font-weight resolution replicates mermaid 11.16.0's 3-DOM-layer
// labelStyle cascade (foreignObject class rule + DIV inline + SPAN inline): em
// compounds N^3 x root, ex/ch compound through the same 3 layers against each
// layer's grown font (theme font at L1, node font at L2-3), bolder/lighter step
// per layer, and Chrome clamps computed font-size to 10000px per layer. Probed in
// G:/github/req-probe/STEP0F_REPORT.md (§1.3, §4, §0F++ item 2; 55/55 verifier).
//
// Per CLAUDE.md / the lupdate convention this header has no namespace block; the
// declarations are fully qualified.

#include <QColor>
#include <QFont>
#include <QString>
#include <QStringList>

namespace muffin::mermaid::requirement {

enum class RequirementTextTransform { None, UpperCase, LowerCase, Capitalize };

// Resolved per-node text style. Sentinel defaults mean "use the scene/theme base":
//   fontSizePx < 0  => theme font-size; == 0 => collapse (no measure/paint).
//   lineHeightPx < 0 => theme lineHeight (fontSize*1.5); == 0 => collapse;
//                       lineHeightNormal true => QFontMetricsF natural height.
//   fontFamily empty => theme family. color invalid => theme textColor.
struct RequirementTextStyle {
  QString fontFamily;
  qreal fontSizePx = -1.0;
  QFont::Weight fontWeight = QFont::Normal;  // resolved (bolder/lighter compounded)
  // True iff a VALID font-weight was declared: normal/bold/bolder/lighter, a
  // 1..1000 numeric, or a CSS-wide keyword (inherit/initial/unset/revert/
  // revert-layer — probe: all five resolve to 400 and suppress the default bold).
  // The requirementBox name row is font-weight:bold by DEFAULT (reqTitle), but a
  // declared node font-weight wins on BOTH name and body rows (probe:
  // font-weight:100 -> name AND body both 100). So the name-row default bold is
  // applied ONLY when this is false (unset, or a truly garbage value e.g. "foo"
  // -> inert -> inherit reqTitle).
  bool fontWeightResolved = false;
  QFont::Style fontStyle = QFont::StyleNormal;
  qreal lineHeightPx = -1.0;
  bool lineHeightNormal = false;
  qreal letterSpacingPx = 0.0;
  qreal wordSpacingPx = 0.0;
  bool underline = false;
  bool overline = false;
  bool strikeOut = false;
  RequirementTextTransform transform = RequirementTextTransform::None;
  QColor color;
};

// Resolve a node's text style from event-ordered cssStyles (last value per key
// wins) over the theme base. Pure: identical inputs yield identical output, so it
// is called from BOTH RequirementLayout measure and RequirementScene build without
// divergence. themeLineHeightPx is the theme default (= themeFontSize * 1.5).
RequirementTextStyle resolveRequirementTextStyle(
    const QStringList& cssStyles,
    const QString& themeFontFamily, qreal themeFontSize,
    QFont::Weight themeFontWeight, qreal themeLineHeightPx);

// Apply text-transform to a full label-row source string BEFORE parseFlowLabel.
// upper/lower case the non-`$$...$$` segments (Unicode default case mapping,
// ß -> SS); capitalize uses Unicode word boundaries (QTextBoundaryFinder::Word),
// Markdown markers `*` / backtick transparent, `$$...$$` math spans preserved and
// treated as a word boundary; only the first cased char of each word is titled.
QString applyRequirementTextTransform(QString source, RequirementTextTransform tf);

}  // namespace muffin::mermaid::requirement
