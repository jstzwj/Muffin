#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/gitgraph/GitGraphDiagram.h"

#include <QPainterPath>
#include <QJsonObject>
#include <QLineF>
#include <QPolygonF>
#include <QRectF>
#include <QVector>

namespace muffin::mermaid::gitgraph {

struct GitGraphConfig {
  bool useMaxWidth = true;
  qreal titleTopMargin = 25.0;
  qreal diagramPadding = 8.0;
  bool showCommitLabel = true;
  bool showBranches = true;
  bool rotateCommitLabel = true;
  bool parallelCommits = false;
};

struct GitGraphSceneStyle {
  QString themeName = QStringLiteral("default");
  QString look = QStringLiteral("classic");
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal fontSize = 16.0;
  QString textColor = QStringLiteral("#333333");
  QString lineColor = QStringLiteral("#333333");
  QString commitLineColor;
  QString nodeBorder = QStringLiteral("#333333");
  QString mainBkg = QStringLiteral("#ffffff");
  QString primaryColor = QStringLiteral("#ECECFF");
  QString commitLabelColor = QStringLiteral("#333333");
  QString commitLabelBackground = QStringLiteral("#ffffde");
  qreal commitLabelFontSize = 10.0;
  QString tagLabelColor = QStringLiteral("#333333");
  QString tagLabelBackground = QStringLiteral("#ECECFF");
  QString tagLabelBorder = QStringLiteral("#9370DB");
  qreal tagLabelFontSize = 10.0;
  qreal strokeWidth = 1.0;
  bool useGradient = false;
  QString gradientStart;
  QString gradientStop;
  QVector<QString> gitColors;
  QVector<QString> gitInvColors;
  QVector<QString> branchLabelColors;
  QVector<QString> borderColors;
};

enum class PrimitiveKind { Line, Path, Circle, Rect, Polygon, Text };

struct GitGraphPrimitive {
  PrimitiveKind kind = PrimitiveKind::Line;
  QString role;
  QString cssClass;
  QRectF rect;
  QLineF line;
  QPointF center;
  qreal radius = 0.0;
  QPainterPath path;
  QString pathData;
  QPolygonF polygon;
  QString text;
  QStringList textLines;
  QPointF position;
  QRectF bounds;
  QPointF translation;
  QString anchor = QStringLiteral("start");
  qreal rotation = 0.0;
  QPointF rotationOrigin;
  qreal fontSize = 16.0;
  bool bold = false;
  QString fill = QStringLiteral("none");
  QString stroke = QStringLiteral("none");
  qreal strokeWidth = 1.0;
  QVector<qreal> dash;
  qreal opacity = 1.0;
  qreal rx = 0.0;
  bool gradientStroke = false;
};

struct GitGraphScene final : MermaidScene {
  QRectF bounds;
  QRectF rasterBounds;
  QRectF contentBounds;
  QString viewBoxAttribute;
  bool useMaxWidth = true;
  GitGraphConfig config;
  GitGraphSceneStyle style;
  QVector<GitGraphPrimitive> primitives;

  QRectF sceneBounds() const override { return bounds; }
  QRectF renderBounds() const override { return rasterBounds; }
  void paint(QPainter& painter,
             const MermaidPaintOptions& options = {}) const override;
  QJsonObject toJsonObject() const override;
};

GitGraphScene buildGitGraphScene(const GitGraphData& data,
                                 GitGraphConfig config,
                                 GitGraphSceneStyle style);

}  // namespace muffin::mermaid::gitgraph
