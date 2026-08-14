#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/xychart/XYChartDiagram.h"

#include <QFont>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

class QPainter;

namespace muffin::mermaid::xychart {

struct XYChartAxisConfig {
  bool showLabel = true;
  qreal labelFontSize = 14.0;
  qreal labelPadding = 5.0;
  bool showTitle = true;
  qreal titleFontSize = 16.0;
  qreal titlePadding = 5.0;
  bool showTick = true;
  qreal tickLength = 5.0;
  qreal tickWidth = 2.0;
  bool showAxisLine = true;
  qreal axisLineWidth = 2.0;
  qreal labelRotation = 0.0;
};

struct XYChartConfig {
  qreal width = 700.0;
  qreal height = 500.0;
  qreal titleFontSize = 20.0;
  qreal titlePadding = 10.0;
  bool showTitle = true;
  bool showDataLabel = false;
  bool showDataLabelOutsideBar = false;
  XYChartAxisConfig xAxis;
  XYChartAxisConfig yAxis;
  XYChartOrientation orientation = XYChartOrientation::Vertical;
  qreal plotReservedSpacePercent = 50.0;
};

struct XYChartSceneStyle {
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal rootFontSize = 16.0;
  QString backgroundColor = QStringLiteral("white");
  QString titleColor = QStringLiteral("#333");
  QString dataLabelColor = QStringLiteral("#333");
  QString xAxisTitleColor = QStringLiteral("#333");
  QString xAxisLabelColor = QStringLiteral("#333");
  QString xAxisTickColor = QStringLiteral("#333");
  QString xAxisLineColor = QStringLiteral("#333");
  QString yAxisTitleColor = QStringLiteral("#333");
  QString yAxisLabelColor = QStringLiteral("#333");
  QString yAxisTickColor = QStringLiteral("#333");
  QString yAxisLineColor = QStringLiteral("#333");
  QStringList plotColorPalette;
  qreal backgroundOpacity = 1.0;
  bool backgroundVisible = true;
};

enum class XYChartTextAnchor { Start, Middle, End };
enum class XYChartBaseline { Auto, BeforeEdge, Hanging, Middle };

struct XYChartTextGeometry {
  QString group;
  QString text;
  QPointF position;
  QString fill;
  qreal fontSize = 0.0;
  qreal rotation = 0.0;
  XYChartTextAnchor anchor = XYChartTextAnchor::Middle;
  XYChartBaseline baseline = XYChartBaseline::Middle;
  QString fontFamily;
  QFont::Weight fontWeight = QFont::Normal;
  qreal opacity = 1.0;
  bool visible = true;
  int paintOrder = -1;
};

struct XYChartPathGeometry {
  QString group;
  QString path;
  QVector<QPointF> points;
  QString fill;
  QString stroke;
  qreal strokeWidth = 0.0;
  qreal fillOpacity = 1.0;
  qreal strokeOpacity = 1.0;
  bool visible = true;
  int paintOrder = -1;
};

struct XYChartRectGeometry {
  QString group;
  QRectF rect;
  QString fill;
  QString stroke;
  qreal strokeWidth = 0.0;
  qreal fillOpacity = 1.0;
  qreal strokeOpacity = 1.0;
  bool visible = true;
  int paintOrder = -1;
};

struct XYChartScene : MermaidScene {
  QRectF sceneBounds() const override { return bounds; }
  void paint(QPainter& painter, const MermaidPaintOptions& options) const override;
  QJsonObject toJsonObject() const override;

  QRectF bounds;
  QRectF plotBounds;
  XYChartConfig config;
  XYChartSceneStyle style;
  QString title;
  QString accTitle;
  QString accDescr;
  QVector<XYChartTextGeometry> texts;
  QVector<XYChartPathGeometry> paths;
  QVector<XYChartRectGeometry> rects;
  int nextPaintOrder = 0;
};

XYChartScene buildXYChartScene(const XYChartData& data, XYChartConfig config,
                               XYChartSceneStyle style);

}  // namespace muffin::mermaid::xychart
