#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/wardley/WardleyDiagram.h"

#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QVector>

namespace muffin::mermaid::wardley {

struct WardleyConfig {
  qreal width = 900.0;
  qreal height = 600.0;
  qreal padding = 48.0;
  qreal nodeRadius = 6.0;
  qreal nodeLabelOffset = 8.0;
  qreal axisFontSize = 12.0;
  qreal labelFontSize = 10.0;
  bool showGrid = false;
  bool useMaxWidth = true;
  QString svgId = QStringLiteral("wardley-native");
};

struct WardleySceneStyle {
  QString fontFamily = QStringLiteral("Times New Roman");
  QString backgroundColor = QStringLiteral("#fff");
  QString axisColor = QStringLiteral("#000");
  QString axisTextColor = QStringLiteral("#222");
  QString gridColor = QStringLiteral("rgba(100,100,100,0.2)");
  QString componentFill = QStringLiteral("#fff");
  QString componentStroke = QStringLiteral("#000");
  QString componentLabelColor = QStringLiteral("#222");
  QString linkStroke = QStringLiteral("#000");
  QString evolutionStroke = QStringLiteral("#dc3545");
  QString annotationStroke = QStringLiteral("#000");
  QString annotationTextColor = QStringLiteral("#222");
  QString annotationFill = QStringLiteral("#fff");
};

enum class WardleyPrimitiveType { Rect, Line, Circle, Path, Text };
enum class WardleyTextBaseline { Auto, Middle, Central };

struct WardleyPrimitive {
  WardleyPrimitiveType type = WardleyPrimitiveType::Line;
  QString role;
  QString parentClass;
  QRectF rect;
  QLineF line;
  QPointF center;
  qreal radius = 0.0;
  qreal rx = 0.0;
  QPainterPath path;
  QString pathData;
  QString text;
  QPointF position;
  QRectF bounds;
  qreal rotation = 0.0;
  QString anchor = QStringLiteral("start");
  WardleyTextBaseline baseline = WardleyTextBaseline::Auto;
  qreal fontSize = 10.0;
  bool bold = false;
  QString fill = QStringLiteral("none");
  QString stroke = QStringLiteral("none");
  qreal strokeWidth = 0.0;
  QVector<qreal> dash;
  qreal opacity = 1.0;
  bool markerStart = false;
  bool markerEnd = false;
  QString markerColor;
  qreal markerSize = 5.0;
};

struct WardleyScene final : MermaidScene {
  QRectF bounds;
  QRectF contentBounds;
  QString viewBoxAttribute;
  bool useMaxWidth = true;
  WardleyConfig config;
  WardleySceneStyle style;
  QVector<WardleyPrimitive> primitives;

  QRectF sceneBounds() const override { return bounds; }
  void paint(QPainter &painter,
             const MermaidPaintOptions &options = {}) const override;
  QJsonObject toJsonObject() const override;
};

WardleyScene buildWardleyScene(const WardleyData &data, WardleyConfig config,
                               WardleySceneStyle style);

} // namespace muffin::mermaid::wardley
