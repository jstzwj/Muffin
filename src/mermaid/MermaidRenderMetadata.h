#pragma once

#include <QRectF>
#include <QSizeF>
#include <QString>

class QPainter;

namespace muffin::mermaid {

// Presentation metadata shared by every native Mermaid family. The semantic
// scene remains diagram-specific; title and accessibility data travel beside
// it so editor, print, raster export, and the future SVG exporter cannot drift.
struct MermaidRenderMetadata {
  QString diagramType;
  QString roleDescription;
  QString cssClass;  // SVG root class suffix (e.g. "flowchart", "erDiagram")
  QString title;
  QString accessibleTitle;
  QString accessibleDescription;
  QString titleColor = QStringLiteral("#333333");
  QString fontFamily = QStringLiteral("Arial");
  qreal titleFontSize = 18.0;
  qreal titleTopMargin = 25.0;
  qreal titleHeight = 0.0;
  qreal diagramPadding = 0.0;
  // The family viewbox padding that also pads the title box above the
  // content (upstream insertTitle + setupGraphViewbox: the title baseline
  // sits titleTopMargin above the content bbox and the viewbox adds the
  // family padding around the union). Families whose padding is already
  // folded into their scene bounds (state) pass their padding here instead
  // of diagramPadding.
  qreal titleBandPadding = -1.0;  // < 0: use diagramPadding
  QSizeF contentSize;

  // SVG-only root element contract. These values are retained beside the
  // scene because they affect serialization, not QPainter geometry.
  bool svgUseMaxWidth = true;
  bool svgEmitViewBox = true;
  bool svgArrowMarkerAbsolute = false;
  bool svgDeterministicIds = false;
  // Most families always expose an SVG <title>, falling back to "Mermaid
  // diagram". Families whose upstream renderer discards metadata set this
  // false and retain only role/roledescription on the root.
  bool svgEmitAccessibleTitle = true;
  QString svgDeterministicIdSeed;

  bool hasVisibleTitle() const { return !title.trimmed().isEmpty(); }
  QString accessibleName() const;
};

qreal measureMermaidTitleWidth(const MermaidRenderMetadata& metadata);
void paintMermaidTitle(const MermaidRenderMetadata& metadata,
                       QPainter& painter, const QRectF& titleRect);

}  // namespace muffin::mermaid
