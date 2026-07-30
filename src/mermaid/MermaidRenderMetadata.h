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
  QSizeF contentSize;

  // SVG-only root element contract. These values are retained beside the
  // scene because they affect serialization, not QPainter geometry.
  bool svgUseMaxWidth = true;
  bool svgArrowMarkerAbsolute = false;
  bool svgDeterministicIds = false;
  QString svgDeterministicIdSeed;

  bool hasVisibleTitle() const { return !title.trimmed().isEmpty(); }
  QString accessibleName() const;
};

qreal measureMermaidTitleWidth(const MermaidRenderMetadata& metadata);
void paintMermaidTitle(const MermaidRenderMetadata& metadata,
                       QPainter& painter, const QRectF& titleRect);

}  // namespace muffin::mermaid
