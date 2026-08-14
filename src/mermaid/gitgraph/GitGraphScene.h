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

// Resolved themeCSS declarations for one element. Empty strings mean "no CSS
// opinion" — the base theme paint stands. `visible` is displayed() including
// ancestors; `hasBox` follows the display:none chain because the final
// setupGraphViewbox getBBox drops undisplayed geometry; `measures` follows
// only the element's OWN display — Chrome's getBBox keeps geometry under a
// display:none ancestor but returns an empty rect for the hidden element
// itself (gitgraph-hidden locks both behaviors).
struct GitGraphElementCss {
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
  bool measures = true;
};

// themeCSS overlay resolved against the upstream gitGraph DOM. Geometry slots
// follow the builder's primitive emission order (branch groups in
// orderedBranches order, arrows per parent edge, then commits in draw order).
struct GitGraphCssOverrides {
  bool active = false;
  // The branch-spacing probe measures through a transient classless
  // `g.label.branch-label` wrapper (no numeric suffix), so `.branch-labelN`
  // font rules move only the drawn label — the measure/draw split.
  GitGraphElementCss branchProbe;
  struct Branch {
    GitGraphElementCss line;   // line.branch.branchN
    GitGraphElementCss bkg;    // rect.branchLabelBkg.labelN
    GitGraphElementCss group;  // g.label.branch-labelN
    GitGraphElementCss text;   // the label <text>
  };
  struct CommitLabel {
    GitGraphElementCss wrapper;  // the classless label <g>
    GitGraphElementCss bkg;      // rect.commit-label-bkg
    GitGraphElementCss text;     // text.commit-label
  };
  struct Tag {
    GitGraphElementCss bkg;   // polygon.tag-label-bkg
    GitGraphElementCss hole;  // circle.tag-hole
    GitGraphElementCss text;  // text.tag-label
  };
  QVector<Branch> branches;
  QVector<GitGraphElementCss> arrows;
  QVector<QVector<GitGraphElementCss>> bullets;  // per drawn commit, DOM order
  QVector<CommitLabel> labels;                    // emitted labels, draw order
  QVector<Tag> tags;                              // flat, emission order
  GitGraphElementCss title;
};

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
  GitGraphElementCss css;
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
                                 GitGraphSceneStyle style,
                                 const GitGraphCssOverrides* css = nullptr);

// The arrow path class digit depends on layout (reroute lanes and merge-parent
// flips pick the other branch's color), so the adapter's DOM model needs the
// resolved digits before the scene builds. Computed with the default branch
// label metrics — identical to the builder whenever the themeCSS leaves the
// measurement probe alone.
QVector<int> gitGraphArrowClassDigits(const GitGraphData& data,
                                      const GitGraphConfig& config,
                                      const GitGraphSceneStyle& style);

}  // namespace muffin::mermaid::gitgraph
