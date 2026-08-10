#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/gantt/GanttDiagram.h"

#include <QLineF>
#include <QRectF>
#include <QString>
#include <QVector>

class QPainter;

namespace muffin::mermaid::gantt {

struct GanttConfig {
  bool useMaxWidth = true;
  qreal useWidth = 1200.0;
  qreal titleTopMargin = 25.0;
  qreal barHeight = 20.0;
  qreal barGap = 4.0;
  qreal topPadding = 50.0;
  qreal rightPadding = 75.0;
  qreal leftPadding = 75.0;
  qreal gridLineStartPadding = 35.0;
  qreal fontSize = 11.0;
  qreal sectionFontSize = 11.0;
  int numberSectionStyles = 4;
  QString axisFormat = QStringLiteral("%Y-%m-%d");
  QString tickInterval;
  bool topAxis = false;
  QString displayMode;
  QString weekday = QStringLiteral("sunday");
};

struct GanttSceneStyle {
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal rootFontSize = 16.0;
  QString textColor = QStringLiteral("#333");
  QString titleColor = QStringLiteral("#333");
  QString sectionBkgColor;
  QString altSectionBkgColor;
  QString sectionBkgColor2;
  QString excludeBkgColor;
  QString taskBorderColor;
  QString taskBkgColor;
  QString taskTextColor;
  QString taskTextDarkColor;
  QString taskTextOutsideColor;
  QString taskTextClickableColor;
  QString activeTaskBorderColor;
  QString activeTaskBkgColor;
  QString gridColor;
  QString doneTaskBkgColor;
  QString doneTaskBorderColor;
  QString critBorderColor;
  QString critBkgColor;
  QString todayLineColor;
  QString vertLineColor = QStringLiteral("navy");
};

enum class GanttTextAnchor { Start, Middle, End };

struct GanttRectGeometry {
  QString id;
  QString cssClass;
  QRectF rect;
  QString fill;
  QString stroke;
  qreal strokeWidth = 0.0;
  qreal opacity = 1.0;
  qreal radius = 0.0;
  bool milestone = false;
  QPointF transformOrigin;
};

struct GanttLineGeometry {
  QString id;
  QString cssClass;
  QLineF line;
  QString stroke;
  qreal strokeWidth = 1.0;
  qreal opacity = 1.0;
};

struct GanttTextGeometry {
  QString id;
  QString cssClass;
  QString text;
  QStringList lines;
  QPointF position;
  qreal fontSize = 11.0;
  QString fill;
  GanttTextAnchor anchor = GanttTextAnchor::Start;
  bool italic = false;
  bool bold = false;
  qreal lineStep = 0.0;
};

struct GanttScene : MermaidScene {
  QRectF sceneBounds() const override { return bounds; }
  QRectF renderBounds() const override { return bounds; }
  void paint(QPainter& painter, const MermaidPaintOptions& options) const override;
  QJsonObject toJsonObject() const override;
  const QVector<InteractionRegion>& interactionRegions() const override {
    return interactions;
  }

  QRectF bounds;
  QString title;
  QString accTitle;
  QString accDescr;
  GanttConfig config;
  GanttSceneStyle style;
  QVector<GanttRectGeometry> excludes;
  QVector<GanttLineGeometry> gridLines;
  QVector<GanttTextGeometry> gridLabels;
  QVector<GanttRectGeometry> sections;
  QVector<GanttRectGeometry> tasks;
  QVector<GanttTextGeometry> taskLabels;
  QVector<GanttTextGeometry> sectionLabels;
  QVector<GanttLineGeometry> todayLines;
  GanttTextGeometry titleGeometry;
  QVector<InteractionRegion> interactions;
};

GanttScene buildGanttScene(const GanttData& data, GanttConfig config,
                           GanttSceneStyle style);

}  // namespace muffin::mermaid::gantt
