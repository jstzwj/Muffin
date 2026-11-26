#pragma once

#include "mermaid/erdiagram/ErLayout.h"
#include "mermaid/erdiagram/ErScene.h"
#include "mermaid/theme/FlowTheme.h"
#include "mermaid/theme/MermaidStyleResolve.h"

#include <QString>
#include <QVector>

namespace muffin::mermaid::er {

// What styles an erDiagram's document besides the diagram itself.
struct ErStyleInput {
  flowtheme::FlowThemeVariables theme;
  QString fontFamily;                       // the theme's, as its stylesheet names it
  QString look = QStringLiteral("classic"); // classic, neo or handDrawn
  QString themeCss;
  QVector<style::ClassDef> classDefs;
  // The color themes paint each entity from their palettes, by its order:
  // the first `themeColorLimit` of them.
  bool colorTheme = false;
  int themeColorLimit = 0;
  // The redux themes write a styled entity's whole node style on every one
  // of its paths; the others write the stroke styles there.
  bool nodeStylesReachEveryPath = false;
  // Mermaid's `htmlLabels`, as its two readers take it.
  bool htmlLabels = true;
  bool relationshipHtmlLabels = true;
  // The neo look strokes every path of a node with the theme's gradient
  // where the theme has one.
  bool useGradient = false;
  QString gradientStart;
  QString gradientStop;
  // The theme's name contains "dark": the flood color of the drop-shadow
  // filter the redux themes name.
  bool darkTheme = false;
  // The handDrawn look: rough.js draws the shapes from this seed. Mermaid's
  // default is 0, with which it draws at random.
  quint32 handDrawnSeed = 0;
};

// The document's style: how its texts are set, what paints its elements, and
// what the two looks beyond the classic one add: the neo look's gradient and
// the handDrawn look's rough.js options, by entity id.
struct ErDocumentStyle {
  ErTextOptions text;
  ErDocumentPaint paint;
  bool neo = false;
  ErGradient gradient;
  bool handDrawn = false;
  QHash<QString, ErRoughEntity> rough;
};

// erDiagram's stylesheet with its theme variables in place: what reaches the
// document before themeCSS, which Mermaid appends to it.
QString erBaseCss(const ErStyleInput& input);

// Resolves the document's style: the stylesheet, the classDefs and the inline
// styles erBox writes, and themeCSS, over the elements of the document.
ErDocumentStyle resolveErDocumentStyle(const ErLayoutInput& layout, const ErStyleInput& input);

// The dash of the relationship lines under the neo look, which Mermaid writes
// along each path once the path exists: the length of a solid one, and as
// many `2 2` as fit a dashed one. It is an inline style, so a rule of
// themeCSS with `!important` still overrides it, which is why the lines are
// resolved again with it rather than written over their paint. Under any
// other look there is nothing to do.
void resolveErLineDashes(ErDocumentStyle& document, const ErLayoutInput& layout, const ErPlacementResult& placement,
                         const ErStyleInput& input);

} // namespace muffin::mermaid::er
