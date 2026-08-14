#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/radar/RadarDiagram.h"

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QFont>

#include <optional>

class QPainter;

namespace muffin::mermaid::radar {

struct RadarConfig {
  bool useMaxWidth = true;
  qreal width = 600.0;
  qreal height = 600.0;
  qreal marginTop = 50.0;
  qreal marginRight = 50.0;
  qreal marginBottom = 50.0;
  qreal marginLeft = 50.0;
  // The adapter supplies these when source config values exercise JavaScript
  // `+` concatenation (for example width:"320"). Pure numeric callers may
  // leave them unset and use the arithmetic fallback in buildRadarScene().
  std::optional<qreal> totalWidth;
  std::optional<qreal> totalHeight;
  std::optional<qreal> centerX;
  std::optional<qreal> centerY;
  std::optional<qreal> legendX;
  std::optional<qreal> legendY;
  qreal axisScaleFactor = 1.0;
  qreal axisLabelFactor = 1.05;
  qreal curveTension = 0.17;
};

struct RadarSceneStyle {
  QString fontFamily = QStringLiteral("Noto Sans");
  QString textColor = QStringLiteral("#333333");
  QString titleColor = QStringLiteral("#333333");
  qreal titleFontSize = 16.0;
  QString axisColor = QStringLiteral("#333333");
  QString axisLabelColor = QStringLiteral("#333333");
  qreal axisStrokeWidth = 2.0;
  qreal axisLabelFontSize = 12.0;
  qreal curveOpacity = 0.5;
  qreal curveStrokeWidth = 2.0;
  QString graticuleColor = QStringLiteral("#DEDEDE");
  qreal graticuleStrokeWidth = 1.0;
  qreal graticuleOpacity = 0.3;
  qreal legendFontSize = 12.0;
  QVector<QString> palette;
  int themeColorLimit = 12;
};

enum class RadarTextAnchor { Start, Middle, End };
enum class RadarBaseline { Auto, Central, Hanging };

struct RadarGraticuleGeometry {
  bool circle = true;
  qreal radius = 0.0;
  QVector<QPointF> points;
  QString fill;
  QString stroke;
  QString color = QStringLiteral("black");
  qreal strokeWidth = 1.0;
  qreal fillOpacity = 0.3;
  qreal strokeOpacity = 1.0;
  bool visible = true;
};

struct RadarAxisGeometry {
  QString name;
  QString label;
  QPointF end;
  QPointF labelPosition;
  RadarTextAnchor textAnchor = RadarTextAnchor::Middle;
  RadarBaseline baseline = RadarBaseline::Central;
  QString lineStroke;
  QString lineColor = QStringLiteral("black");
  qreal lineStrokeWidth = 2.0;
  qreal lineOpacity = 1.0;
  bool lineVisible = true;
  QString labelFill;
  QString labelColor = QStringLiteral("black");
  QString labelFontFamily;
  qreal labelFontSize = 12.0;
  QFont::Weight labelFontWeight = QFont::Normal;
  qreal labelOpacity = 1.0;
  bool labelVisible = true;
};

struct RadarCubicSegment {
  QPointF control1;
  QPointF control2;
  QPointF end;
};

struct RadarCurveGeometry {
  QString name;
  QString label;
  int colorIndex = 0;
  bool classGenerated = false;
  QString color;
  bool polygon = false;
  QVector<QPointF> points;
  QVector<RadarCubicSegment> cubics;
  QString fill;
  QString stroke;
  QString elementColor;
  qreal strokeWidth = 2.0;
  qreal fillOpacity = 0.5;
  qreal strokeOpacity = 1.0;
  bool visible = true;
};

struct RadarLegendGeometry {
  QString text;
  int colorIndex = 0;
  bool classGenerated = false;
  QString color;
  QPointF position;
  QString boxFill;
  QString boxStroke;
  QString boxColor;
  qreal boxStrokeWidth = 1.0;
  qreal boxFillOpacity = 0.5;
  qreal boxStrokeOpacity = 1.0;
  bool boxVisible = true;
  QString textFill;
  QString textColor;
  QString textFontFamily;
  qreal textFontSize = 12.0;
  QFont::Weight textFontWeight = QFont::Normal;
  qreal textOpacity = 1.0;
  bool textVisible = true;
};

struct RadarScene : MermaidScene {
  QRectF sceneBounds() const override { return bounds; }
  void paint(QPainter& painter, const MermaidPaintOptions& options) const override;
  QJsonObject toJsonObject() const override;

  QRectF bounds;
  QPointF center;
  qreal radius = 0.0;
  QString title;
  QString accTitle;
  QString accDescr;
  RadarConfig config;
  RadarSceneStyle style;
  RadarOptions options;
  QVector<RadarGraticuleGeometry> graticules;
  QVector<RadarAxisGeometry> axes;
  QVector<RadarCurveGeometry> curves;
  QVector<RadarLegendGeometry> legends;
  QString titleFill;
  QString titleColor;
  QString titleFontFamily;
  qreal titleFontSize = 16.0;
  QFont::Weight titleFontWeight = QFont::Normal;
  qreal titleOpacity = 1.0;
  bool titleVisible = true;
};

RadarScene buildRadarScene(const RadarData& data, RadarConfig config,
                           RadarSceneStyle style);

}  // namespace muffin::mermaid::radar
