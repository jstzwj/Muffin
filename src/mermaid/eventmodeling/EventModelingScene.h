#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/eventmodeling/EventModelingDiagram.h"
#include "mermaid/flowchart/FlowLabel.h"

#include <QJsonValue>
#include <QLineF>
#include <QPair>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace muffin::mermaid::eventmodeling {

struct EventModelingConfig {
  QJsonValue useMaxWidth = true;
  QJsonValue padding = 30.0;
  QJsonValue rowHeight = 32.0;
};

struct EventModelingSceneStyle {
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal rootFontSize = 16.0;
  QString textColor = QStringLiteral("#333");
  QString uiFill = QStringLiteral("white");
  QString uiStroke = QStringLiteral("#dbdada");
  QString processorFill = QStringLiteral("#edb3f6");
  QString processorStroke = QStringLiteral("#b88cbf");
  QString readModelFill = QStringLiteral("#d3f1a2");
  QString readModelStroke = QStringLiteral("#a3b732");
  QString commandFill = QStringLiteral("#bcd6fe");
  QString commandStroke = QStringLiteral("#679ac3");
  QString eventFill = QStringLiteral("#ffb778");
  QString eventStroke = QStringLiteral("#c19a0f");
  QString swimlaneFill = QStringLiteral("rgb(250,250,250)");
  QString swimlaneStroke = QStringLiteral("rgb(240,240,240)");
  QString arrowhead = QStringLiteral("#000000");
  QString relationStroke = QStringLiteral("#000000");
};

// themeCSS overlay for one eventmodeling DOM element. Eventmodeling ships no
// base stylesheet and measures every box through calculateTextDimensions
// with hardcoded config fonts, so themeCSS only repaints; the box labels are
// HTML spans inside foreignObject, where `color` (not `fill`) semantics
// apply and the color chain starts at the initial black.
struct EventModelingElementCss {
  QString fill;
  QString stroke;
  QString strokeWidth;
  QString color;
  QString fontFamily;
  qreal fontSize = -1.0;
  QString fontWeight;
  QString fontStyle;
  qreal opacity = -1.0;
  bool visible = true;
  bool measures = true;
};

// Slots are indexed by emission order (swimlanes, boxes, relations); the
// shared arrowhead marker polygon carries one slot.
struct EventModelingCssOverrides {
  struct Swimlane {
    EventModelingElementCss rect;
    EventModelingElementCss text;
  };
  struct Box {
    EventModelingElementCss rect;
    EventModelingElementCss label;
  };
  bool active = false;
  QVector<Swimlane> swimlanes;
  QVector<Box> boxes;
  QVector<EventModelingElementCss> relations;
  EventModelingElementCss marker;
};

struct EventModelingSwimlaneGeometry {
  int index = 0;
  QString label;
  qreal right = 0.0;
  qreal y = 0.0;
  qreal height = 70.0;
  qreal maxHeight = 70.0;
  QRectF rect;
  QPointF labelPosition;
  EventModelingElementCss rectCss;
  EventModelingElementCss textCss;
};

struct EventModelingBoxGeometry {
  int frameIndex = 0;
  int swimlaneIndex = 0;
  QString frameName;
  QString modelEntityType;
  QString entityIdentifier;
  QRectF rect;
  QRectF foreignObjectRect;
  QString fill;
  QString stroke;
  QString contentHtml;
  flowchart::FlowLabelDocument label;
  qreal rightWithPadding = 0.0;
  EventModelingElementCss rectCss;
  EventModelingElementCss labelCss;
};

struct EventModelingRelationGeometry {
  int sourceBox = -1;
  int targetBox = -1;
  QLineF line;
  QString pathData;
  QString stroke;
  EventModelingElementCss css;
};

struct EventModelingScene final : MermaidScene {
  QRectF sceneBounds() const override { return bounds; }
  QRectF renderBounds() const override { return bounds; }
  void paint(QPainter& painter,
             const MermaidPaintOptions& options) const override;
  QJsonObject toJsonObject() const override;

  QRectF bounds;
  QRectF contentBounds;
  bool useMaxWidth = true;
  qreal padding = 30.0;
  EventModelingConfig config;
  EventModelingSceneStyle style;
  QVector<EventModelingSwimlaneGeometry> swimlanes;
  QVector<EventModelingBoxGeometry> boxes;
  QVector<EventModelingRelationGeometry> relations;
  // The shared arrowhead marker polygon (defs > marker > polygon).
  EventModelingElementCss markerCss;
};

EventModelingScene buildEventModelingScene(const EventModelingData& data,
                                            EventModelingConfig config,
                                            EventModelingSceneStyle style,
                                            const EventModelingCssOverrides* css =
                                                nullptr);

// Frame-type → (fill, stroke) presentation pair, shared with the themeCSS
// DOM model so the adapter can stamp the same presentation attributes.
QPair<QString, QString> eventModelingBoxPaint(const EventModelingFrame& frame,
                                              const EventModelingSceneStyle& style);

}  // namespace muffin::mermaid::eventmodeling
