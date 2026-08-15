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

// themeCSS overlay for one cynefin DOM element. `measures` carries the
// own-display flag for the item badge: the item text already wears its class
// at getBBox time, so a themeCSS font resizes the badge, while own
// display:none collapses the Chrome bbox to 0x0 and falls back to
// label.length * 7. The viewBox stays config-driven (no bbox feedback).
struct CynefinElementCss {
  QString fill;
  QString stroke;
  QString strokeWidth;
  QString fontFamily;
  qreal fontSize = -1.0;
  QString fontWeight;
  QString fontStyle;
  qreal opacity = -1.0;
  qreal fillOpacity = -1.0;
  qreal strokeOpacity = -1.0;
  bool visible = true;
  bool measures = true;
};

// Slots follow the emission order (quadrant domains, then confusion; each
// domain's visible items plus the overflow badge; then transitions).
struct CynefinCssOverrides {
  struct Item {
    CynefinElementCss group;
    CynefinElementCss rect;
    CynefinElementCss text;
  };
  struct Arrow {
    CynefinElementCss group;
    CynefinElementCss line;
    CynefinElementCss label;
  };
  bool active = false;
  QVector<CynefinElementCss> backgrounds;
  QVector<CynefinElementCss> boundaries;
  CynefinElementCss confusion;
  QVector<CynefinElementCss> labels;
  QVector<CynefinElementCss> subtitles;
  QVector<Item> items;
  QVector<Arrow> arrows;
  CynefinElementCss arrowHead;
  CynefinElementCss title;
};

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
  CynefinElementCss css;
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
  CynefinElementCss css;
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
  CynefinElementCss css;
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
  CynefinElementCss css;
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
  CynefinElementCss arrowHeadCss;

  QRectF sceneBounds() const override { return bounds; }
  void paint(QPainter &painter,
             const MermaidPaintOptions &options = {}) const override;
  QJsonObject toJsonObject() const override;
};

CynefinScene buildCynefinScene(const CynefinData &data, CynefinConfig config,
                               CynefinSceneStyle style,
                               const CynefinCssOverrides *css = nullptr);

} // namespace muffin::mermaid::cynefin
