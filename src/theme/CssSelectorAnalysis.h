#pragma once

#include <QString>

namespace muffin {

// Analysed right-most compound of a selector — enough to decide which semantic token it can
// feed and to exclude state pseudo-classes (:hover etc.). Filled by analyzeSelector.
struct SelInfo {
  QString tag;            // rightmost type selector, lowercased ("body","h2",…)
  bool idWrite = false;   // rightmost compound targets #write
  bool classFences = false;  // .md-fences (community code-fence class)
  bool mdFocus = false;      // .md-focus (community editor's focused/active-block class)
  QString pseudoElement;  // "selection","before","after","marker",… (without ::)
  bool hover = false;
  bool focus = false;
  bool visited = false;
  bool active = false;
  bool unsupportedPseudoClass = false;  // structural pseudos like :has/:last-child are not modelled here
  bool nthEven = false;   // :nth-child(even) / :nth-of-type(even)
  // Typora editor-only selector (chrome/UI Muffin never renders). Rules carrying one of
  // these classes are dropped at flatten so their editor-only hacks never reach element
  // matching — see isTyporaEditorOnlyClass.
  bool editorOnly = false;
};

// Extract the last compound of a selector (after the final combinator space/>/+/~),
// respecting (...) and [...]. Defined in CssSelectorAnalysis.cpp.
QString lastCompound(const QString& selector);

// Parse the rightmost compound of `selector` into a SelInfo (tag / #id / .class /
// :pseudo-class / ::pseudo-element). The flat semantic mapper, the flatten pass, and the
// pseudo/hover/state extractors all route through this single chokepoint so a selector
// parsing fix applies everywhere at once. Defined in CssSelectorAnalysis.cpp.
SelInfo analyzeSelector(const QString& selector);

// Grouping key for a pseudo-element rule's host: "#write" → the write surface, the tag name
// otherwise, ".md-fences" for the community code-fence class, or empty when unanchored (rules
// with no host key are skipped — a pseudo with no host can't be painted). Shared by the
// pseudo-element extractor and fromSheet's pseudo predicate. Defined in CssSelectorAnalysis.cpp.
QString pseudoHostKey(const SelInfo& info);

}  // namespace muffin
