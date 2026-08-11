#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/cynefin/CynefinDiagram.h"

#include <QJsonValue>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QVector>

namespace muffin::mermaid::cynefin {

struct CynefinConfig {
  QJsonValue useMaxWidth = true;
  QJsonValue width = 800.0;
  QJsonValue height = 600.0;
  QJsonValue padding = 40.0;
  QJsonValue showDomainDescriptions = true;
  QJsonValue boundaryAmplitude = 8.0;
  QJsonValue seed = 0.0;
  QString svgId = QStringLiteral("cynefin-native");
};

struct CynefinSceneStyle {
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal rootFontSize = 16.0;
  QJsonValue domainFontSize = 16.0;
  QJsonValue itemFontSize = 12.0;
  QString boundaryColor = QStringLiteral("#333333");
  QString boundaryWidth = QStringLiteral("2");
  QString cliffColor = QStringLiteral("#8B0000");
  QString cliffWidth = QStringLiteral("4");
  QString arrowColor = QStringLiteral("#333333");
  QString arrowWidth = QStringLiteral("2");
  QString complexBg = QStringLiteral("#E8F5E9");
  QString complicatedBg = QStringLiteral("#E3F2FD");
  QString chaoticBg = QStringLiteral("#FBE9E7");
  QString clearBg = QStringLiteral("#FFF8E1");
  QString confusionBg = QStringLiteral("#F3E5F5");
  QString textColor = QStringLiteral("#333333");
  QString labelColor = QStringLiteral("#131300");
};

enum class CynefinTextBaseline { Auto, Middle, Central };

struct CynefinTextGeometry {
  QString role;
  QString text;
  QPointF position;
  QRectF bounds;
  qreal fontSize = 16.0;
  bool bold = false;
  bool italic = false;
  QString anchor = QStringLiteral("middle");
  CynefinTextBaseline baseline = CynefinTextBaseline::Middle;
  QString fill;
};

struct CynefinRectGeometry {
  QString role;
  QString domain;
  QRectF rect;
  qreal radius = 0.0;
  QString fill;
  qreal fillOpacity = 1.0;
  QString stroke;
  qreal strokeWidth = 0.0;
  QVector<qreal> dash;
};

struct CynefinPathGeometry {
  QString role;
  QString domain;
  QString pathData;
  QPainterPath path;
  QString fill;
  qreal fillOpacity = 1.0;
  QString stroke;
  qreal strokeWidth = 1.0;
  QVector<qreal> dash;
};

struct CynefinItemGeometry {
  QString domain;
  QPointF translation;
  CynefinRectGeometry rect;
  CynefinTextGeometry text;
  bool overflow = false;
};

struct CynefinArrowGeometry {
  QString from;
  QString to;
  QPointF start;
  QPointF control;
  QPointF end;
  QString pathData;
  QPainterPath path;
  QString stroke;
  qreal strokeWidth = 2.0;
  CynefinTextGeometry label;
};

struct CynefinScene final : MermaidScene {
  QRectF bounds;
  QRectF contentBounds;
  QString viewBoxAttribute;
  QString rootTransformAttribute;
  QPointF rootTranslation;
  bool useMaxWidth = true;
  qreal configuredWidth = 800.0;
  qreal configuredHeight = 600.0;
  qreal configuredPadding = 40.0;
  CynefinSceneStyle style;
  QVector<CynefinRectGeometry> backgrounds;
  QVector<CynefinPathGeometry> boundaries;
  CynefinPathGeometry confusion;
  QVector<CynefinTextGeometry> labels;
  QVector<CynefinTextGeometry> subtitles;
  QVector<CynefinItemGeometry> items;
  QVector<CynefinArrowGeometry> arrows;
  CynefinTextGeometry title;

  QRectF sceneBounds() const override { return bounds; }
  void paint(QPainter &painter,
             const MermaidPaintOptions &options = {}) const override;
  QJsonObject toJsonObject() const override;
};

CynefinScene buildCynefinScene(const CynefinData &data, CynefinConfig config,
                               CynefinSceneStyle style);

} // namespace muffin::mermaid::cynefin
