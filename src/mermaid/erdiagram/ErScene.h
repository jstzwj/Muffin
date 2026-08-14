#pragma once

#include "mermaid/MermaidScene.h"
#include "mermaid/erdiagram/ErLayout.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/theme/MermaidStyleResolve.h"

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>

#include <optional>

namespace muffin::mermaid::er {

// Default palette matches the mermaid "default" theme erDiagram styles
// (entityFill #ECECFF, entityStroke #9370DB). See styles.d.ts / styles.js.
struct ErSceneStyle {
  QString entityFill = QStringLiteral("#ECECFF");
  QString entityStroke = QStringLiteral("#9370DB");
  QString entityTitle1 = QStringLiteral("#131300");
  QString attributeColor = QStringLiteral("#131300");
  QString relationshipColor = QStringLiteral("#333333");
  QString relationshipLabelColor = QStringLiteral("#333333");
  QString labelBackground = QStringLiteral("#ECECFF");
  QString fontFamily = QStringLiteral("Noto Sans");
  qreal fontSize = 16.0;
  qreal lineHeight = 24.0;
  qreal strokeWidth = 1.0;
  qreal relationshipStrokeWidth = 1.0;
};

struct ErSceneAttribute {
  QString text;
  QString keyType;  // "" / "PK" / "FK" / "UK"
  QString comment;
};

// `bounds` is the full table rect (header + rows). `headerRect` is the name
// band; `attributeRects[i]` is the row rect for `attributes[i]`. The flowchart
// label documents are pre-shaped so the painter reuses cached glyph runs
// (paintFlowLabel) instead of re-measuring.
struct ErSceneEntity {
  QString id;
  QString name;
  QRectF bounds;
  QRectF headerRect;
  QVector<QRectF> attributeRects;
  QVector<ErSceneAttribute> attributes;
  flowchart::FlowLabelDocument nameDocument;
  QVector<flowchart::FlowLabelDocument> attributeDocuments;
  // Resolved entity paint (classDef / inline style via MermaidStyleResolve);
  // empty when no classDef/style applies — the painter falls back to scene.style.
  QString fill;
  QString stroke;
};

// `pathBounds` is the bounding rect of the raw edge geometry (for culling);
// `labelBounds` additionally includes the label box. `cardA`/`cardB` select the
// crow's-foot marker drawn at the entityA / entityB endpoint respectively.
struct ErSceneRelationship {
  QString id;
  QString path;
  QVector<QPointF> points;
  QVector<QVector<QPointF>> segments;
  ErCardinality cardA = ErCardinality::ExactlyOne;
  ErCardinality cardB = ErCardinality::ExactlyOne;
  bool identifying = true;
  QString roleA;
  QString roleB;
  QString label;
  std::optional<QPointF> labelPosition;
  flowchart::FlowLabelDocument labelDocument;
  QSizeF labelSize;
  QRectF pathBounds;
  QRectF labelBounds;
};

struct ErScene : MermaidScene {
  QRectF sceneBounds() const override { return bounds; }
  void paint(QPainter& painter, const MermaidPaintOptions& options) const override;
  QJsonObject toJsonObject() const override;
  SvgMarkerProjection svgMarkerProjection() const override;

  QRectF bounds;
  QVector<ErSceneEntity> entities;
  QVector<ErSceneRelationship> relationships;
  ErSceneStyle style;
  QString role = QStringLiteral("graphics-document document");
  QString ariaRoleDescription = QStringLiteral("erDiagram");
};

// Builds the immutable scene from the layout input (source of attribute text)
// and the dagre placement (positions / edge paths). Entity rects are derived
// from the measured sizes recorded during layout; attribute row rects are
// computed from the style line height. The result is safe to copy and reuse
// across paint passes.
ErScene buildErScene(const ErLayoutInput& input, const ErPlacementResult& placement,
                     ErSceneStyle style = {},
                     const QVector<style::ClassDef>& classDefs = {},
                     const style::ThemeDefaults& themeDefaults = {});

}  // namespace muffin::mermaid::er
