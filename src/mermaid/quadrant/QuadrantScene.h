#pragma once

// Immutable scene for the mermaid 11.16.0 quadrantChart family. Holds the
// deterministic layout (quadrant rects, points, borders, axis labels, title)
// computed by the same formula as quadrantBuilder.ts, and delegates paint() to
// paintQuadrantScene(). Geometry is font-INDEPENDENT and is the byte-parity
// target frozen in tests/fixtures/mermaid/quadrant-geometry.json.
//
// Notable: points render in REVERSE source order (addPoints prepends); a chart
// WITH points forces xAxisPosition "bottom"; the theme's quadrantPointFill is
// the invalid string "hsl(240, 100%, NaN%)" (emitted verbatim for oracle parity;
// the painter falls back to black, the SVG default for an invalid fill).

#include "mermaid/MermaidScene.h"
#include "mermaid/quadrant/QuadrantDiagram.h"

#include <QRectF>
#include <QString>
#include <QVector>

class QPainter;

namespace muffin::mermaid::quadrant {

// Theme values captured live from mermaid 11.16.0 (default theme). The adapter
// swaps in the dark-theme set when the resolved theme is dark. NB the default
// quadrantPointFill is the upstream-invalid "hsl(240, 100%, NaN%)" — emitted
// verbatim for oracle parity; the painter falls back to black.
struct QuadrantSceneStyle {
  QString quadrant1Fill = QStringLiteral("#ECECFF");
  QString quadrant2Fill = QStringLiteral("#f1f1ff");
  QString quadrant3Fill = QStringLiteral("#f6f6ff");
  QString quadrant4Fill = QStringLiteral("#fbfbff");
  QString quadrant1TextFill = QStringLiteral("#131300");
  QString quadrant2TextFill = QStringLiteral("#0e0e00");
  QString quadrant3TextFill = QStringLiteral("#090900");
  QString quadrant4TextFill = QStringLiteral("#040400");
  QString quadrantPointFill = QStringLiteral("hsl(240, 100%, NaN%)");
  QString quadrantPointTextFill = QStringLiteral("#131300");
  QString quadrantXAxisTextFill = QStringLiteral("#131300");
  QString quadrantYAxisTextFill = QStringLiteral("#131300");
  QString quadrantInternalBorderStrokeFill = QStringLiteral("#C7C7F1");
  QString quadrantExternalBorderStrokeFill = QStringLiteral("#C7C7F1");
  QString quadrantTitleFill = QStringLiteral("#131300");
  QString fontFamily = QStringLiteral("Noto Sans");
};

struct QuadrantRect { qreal x, y, width, height; QString fill; QString text; QString textFill; };
struct QuadrantPointG {
  qreal x, y;
  qreal radius;
  QString fill;
  QString stroke;
  QString strokeWidth;  // "Npx" (theme default "0px")
  QString text;
};
struct QuadrantBorder { qreal x1, y1, x2, y2; QString strokeFill; qreal strokeWidth; };
struct QuadrantAxisLabel { QString text; QString fill; qreal x, y; qreal fontSize; int rotation; };

struct QuadrantScene : MermaidScene {
  QRectF sceneBounds() const override { return bounds; }
  void paint(QPainter& painter, const MermaidPaintOptions& options) const override;
  QJsonObject toJsonObject() const override;

  QRectF bounds;
  // Config (deterministic defaults; chartWidth/Height from pre.config).
  qreal chartWidth = 500.0, chartHeight = 500.0;
  QString title;
  QString accTitle, accDescr;
  QVector<QuadrantRect> quadrants;
  QVector<QuadrantPointG> points;       // REVERSE source order (render order)
  QVector<QuadrantBorder> borders;
  QVector<QuadrantAxisLabel> axisLabels;
  // Title placement (empty when no title).
  QString titleText;
  qreal titleX = 0.0, titleY = 0.0;
  QuadrantSceneStyle style;
};

QuadrantScene buildQuadrantScene(const QuadrantData& data,
                                 const QJsonObject& quadrantConfig,
                                 QuadrantSceneStyle style);

}  // namespace muffin::mermaid::quadrant
