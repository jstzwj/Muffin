#pragma once

// Immutable scene for the mermaid 11.16.0 pie family. Holds the deterministic
// D3 pie/arc geometry (slice arc paths, angles, radii, label centroids), the
// legend census, and the title/accessibility text, and delegates paint() to
// paintPieScene().
//
// Geometry is computed font-INDEPENDENTLY and is the byte-parity target frozen
// in tests/fixtures/mermaid/pie-geometry.json (the arc path `d` strings, radii,
// angles, counts reproduce d3 exactly). Legend/title pixel positions and the
// canvas width are font-coupled (legend text width) and are NOT byte-parity
// targets — they are painted for the pixel golden only.
//
// Mirrors classdiagram::ClassScene / requirement::RequirementScene: a struct
// inheriting MermaidScene with QRectF bounds + element arrays + a style struct.

#include "mermaid/MermaidScene.h"
#include "mermaid/pie/PieDiagram.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <QRectF>

class QPainter;

namespace muffin::mermaid::pie {

struct PieSceneStyle {
  // 12-color cyclic palette = theme pie1..pie12 (scaleOrdinal range). Cycled by
  // GLOBAL section index (filtered slices still consume their color slot). An
  // empty entry means "no fill attribute" (mermaid's dark theme leaves pie12
  // unset, so the 12th slice paints no fill — reproduced verbatim).
  QStringList palette;
  // Stroke / opacity defaults mirror mermaid 11.16.0 default-theme fallbacks
  // (chunk-WYO6CB5R.mjs): pieStrokeColor/ pieOuterStrokeColor default "black",
  // widths "2px", pieOpacity "0.7".
  QString outerStrokeColor = QStringLiteral("black");   // pieOuterStrokeColor
  qreal outerStrokeWidth = 2.0;                          // pieOuterStrokeWidth (paint, CSS)
  // Upstream computes the outer-circle RADIUS from parseFontSize(pieOuterStrokeWidth)
  // (the leading-integer prefix), NOT the CSS paint width above (pieDiagram:157).
  // For "2px" both are 2, but they diverge for non-px/non-integer overrides, so the
  // geometry width is tracked separately and feeds outerRingRadius below.
  qreal outerStrokeWidthGeom = 2.0;                      // parseFontSize(pieOuterStrokeWidth)
  QString sliceStrokeColor = QStringLiteral("black");   // pieStrokeColor
  qreal sliceStrokeWidth = 2.0;                          // pieStrokeWidth
  qreal pieOpacity = 0.7;                                // pieOpacity
  QString titleColor = QStringLiteral("#333333");        // pieTitleTextColor
  QString sectionTextColor = QStringLiteral("#131300");   // pieSectionTextColor
  QString legendTextColor = QStringLiteral("#131300");    // pieLegendTextColor
  QString fontFamily = QStringLiteral("Noto Sans");
  // Font sizes (theme fallbacks): pieTitleTextSize 25px, section/legend 17px.
  qreal titleFontSize = 25.0;
  qreal sectionFontSize = 17.0;
  qreal legendFontSize = 17.0;
};

// One drawn slice's geometry. `pathD` is the d3 arc path string (byte-parity
// target); angles/radii/centroid are the font-independent fields the geometry
// oracle asserts. Coordinates are GROUP-LOCAL (centered at the pie center,
// matching the oracle's pathD / labelTransform).
struct PieSliceGeometry {
  QString label;
  double value = 0.0;
  int colorIndex = 0;        // global section index % palette.size()
  QString fill;              // palette[colorIndex] (may be empty => no fill)
  QString className = QStringLiteral("pieCircle");
  QString pathD;             // d3 arc path `d` (3-decimal serialization)
  qreal startAngleDeg = 0.0;
  qreal endAngleDeg = 0.0;
  qreal midAngleDeg = 0.0;
  qreal outerRadius = 0.0;
  qreal innerRadius = 0.0;   // donut inner radius (0 for solid slices)
  qreal labelRadius = 0.0;
  qreal centroidX = 0.0;     // label centroid, group-local (full precision)
  qreal centroidY = 0.0;
  QString percentage;        // "38%" — (value/origSum*100).toFixed(0) + "%"
  qreal rawPercentage = 0.0; // value/origSum*100, rounded to 3
};

// One legend row (ALL sections, not just drawn). `text` is "label" or, when
// showData is on, "label [value]".
struct PieLegendEntry {
  QString label;
  QString text;
  QString fill;
  int colorIndex = 0;
};

struct PieScene : MermaidScene {
  QRectF sceneBounds() const override { return bounds; }
  void paint(QPainter& painter, const MermaidPaintOptions& options) const override;
  QJsonObject toJsonObject() const override;

  QRectF bounds;
  // Fixed chart constants (pieRenderer.ts draw()).
  qreal margin = 40.0;
  qreal height = 450.0;
  qreal pieWidth = 450.0;
  qreal centerX = 225.0;
  qreal centerY = 225.0;
  qreal radius = 185.0;
  qreal outerRingRadius = 186.0;   // radius + outerStrokeWidth/2
  qreal legendRectSize = 18.0;
  qreal legendSpacing = 4.0;
  qreal legendHeight = 22.0;       // legendRectSize + legendSpacing
  // Resolved config (4 live fields + the effective donut hole / inner radius).
  qreal textPosition = 0.75;
  qreal donutHole = 0.0;
  qreal effectiveDonutHole = 0.0;
  qreal donutInnerRadius = 0.0;
  qreal labelRadius = 138.75;      // radius * textPosition
  QString legendPosition = QStringLiteral("right");
  QString highlightSlice;
  bool showData = false;
  bool useMaxWidth = true;
  // Text / accessibility.
  QString title;
  QString accTitle;
  QString accDescr;
  // Geometry (font-independent).
  QVector<PieSliceGeometry> slices;   // drawn slices (>= 1%), source order
  QVector<PieLegendEntry> legends;    // ALL sections, source order
  double originalSum = 0.0;
  double filteredSum = 0.0;
  // Computed canvas width (font-coupled — legend text width dependent).
  qreal totalWidth = 450.0;
  qreal totalHeight = 450.0;  // grows by n*legendHeight for top/bottom legendPosition
  // Longest legend text advance (font-coupled), measured by the adapter so the
  // painter can place legends identically and the bounds/canvas agree.
  qreal longestLegendWidth = 0.0;
  PieSceneStyle style;
};

// Build the immutable scene from parsed data + resolved config + theme style.
PieScene buildPieScene(const PieData& data, const PieConfig& config, PieSceneStyle style);

// d3-path 3-decimal coordinate serializer: round to 3 decimals, strip trailing
// zeros, "-0" -> "0". Exposed for unit tests.
QString formatPieCoord(qreal v);

}  // namespace muffin::mermaid::pie
