#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/eventmodeling/EventModelingDiagram.h"
#include "mermaid/flowchart/FlowLabel.h"

#include <QJsonValue>
#include <QLineF>
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

struct EventModelingSwimlaneGeometry {
  int index = 0;
  QString label;
  qreal right = 0.0;
  qreal y = 0.0;
  qreal height = 70.0;
  qreal maxHeight = 70.0;
  QRectF rect;
  QPointF labelPosition;
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
};

struct EventModelingRelationGeometry {
  int sourceBox = -1;
  int targetBox = -1;
  QLineF line;
  QString pathData;
  QString stroke;
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
};

EventModelingScene buildEventModelingScene(const EventModelingData& data,
                                            EventModelingConfig config,
                                            EventModelingSceneStyle style);

}  // namespace muffin::mermaid::eventmodeling
