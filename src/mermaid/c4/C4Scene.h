#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/c4/C4Diagram.h"

#include <QHash>
#include <QJsonObject>
#include <QLineF>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QVector>

namespace muffin::mermaid::c4 {

struct C4Font {
  QString family = QStringLiteral("Noto Sans");
  qreal size = 14.0;
  QString weight = QStringLiteral("normal");
};

struct C4Config {
  bool useMaxWidth = true;
  qreal diagramMarginX = 50.0;
  qreal diagramMarginY = 10.0;
  qreal c4ShapeMargin = 50.0;
  qreal c4ShapePadding = 20.0;
  qreal width = 216.0;
  qreal height = 60.0;
  qreal boxMargin = 10.0;
  int c4ShapeInRow = 4;
  qreal nextLinePaddingX = 0.0;
  int c4BoundaryInRow = 2;
  bool wrap = true;
  qreal wrapPadding = 10.0;
  QHash<QString, C4Font> fonts;
  QHash<QString, QString> backgroundColors;
  QHash<QString, QString> borderColors;
};

struct C4SceneStyle {
  QString rootFontFamily = QStringLiteral("Noto Sans");
  qreal rootFontSize = 16.0;
  QString rootFontWeight = QStringLiteral("normal");
  QString rootTextColor = QStringLiteral("#333333");
};

enum class C4PrimitiveKind { Rect, Path, Line, Text, Image };

struct C4Primitive {
  C4PrimitiveKind kind = C4PrimitiveKind::Rect;
  QString role;
  QString alias;
  QRectF rect;
  QLineF line;
  QPainterPath path;
  QString pathData;
  QString text;
  QPointF position;
  QRectF bounds;
  QString fontFamily;
  qreal fontSize = 16.0;
  QString fontWeight = QStringLiteral("normal");
  bool italic = false;
  bool middleAnchor = false;
  bool mathematicalBaseline = false;
  qreal textDy = 0.0;
  qreal forcedTextWidth = 0.0;
  QString fill = QStringLiteral("none");
  QString stroke = QStringLiteral("none");
  qreal strokeWidth = 1.0;
  QVector<qreal> dash;
  qreal rx = 0.0;
  bool markerStart = false;
  bool markerEnd = false;
  QString imageKind;
};

struct C4Scene final : MermaidScene {
  QRectF bounds;
  QRectF contentBounds;
  QString viewBoxAttribute;
  bool useMaxWidth = true;
  C4Config config;
  C4SceneStyle style;
  QVector<C4Primitive> primitives;

  QRectF sceneBounds() const override { return bounds; }
  void paint(QPainter& painter,
             const MermaidPaintOptions& options = {}) const override;
  QJsonObject toJsonObject() const override;
};

C4Scene buildC4Scene(const C4Data& data, C4Config config,
                     C4SceneStyle style);

}  // namespace muffin::mermaid::c4
