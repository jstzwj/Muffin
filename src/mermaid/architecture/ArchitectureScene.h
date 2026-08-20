#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/architecture/ArchitectureDiagram.h"

#include <QJsonValue>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace muffin::mermaid::architecture {

struct ArchitectureConfig {
  QJsonValue useMaxWidth = true;
  QJsonValue padding = 40.0;
  QJsonValue iconSize = 80.0;
  QJsonValue fontSize = 16.0;
  QJsonValue randomize = false;
  QJsonValue nodeSeparation = 75.0;
  QJsonValue idealEdgeLengthMultiplier = 1.5;
  QJsonValue edgeElasticity = 0.45;
  QJsonValue numIter = 2500.0;
  QJsonValue seed = 1.0;
  QString svgId = QStringLiteral("architecture-native");
};

struct ArchitectureSceneStyle {
  QString fontFamily = QStringLiteral("Noto Sans");
  QString textColor = QStringLiteral("#333");
  QString edgeColor = QStringLiteral("#333333");
  QString arrowColor = QStringLiteral("#333333");
  QString edgeWidth = QStringLiteral("3");
  QString groupBorderColor =
      QStringLiteral("hsl(240, 60%, 86.2745098039%)");
  QString groupBorderWidth = QStringLiteral("2px");
};

enum class ArchitectureNodeKind { Service, Junction };

// themeCSS overlay for one architecture DOM element. `hasBox` carries the
// ancestor display chain: setupGraphViewbox reads the svg root getBBox, whose
// union drops display:none subtrees, so a `.architecture-edges { display:none }`
// removes the edge layer from the viewBox (the fcose layout itself stays
// CSS-independent — Cytoscape sizes nodes from config iconSize, never the DOM).
struct ArchitectureElementCss {
  QString fill;
  QString stroke;
  QString strokeWidth;
  QString fontFamily;
  qreal fontSize = -1.0;
  QString fontWeight;
  QString fontStyle;
  qreal opacity = -1.0;
  qreal fillOpacity = -1.0;
  qreal strokeOpacity = -1.0;
  bool visible = true;
  bool hasBox = true;
};

// Slots follow the DOM: the three layer groups, then per-edge (line + first
// arrow + label), per-service (group + label + iconless node-bkg path),
// per-junction (group + invisible rect), per-group (rect.node-bkg + label).
struct ArchitectureCssOverrides {
  struct Edge {
    ArchitectureElementCss line;
    ArchitectureElementCss arrow;
    ArchitectureElementCss label;
  };
  struct Node {
    ArchitectureElementCss group;
    ArchitectureElementCss label;
    ArchitectureElementCss nodeBkg;
  };
  struct Junction {
    ArchitectureElementCss group;
    ArchitectureElementCss rect;
  };
  struct Group {
    ArchitectureElementCss rect;
    ArchitectureElementCss label;
  };
  bool active = false;
  ArchitectureElementCss edgesLayer;
  ArchitectureElementCss servicesLayer;
  ArchitectureElementCss groupsLayer;
  QVector<Edge> edges;
  QVector<Node> nodes;
  QVector<Junction> junctions;
  QVector<Group> groups;
};

struct ArchitectureNodeGeometry {
  ArchitectureNodeKind kind = ArchitectureNodeKind::Service;
  QString id;
  QString icon;
  QString iconText;
  QString title;
  QString parent;
  QPointF topLeft;
  QRectF localBounds;
  QRectF paintedBounds;
  ArchitectureElementCss groupCss;
  ArchitectureElementCss labelCss;
  ArchitectureElementCss nodeBkgCss;
};

struct ArchitectureGroupGeometry {
  QString id;
  QString icon;
  QString title;
  QString parent;
  QRectF rect;
  ArchitectureElementCss rectCss;
  ArchitectureElementCss labelCss;
};

struct ArchitectureArrowGeometry {
  QChar direction;
  QPointF position;
  QPolygonF polygon;
};

struct ArchitectureEdgeGeometry {
  QString id;
  QString lhsId;
  QString rhsId;
  QString title;
  QVector<QPointF> points;
  QString pathData;
  QRectF bounds;
  QRectF labelBounds;
  QVector<ArchitectureArrowGeometry> arrows;
  ArchitectureElementCss lineCss;
  ArchitectureElementCss arrowCss;
  ArchitectureElementCss labelCss;
};

struct ArchitectureScene final : MermaidScene {
  QRectF bounds;
  QRectF rasterBounds;
  QRectF contentBounds;
  QString viewBoxAttribute;
  bool useMaxWidth = true;
  ArchitectureConfig config;
  ArchitectureSceneStyle style;
  QVector<ArchitectureNodeGeometry> nodes;
  QVector<ArchitectureGroupGeometry> groups;
  QVector<ArchitectureEdgeGeometry> edges;

  QRectF sceneBounds() const override { return bounds; }
  QRectF renderBounds() const override { return rasterBounds; }
  // The exported root carries the browser's exact fractional viewBox
  // (upstream setupGraphViewbox reads svgBBox + padding with no translate —
  // the theme-css oracle locks values like -327.624 … 727.186), not the
  // raster-rounded canvas ints.
  QRectF svgClientViewBox() const override { return bounds; }
  void paint(QPainter& painter,
             const MermaidPaintOptions& options = {}) const override;
  QJsonObject toJsonObject() const override;
};

ArchitectureScene buildArchitectureScene(const ArchitectureData& data,
                                           ArchitectureConfig config,
                                           ArchitectureSceneStyle style,
                                           const ArchitectureCssOverrides* css =
                                               nullptr);

}  // namespace muffin::mermaid::architecture
