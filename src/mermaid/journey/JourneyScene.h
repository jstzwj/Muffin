#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/journey/JourneyDiagram.h"

#include <QRectF>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>

class QPainter;

namespace muffin::mermaid::journey {

struct JourneyConfig {
  bool useMaxWidth = true;
  qreal diagramMarginX = 50.0;
  qreal diagramMarginY = 10.0;
  qreal leftMargin = 150.0;
  qreal maxLabelWidth = 360.0;
  qreal width = 150.0;
  qreal height = 50.0;
  qreal boxTextMargin = 5.0;
  qreal taskFontSize = 14.0;
  qreal taskFontLineStep = 14.0;
  QString taskFontFamily = QStringLiteral("Open Sans");
  qreal taskMargin = 50.0;
  qreal rectWidth = 150.0;
  qreal rectHeight = 50.0;
  QJsonValue diagramMarginXRaw = 50.0;
  QJsonValue diagramMarginYRaw = 10.0;
  QJsonValue leftMarginRaw = 150.0;
  QJsonValue taskMarginRaw = 50.0;
  QString textPlacement = QStringLiteral("fo");
  QString titleColor;
  QString titleFontFamily = QStringLiteral("trebuchet ms");
  qreal titleFontSize = 64.0;
};

struct JourneySceneStyle {
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal fontSize = 16.0;
  QString textColor = QStringLiteral("#333");
  QString faceColor = QStringLiteral("#FFF8DC");
  QStringList fillTypes;
};

struct JourneyActor {
  QString name;
  QStringList lines;
  QString color;
  int position = 0;
  qreal y = 60.0;
  qreal maxLineWidth = 0.0;
};

struct JourneySectionGeometry {
  QRectF rect;
  QPointF oldTextAnchor;
  QPointF tspanTextAnchor;
  QString text;
  QString presentationFill;
  QString fill;
  bool cssFillActive = false;
  int colorIndex = 0;
};

struct JourneyTaskGeometry {
  QRectF rect;
  QPointF oldTextAnchor;
  QPointF tspanTextAnchor;
  QVector<qreal> actorCenters;
  QString text;
  QString section;
  QString presentationFill;
  QString fill;
  bool cssFillActive = false;
  int colorIndex = 0;
  double score = 0.0;
  QPointF faceCenter;
  QStringList people;
};

struct JourneyScene : MermaidScene {
  QRectF sceneBounds() const override { return bounds; }
  QRectF renderBounds() const override { return bounds; }
  void paint(QPainter& painter, const MermaidPaintOptions& options) const override;
  QJsonObject toJsonObject() const override;
  const QVector<InteractionRegion>& interactionRegions() const override {
    return interactions;
  }

  QRectF bounds;
  QRectF upstreamViewBox;
  qreal upstreamRootHeight = 0.0;
  qreal leftMarginResolved = 0.0;
  qreal canvasWidth = 0.0;
  qreal baseHeight = 0.0;
  QString title;
  QString accTitle;
  QString accDescr;
  JourneyConfig config;
  JourneySceneStyle style;
  QVector<JourneyActor> actors;
  JourneyActor prototypeActor;
  bool hasPrototypeActor = false;
  QVector<JourneySectionGeometry> sections;
  QVector<JourneyTaskGeometry> tasks;
  QVector<InteractionRegion> interactions;
};

JourneyScene buildJourneyScene(const JourneyData& data, JourneyConfig config,
                               JourneySceneStyle style);

}  // namespace muffin::mermaid::journey
