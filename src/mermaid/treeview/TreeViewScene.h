#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/treeview/TreeViewDiagram.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QFont>
#include <QHash>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace muffin::mermaid::treeview {

struct TreeViewConfig {
  QJsonValue useMaxWidth = true;
  QJsonValue rowIndent = 10.0;
  QJsonValue paddingX = 5.0;
  QJsonValue paddingY = 5.0;
  QJsonValue lineThickness = 1.0;
  QJsonValue showIcons = false;
  QString defaultIconPack;
  QJsonObject filenameIcons;
  QJsonObject extensionIcons;
};

struct TreeViewResolvedTextStyle {
  QString fontFamily;
  qreal fontSize = 16.0;
  QFont::Weight fontWeight = QFont::Normal;
  QFont::Style fontStyle = QFont::StyleNormal;
  QString fill;
  qreal opacity = 1.0;
  bool visible = true;
  bool hasBox = true;
};

struct TreeViewResolvedShapeStyle {
  QString fill;
  QString stroke;
  qreal strokeWidth = 1.0;
  qreal fillOpacity = 1.0;
  qreal strokeOpacity = 1.0;
  bool visible = true;
};

struct TreeViewSceneStyle {
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal rootFontSize = 16.0;
  QString rootTextColor = QStringLiteral("#333");
  QString labelFontSize = QStringLiteral("16px");
  QString labelColor = QStringLiteral("black");
  QString lineColor = QStringLiteral("black");
  QString iconColor = QStringLiteral("#546e7a");
  QString descriptionColor = QStringLiteral("#6a9955");
  QString highlightBg = QStringLiteral("rgba(255, 193, 7, 0.15)");
  QString highlightStroke = QStringLiteral("#ffc107");
  QHash<int, TreeViewResolvedTextStyle> labelStyles;
  QHash<int, TreeViewResolvedTextStyle> descriptionStyles;
  QHash<int, TreeViewResolvedShapeStyle> highlightStyles;
  QVector<TreeViewResolvedShapeStyle> lineStyles;
};

struct TreeViewTextGeometry {
  QString cssClass;
  QString text;
  QPointF position;
  QRectF inkBounds;
  qreal layoutWidth = 0.0;
  qreal fontSize = 16.0;
  QString fontFamily;
  QFont::Weight fontWeight = QFont::Normal;
  QFont::Style fontStyle = QFont::StyleNormal;
  bool bold = false;
  bool italic = false;
  QString fill;
  qreal opacity = 1.0;
  bool visible = true;
  bool hasBox = true;
};

struct TreeViewLineGeometry {
  QPointF start;
  QPointF end;
  QString x1Attribute;
  QString y1Attribute;
  QString x2Attribute;
  QString y2Attribute;
  QString stroke;
  QString strokeWidthAttribute;
  qreal strokeWidth = 1.0;
  qreal opacity = 1.0;
  bool visible = true;
  int paintOrder = -1;
};

struct TreeViewNodeGeometry {
  int id = 0;
  int depth = 0;
  QRectF bbox;
  QString xAttribute;
  bool iconReserved = false;
  QString iconName;
  TreeViewTextGeometry label;
  bool hasDescription = false;
  TreeViewTextGeometry description;
  bool highlighted = false;
  QRectF highlightRect;
  QString highlightFill;
  QString highlightStroke;
  qreal highlightStrokeWidth = 1.0;
  qreal highlightFillOpacity = 1.0;
  qreal highlightStrokeOpacity = 1.0;
  bool highlightVisible = true;
  int groupPaintOrder = -1;
};

struct TreeViewScene final : MermaidScene {
  QRectF sceneBounds() const override { return bounds; }
  QRectF renderBounds() const override {
    return QRectF(bounds.topLeft(),
                  QSizeF(qRound(bounds.width()), qRound(bounds.height())));
  }
  void paint(QPainter& painter,
             const MermaidPaintOptions& options) const override;
  QJsonObject toJsonObject() const override;

  QRectF bounds;
  qreal totalWidth = 0.0;
  qreal totalHeight = 0.0;
  bool useMaxWidth = true;
  TreeViewConfig config;
  TreeViewSceneStyle style;
  QVector<TreeViewNodeGeometry> nodes;
  QVector<TreeViewLineGeometry> lines;
  QStringList iconDefs;
};

TreeViewScene buildTreeViewScene(const TreeViewData& data,
                                 TreeViewConfig config,
                                 TreeViewSceneStyle style);

}  // namespace muffin::mermaid::treeview
