#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/timeline/TimelineDiagram.h"

#include <QPointF>
#include <QFont>
#include <QJsonValue>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

class QPainter;

namespace muffin::mermaid::timeline {

struct TimelineConfig {
  bool useMaxWidth = true;
  qreal leftMargin = 150.0;
  QJsonValue leftMarginRaw = 150.0;
  qreal padding = 50.0;
  bool invalidPadding = false;
  bool disableMulticolor = false;
};

// Values are already resolved through FlowTheme by the adapter. Raw CSS paint
// spellings are kept because `none`, `currentColor`, invalid colours and the
// neo gradient have property-specific behaviour at the paint boundary.
struct TimelineSceneStyle {
  QString themeName = QStringLiteral("default");
  QString look = QStringLiteral("classic");
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal fontSize = 16.0;
  // Renderer arithmetic reads root config.fontSize, while CSS node text reads
  // themeVariables.fontSize. Redux therefore paints 14px text but still adds
  // the default 16px * .55 vertical allowance.
  qreal layoutFontSize = 16.0;
  QFont::Weight nodeFontWeight = QFont::Normal;
  QString textColor = QStringLiteral("#333");
  QString mainBkg = QStringLiteral("#ECECFF");
  QString nodeBorder = QStringLiteral("#9370DB");
  QString tertiaryColor = QStringLiteral("#ffffde");
  QString clusterBorder = QStringLiteral("#aaaa33");
  qreal strokeWidth = 1.0;
  QString strokeWidthCss = QStringLiteral("1px");
  bool useGradient = true;
  QString gradientStart;
  QString gradientStop;
  int themeColorLimit = 12;
  qreal rawThemeColorLimit = 12.0;
  int themeColorRuleCount = 12;
  QVector<QString> cScale;
  QVector<QString> cScaleInv;
  QVector<QString> cScaleLabel;
  QStringList borderColorArray;
  QStringList bkgColorArray;
};

enum class TimelineNodeKind { Section, Task, Event };

// Resolved themeCSS declarations for one element, shared by the scene builder
// (layout gates) and the painter. Empty strings mean "no CSS opinion" — the
// base theme paint stands. `visible` is the engine's displayed() (display and
// visibility including ancestors); `hasBox` follows the display:none chain
// because getBBox() drops undisplayed geometry while visibility keeps it.
struct TimelineElementCss {
  QString fill;
  QString stroke;
  QString strokeWidth;
  QString color;
  QString fontFamily;
  QString fontWeight;
  qreal fontSize = -1.0;
  qreal opacity = -1.0;
  bool visible = true;
  bool hasBox = true;
};

// themeCSS overlay resolved against a faithful model of the timeline DOM.
struct TimelineCssOverrides {
  bool active = false;
  // The transient classless probe <text> upstream measures node heights with:
  // its font feeds wrap and bbox, so font rules change layout.
  TimelineElementCss measureText;
  TimelineElementCss title;  // classless svg-level <text>
  TimelineElementCss axis;   // the axis lineWrapper <line>
  struct Node {
    TimelineElementCss box;      // path.node-bkg
    TimelineElementCss divider;  // line.node-line-N
    TimelineElementCss text;     // classless <text> in the node group
  };
  QVector<Node> nodes;  // scene.nodes order
  QVector<TimelineElementCss> connectors;  // scene.lines order, non-axis
};

struct TimelineTextLine {
  QString sourceText;
  QString visibleText;
  QPointF baseline;
  QRectF logicalBounds;
};

struct TimelineNodeGeometry {
  TimelineNodeKind kind = TimelineNodeKind::Task;
  QString text;
  QString section;
  int sectionNumber = 0;
  int fullSection = 0;
  int paletteIndex = -1;
  qreal sectionClassValue = -1.0;
  QString sectionClass;
  QPointF position;
  qreal width = 0.0;
  qreal height = 0.0;
  QString pathData;
  QPointF textOffset;
  QVector<TimelineTextLine> textLines;
  QRectF textBounds;
  QString fill;
  QString stroke;
  QString dividerStroke;
  QString textFill;
  qreal strokeWidth = 0.0;
  qreal dividerWidth = 0.0;
  bool rounded = true;
  bool dividerVisible = true;
  bool eventBrightness = false;
  bool gradientStroke = false;
  bool dropShadow = false;
  int paintOrder = 0;
  // themeCSS surface of the three painted node parts.
  TimelineElementCss boxCss;
  TimelineElementCss dividerCss;
  TimelineElementCss textCss;
};

struct TimelineLineGeometry {
  QPointF start;
  QPointF end;
  QString stroke;
  qreal strokeWidth = 2.0;
  QVector<qreal> dashPattern;
  bool markerEnd = true;
  bool markerResolved = true;
  bool axis = false;
  int paintOrder = 0;
  TimelineElementCss css;
};

struct TimelineTitleGeometry {
  bool visible = false;
  QString text;
  QPointF baseline;
  QRectF logicalBounds;
  qreal fontSize = 0.0;
  QString fill;
  int paintOrder = 0;
  TimelineElementCss css;
};

struct TimelineScene : MermaidScene {
  QRectF sceneBounds() const override { return bounds; }
  QRectF renderBounds() const override { return bounds; }
  void paint(QPainter& painter, const MermaidPaintOptions& options) const override;
  QJsonObject toJsonObject() const override;

  QRectF bounds;
  QRectF contentBounds;
  QRectF preTitleBounds;
  TimelineDirection direction = TimelineDirection::LeftToRight;
  QString title;
  QString accTitle;
  QString accDescr;
  TimelineConfig config;
  TimelineSceneStyle style;
  QVector<TimelineNodeGeometry> nodes;
  QVector<TimelineLineGeometry> lines;
  TimelineTitleGeometry titleGeometry;
  QString markerDefinitionId = QStringLiteral("arrowhead");
};

TimelineScene buildTimelineScene(const TimelineData& data, TimelineConfig config,
                                 TimelineSceneStyle style,
                                 const TimelineCssOverrides* css = nullptr);

// The 4ex title presentation attribute resolved against the root font, shared
// by the builder (ink bounds) and the adapter's cascade tree.
qreal timelineTitleFontSizePx(const TimelineSceneStyle& style);

}  // namespace muffin::mermaid::timeline
