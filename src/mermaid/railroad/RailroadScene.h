#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/railroad/RailroadDiagram.h"

#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QVector>

namespace muffin::mermaid::railroad {

struct RailroadConfig {
  bool useMaxWidth = true;
  bool compactMode = false;  // accepted but renderer-inert in Mermaid 11.16
  qreal padding = 10.0;
  qreal verticalSeparation = 8.0;
  qreal horizontalSeparation = 10.0;
  qreal arcRadius = 10.0;
  QString fontFamily = QStringLiteral("monospace");
  qreal fontSize = 14.0;
  QString terminalFill = QStringLiteral("#FFFFC0");
  QString terminalStroke = QStringLiteral("#000000");
  QString terminalTextColor = QStringLiteral("#000000");
  QString nonTerminalFill = QStringLiteral("#FFFFFF");
  QString nonTerminalStroke = QStringLiteral("#000000");
  QString nonTerminalTextColor = QStringLiteral("#000000");
  QString lineColor = QStringLiteral("#000000");
  qreal strokeWidth = 2.0;
  QString markerFill = QStringLiteral("#000000");
  QString commentFill = QStringLiteral("#E8E8E8");
  QString commentStroke = QStringLiteral("#888888");
  QString commentTextColor = QStringLiteral("#666666");
  QString specialFill = QStringLiteral("#F0E0FF");
  QString specialStroke = QStringLiteral("#8800CC");
  QString ruleNameColor = QStringLiteral("#000066");
  bool showMarkers = true;  // accepted but renderer-inert in Mermaid 11.16
  qreal markerRadius = 5.0;
};

enum class RailroadPrimitiveKind { Rect, Circle, Path, Text };
enum class RailroadTextBaseline { Auto, Middle };

struct RailroadPrimitive {
  RailroadPrimitiveKind kind = RailroadPrimitiveKind::Path;
  QString cssClass;
  QPointF translation;
  QRectF rect;
  QPainterPath path;
  QString pathData;
  QString text;
  QPointF position;
  QString fill = QStringLiteral("none");
  QString stroke = QStringLiteral("none");
  qreal strokeWidth = 1.0;
  QVector<qreal> dash;
  qreal rx = 0.0;
  qreal ry = 0.0;
  bool middleAnchor = false;
  RailroadTextBaseline baseline = RailroadTextBaseline::Auto;
  bool bold = false;
  bool italic = false;
  int paintOrder = -1;
};

struct RailroadRuleGeometry {
  QString name;
  qreal y = 0.0;
  qreal width = 0.0;
  qreal height = 0.0;
  qreal baselineY = 0.0;
  qreal definitionX = 0.0;
};

struct RailroadScene final : MermaidScene {
  QRectF bounds;
  QRectF rasterBounds;
  RailroadConfig config;
  RailroadDialect dialect = RailroadDialect::Direct;
  QVector<RailroadPrimitive> primitives;
  QVector<RailroadRuleGeometry> rules;
  QString title;
  QString accTitle;
  QString accDescr;

  QRectF sceneBounds() const override { return bounds; }
  QRectF renderBounds() const override { return rasterBounds; }
  void paint(QPainter& painter,
             const MermaidPaintOptions& options = {}) const override;
  QJsonObject toJsonObject() const override;
};

RailroadScene buildRailroadScene(const RailroadData& data,
                                 RailroadConfig config);

}  // namespace muffin::mermaid::railroad
