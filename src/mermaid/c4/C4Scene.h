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

// themeCSS overlay for one DOM element. Empty strings keep the primitive's
// own value; visible/hasBox follow the shared csscascade semantics (display
// and visibility gate painting, only the ancestor display chain gates
// geometry — an element whose *own* display is none loses its Chrome bbox
// while ancestor-only hiding keeps it, tracked by `measures`). c4 measures
// every label through the *config* fonts upstream, so font overrides never
// feed back into layout — they only repaint.
struct C4ElementCss {
  QString fill;
  QString stroke;
  QString strokeWidth;
  QString fontFamily;
  qreal fontSize = -1.0;
  QString fontWeight;
  QString fontStyle;
  qreal opacity = -1.0;
  bool visible = true;
  bool hasBox = true;
  bool measures = true;
};

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
  // themeCSS slot stamped at emission time.
  C4ElementCss css;
  // Relation arrowhead/arrowend paint color: the marker paths carry no fill
  // attribute upstream and inherit the svg root fill (textColor), unlike the
  // line stroke they cap.
  QString markerFill;
};

// Slots are indexed by parse order (data.shapes / data.boundaries /
// data.relations); the builder resolves them to primitives at emission time,
// mirroring the upstream draw recursion (global shapes, then per boundary:
// nested shapes, nested boundaries, own group — the synthetic global
// boundary never draws its own rect).
struct C4CssOverrides {
  struct Shape {
    C4ElementCss group;
    C4ElementCss body;
    C4ElementCss detail;
    C4ElementCss stereotype;
    C4ElementCss image;
    C4ElementCss label;
    C4ElementCss technology;
    C4ElementCss description;
  };
  struct Boundary {
    C4ElementCss group;
    C4ElementCss body;
    C4ElementCss label;
    C4ElementCss type;
    C4ElementCss description;
  };
  struct Relation {
    C4ElementCss group;
    C4ElementCss body;
    C4ElementCss label;
    C4ElementCss technology;
  };
  bool active = false;
  QVector<Shape> shapes;
  QVector<Boundary> boundaries;
  QVector<Relation> relations;
  C4ElementCss title;
  // Arrowhead + arrowend + filled-head marker paths share one slot: they all
  // inherit the root fill and no c4 rule can tell them apart.
  C4ElementCss markers;
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
                     C4SceneStyle style,
                     const C4CssOverrides* css = nullptr);

}  // namespace muffin::mermaid::c4
