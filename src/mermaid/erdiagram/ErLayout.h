#pragma once

#include "mermaid/erdiagram/ErDiagram.h"

#include <QJsonObject>
#include <QMap>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QVector>

#include <optional>

namespace muffin::mermaid::er {

// Layout-projection model, mirroring StateLayout.h. The er diagram is projected
// into the flowchart dagre pipeline (vertices = entities, edges = relationships,
// no subgraphs) exactly as StateLayout.cpp projects state nodes/edges. See
// IMPLEMENTATION_SPEC.md for the projection rules.

// `attributeLines` are pre-formatted rows ("type name [PK|FK|UK] [comment]")
// used to size the entity table; `attributeStyles` are the per-row style hooks
// (currently informational; populated for upstream-parity and future styling).
struct ErLayoutEntityInput {
  QString id;
  QString name;
  QStringList attributeLines;
  QStringList attributeStyles;
  // Structured per-attribute fields, used by measureErLayoutInput for mermaid's
  // per-column entity sizing (type/name/keys/comment). attributeLines is kept
  // separately because ErScene re-parses it for display.
  QVector<ErAttribute> attributes;
  QString cssClasses;     // classDef names applied via class/cssClass
  QStringList styles;     // inline `style` declarations (key:value)
};

struct ErLayoutRelationshipInput {
  QString id;
  QString entityA;
  QString entityB;
  ErCardinality cardA = ErCardinality::ExactlyOne;
  ErCardinality cardB = ErCardinality::ExactlyOne;
  bool identifying = true;
  QString label;
  QString roleA;
  QString roleB;
};

struct ErLayoutInput {
  QVector<ErLayoutEntityInput> entities;
  QVector<ErLayoutRelationshipInput> relationships;
  // Upstream erDb defaults direction to "TB" and the erDiagram grammar exposes
  // no keyword to change it; buildErLayoutInput therefore always emits "TB".
  // (The struct default is "TB" to match the only real producer.)
  QString direction = QStringLiteral("TB");
};

struct ErLayoutMeasurements {
  QMap<QString, QSizeF> entities;
  QMap<QString, QSizeF> relationships;
};

struct ErPlacementEntity {
  QString id;
  QPointF center;
  QSizeF size;
};

struct ErPlacementRelationship {
  QString id;
  QString path;
  QVector<QPointF> points;
  QVector<QVector<QPointF>> segments;
  std::optional<QPointF> labelPosition;
};

struct ErPlacementResult {
  QVector<ErPlacementEntity> entities;
  QVector<ErPlacementRelationship> relationships;
};

ErLayoutInput buildErLayoutInput(const ErDiagramData& data);
ErLayoutMeasurements measureErLayoutInput(const ErLayoutInput& input,
                                          QString fontFamily = QStringLiteral("Noto Sans"),
                                           qreal fontSize = 16.0,
                                           qreal minEntityWidth = 100.0,
                                           qreal diagramPadding = 20.0,
                                           qreal entityPadding = 15.0,
                                           qreal relationshipFontSize = 14.0);
ErPlacementResult layoutErDiagramDagre(const ErLayoutInput& input,
                                       const ErLayoutMeasurements& measurements,
                                       qreal entitySpacing = 60.0,
                                       qreal rankSpacing = 80.0);
QJsonObject erLayoutInputToJson(const ErLayoutInput& input);

}  // namespace muffin::mermaid::er
