#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/kanban/KanbanDiagram.h"
#include "mermaid/rough/RoughOps.h"

#include <QFont>
#include <QJsonValue>
#include <QLineF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace muffin::mermaid::kanban {

struct KanbanConfig {
  // Raw JS values are load-bearing: sectionWidth uses `|| 200`, padding uses
  // `?? 8` followed by arithmetic Number coercion, and useMaxWidth is consumed
  // through ordinary JavaScript truthiness.
  QJsonValue sectionWidth = 200.0;
  QJsonValue padding = 10.0;  // renderer actually reads mindmap.padding
  QJsonValue useMaxWidth = true;  // renderer actually reads mindmap.useMaxWidth
  bool htmlLabels = true;
  bool markdownAutoWrap = true;
  QString ticketBaseUrl;
  quint32 handDrawnSeed = 7;
};

struct KanbanSceneStyle {
  QString themeName = QStringLiteral("default");
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal fontSize = 16.0;
  QString textColor = QStringLiteral("#333");
  QString background = QStringLiteral("white");
  QString nodeBorder = QStringLiteral("#9370DB");
  QString shadowColor = QStringLiteral("#b9b9b9");
  qreal shadowOpacity = 1.0;
  qreal shadowOffsetX = 1.0;
  qreal shadowOffsetY = 2.0;
  bool dropShadowEnabled = true;
  bool darkMode = false;
  qreal rawThemeColorLimit = 12.0;
  int themeColorRuleCount = 12;
  QVector<QString> cScale;
  QVector<QString> cScaleInv;
  QVector<QString> cScaleLabel;
};

// Resolved themeCSS declarations for one element, shared by the scene builder
// (label measurement fonts) and the painter. Empty strings mean "no CSS
// opinion" — the base theme paint stands. `visible` is displayed() including
// ancestors; `hasBox` follows the display:none chain because kanban's final
// svg.getBBox() (unlike timeline's render-time aggregate) does drop
// undisplayed geometry.
struct KanbanElementCss {
  QString fill;
  QString stroke;
  QString strokeWidth;
  QString color;
  QString fontFamily;
  QString fontWeight;
  qreal fontSize = -1.0;
  qreal opacity = -1.0;
  bool visible = true;
  bool hasBox = true;
};

// themeCSS overlay resolved against a faithful model of the kanban DOM.
struct KanbanCssOverrides {
  bool active = false;
  struct Section {
    KanbanElementCss cluster;  // g.cluster (opacity/display surface)
    KanbanElementCss box;      // the section <rect>
    KanbanElementCss label;    // span.nodeLabel in .cluster-label
  };
  struct ItemLabel {
    KanbanElementCss group;    // g.label
    KanbanElementCss bkg;      // the zero-sized label background <rect>
    KanbanElementCss span;     // span.nodeLabel (.markdown-node-label on titles)
  };
  struct Item {
    KanbanElementCss node;     // g.node (opacity/display surface)
    KanbanElementCss box;      // rect.basic.label-container
    QVector<ItemLabel> labels;  // title, ticket, assigned
    KanbanElementCss priority;  // the priority strike <line>
  };
  QVector<Section> sections;
  QVector<Item> items;
};

struct KanbanLabelGeometry {
  QString source;
  flowchart::FlowLabelDocument document;
  QRectF bounds;
  QString fill;
  bool html = true;
  bool centered = false;
  KanbanElementCss css;
  qreal fontSize = 16.0;
  QString fontFamily;
  QFont::Weight fontWeight = QFont::Normal;
};

struct KanbanSectionGeometry {
  QString id;
  int column = 0;
  QString look;
  QRectF shapeBounds;
  QRectF paintedBounds;
  QString fill;
  QString stroke;
  qreal strokeWidth = 1.0;
  bool handDrawn = false;
  bool dropShadow = false;
  rough::Drawable roughDrawable;
  KanbanLabelGeometry label;
  KanbanElementCss clusterCss;
  KanbanElementCss boxCss;
};

struct KanbanItemGeometry {
  QString id;
  QString parentId;
  QPointF position;
  QRectF localBounds;
  QRectF bounds;
  QString fill;
  QString stroke;
  qreal strokeWidth = 1.0;
  qreal radius = 5.0;
  KanbanLabelGeometry title;
  KanbanLabelGeometry ticket;
  KanbanLabelGeometry assigned;
  bool priorityVisible = false;
  QLineF priorityLine;
  QString priorityStroke;
  qreal priorityStrokeWidth = 4.0;
  QString href;
  KanbanElementCss nodeCss;
  KanbanElementCss boxCss;
  KanbanElementCss priorityCss;
};

struct KanbanScene : MermaidScene {
  QRectF sceneBounds() const override { return bounds; }
  QRectF renderBounds() const override {
    return rasterBounds.isValid() ? rasterBounds : bounds;
  }
  void paint(QPainter& painter, const MermaidPaintOptions& options) const override;
  QJsonObject toJsonObject() const override;
  const QVector<InteractionRegion>& interactionRegions() const override {
    return interactions;
  }

  QRectF bounds;
  // Puppeteer's element screenshot uses the integer CSS client box even when
  // rough.js leaves a fractional SVG viewBox. Classic/neo retain `bounds`.
  QRectF rasterBounds;
  QRectF contentBounds;
  bool useMaxWidth = true;
  KanbanConfig config;
  KanbanSceneStyle style;
  QVector<KanbanSectionGeometry> sections;
  QVector<KanbanItemGeometry> items;
  QVector<InteractionRegion> interactions;
};

KanbanScene buildKanbanScene(const KanbanData& data, KanbanConfig config,
                             KanbanSceneStyle style,
                             const KanbanCssOverrides* css = nullptr);

}  // namespace muffin::mermaid::kanban
