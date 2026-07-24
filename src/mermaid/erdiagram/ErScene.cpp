#include "mermaid/erdiagram/ErScene.h"

#include <QHash>
#include <QPointF>
#include <QRectF>
#include <QRegularExpression>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <utility>

// erDiagram scene builder. Mirrors src/mermaid/classdiagram/ClassScene.cpp::
// buildClassScene. Per CLAUDE.md / IMPLEMENTATION_SPEC.md this .cpp defines its
// free function with fully-qualified names (er::buildErScene) and keeps helpers
// in an anonymous namespace — it is NOT wrapped in a `namespace muffin {}` block.
namespace er = muffin::mermaid::er;
namespace flowchart = muffin::mermaid::flowchart;

namespace {

// Re-parses one attribute line produced by buildErLayoutInput
// (`type name [PK|FK|UK] ["comment"]`) back into structured fields. The frozen
// ErLayoutEntityInput only carries the pre-formatted line, so buildErScene
// recovers type/name/key/comment here (IMPLEMENTATION_SPEC.md §6).
struct ParsedAttribute {
  QString type;
  QString name;
  QString keyType;  // "" / "PK" / "FK" / "UK"
  QString comment;
  QString core;     // line with any trailing quoted comment stripped, trimmed
};

ParsedAttribute parseAttributeLine(const QString& line) {
  ParsedAttribute result;
  QString working = line;
  const int firstQuote = working.indexOf(QLatin1Char('"'));
  if (firstQuote >= 0) {
    const int lastQuote = working.lastIndexOf(QLatin1Char('"'));
    if (lastQuote > firstQuote)
      result.comment =
          working.mid(firstQuote + 1, lastQuote - firstQuote - 1);
    working = working.left(firstQuote);
  }
  result.core = working.trimmed();
  const QStringList parts = working.split(
      QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
  if (!parts.isEmpty()) result.type = parts.first();
  if (parts.size() >= 2) result.name = parts.at(1);
  for (int i = 2; i < parts.size(); ++i) {
    const QString& token = parts.at(i);
    if (token == QLatin1String("PK") || token == QLatin1String("FK") ||
        token == QLatin1String("UK")) {
      result.keyType = token;
      break;
    }
  }
  return result;
}

// Builds the displayed attribute row text: "type name" plus " [KEY]" when a key
// marker is present (IMPLEMENTATION_SPEC.md §6). Falls back to the comment-free
// core text for malformed lines.
QString attributeDisplayText(const ParsedAttribute& parsed) {
  if (!parsed.type.isEmpty() && !parsed.name.isEmpty()) {
    QString text = parsed.type + QLatin1Char(' ') + parsed.name;
    if (!parsed.keyType.isEmpty())
      text += QStringLiteral(" [") + parsed.keyType + QLatin1Char(']');
    return text;
  }
  return parsed.core;
}

// Bounding rect of the raw edge geometry (points + segments) used for culling
// (IMPLEMENTATION_SPEC.md §6 pathBounds).
QRectF edgePointsBounds(const QVector<QPointF>& points,
                        const QVector<QVector<QPointF>>& segments) {
  QRectF bounds;
  bool initialized = false;
  auto include = [&](const QPointF& point) {
    const QRectF pixel(point, QSizeF(0.0, 0.0));
    if (!initialized) {
      bounds = pixel;
      initialized = true;
    } else {
      bounds = bounds.united(pixel);
    }
  };
  for (const QPointF& point : points) include(point);
  for (const QVector<QPointF>& segment : segments)
    for (const QPointF& point : segment) include(point);
  return bounds;
}

}  // namespace

er::ErScene er::buildErScene(const er::ErLayoutInput& input,
                             const er::ErPlacementResult& placement,
                             er::ErSceneStyle style) {
  er::ErScene scene;
  scene.style = std::move(style);

  QHash<QString, const er::ErLayoutEntityInput*> entitiesById;
  for (const er::ErLayoutEntityInput& entity : input.entities)
    entitiesById.insert(entity.id, &entity);
  QHash<QString, const er::ErLayoutRelationshipInput*> relationshipsById;
  for (const er::ErLayoutRelationshipInput& relationship : input.relationships)
    relationshipsById.insert(relationship.id, &relationship);

  for (const er::ErPlacementEntity& placed : placement.entities) {
    er::ErSceneEntity entity;
    entity.id = placed.id;
    const auto found = entitiesById.find(placed.id);
    const er::ErLayoutEntityInput* source =
        found != entitiesById.end() ? found.value() : nullptr;
    entity.name = source ? source->name : placed.id;
    entity.bounds =
        QRectF(placed.center - QPointF(placed.size.width() / 2.0,
                                       placed.size.height() / 2.0),
               placed.size);
    entity.headerRect =
        QRectF(entity.bounds.left(), entity.bounds.top(),
               entity.bounds.width(), scene.style.lineHeight);
    if (source) {
      for (const QString& line : source->attributeLines) {
        if (line.trimmed().isEmpty()) continue;
        const ParsedAttribute parsed = parseAttributeLine(line);
        er::ErSceneAttribute attribute;
        attribute.text = attributeDisplayText(parsed);
        attribute.keyType = parsed.keyType;
        attribute.comment = parsed.comment;
        entity.attributes.append(std::move(attribute));
      }
    }
    // Stack one row rect (sized to the style line height) per attribute below
    // the header band; the painter reuses the pre-shaped FlowLabelDocument.
    for (qsizetype i = 0; i < entity.attributes.size(); ++i) {
      const qreal rowTop =
          entity.bounds.top() + scene.style.lineHeight * (i + 1);
      entity.attributeRects.append(
          QRectF(entity.bounds.left(), rowTop, entity.bounds.width(),
                 scene.style.lineHeight));
      entity.attributeDocuments.append(flowchart::parseFlowLabel(
          entity.attributes.at(i).text, QStringLiteral("text")));
    }
    entity.nameDocument =
        flowchart::parseFlowLabel(entity.name, QStringLiteral("text"));
    scene.entities.append(std::move(entity));
  }

  for (const er::ErPlacementRelationship& placed : placement.relationships) {
    er::ErSceneRelationship relationship;
    relationship.id = placed.id;
    relationship.path = placed.path;
    relationship.points = placed.points;
    relationship.segments = placed.segments;
    relationship.labelPosition = placed.labelPosition;
    const auto found = relationshipsById.find(placed.id);
    const er::ErLayoutRelationshipInput* source =
        found != relationshipsById.end() ? found.value() : nullptr;
    if (source) {
      relationship.cardA = source->cardA;
      relationship.cardB = source->cardB;
      relationship.identifying = source->identifying;
      relationship.roleA = source->roleA;
      relationship.roleB = source->roleB;
      relationship.label = source->label;
    }
    // Edge label mirrors ClassScene edge label (parseFlowLabel + measureFlowLabel
    // + a centered labelBounds rect), using the plain-text "text" label type.
    if (!relationship.label.isEmpty()) {
      relationship.labelDocument = flowchart::parseFlowLabel(
          relationship.label, QStringLiteral("text"));
      relationship.labelSize = flowchart::measureFlowLabel(
          relationship.labelDocument, scene.style.fontFamily,
          scene.style.fontSize, scene.style.lineHeight);
      if (relationship.labelPosition) {
        relationship.labelBounds =
            QRectF(*relationship.labelPosition -
                       QPointF(relationship.labelSize.width() / 2.0,
                               relationship.labelSize.height() / 2.0),
                   relationship.labelSize);
      }
    }
    relationship.pathBounds =
        edgePointsBounds(placed.points, placed.segments);
    scene.relationships.append(std::move(relationship));
  }

  // scene.bounds = union of all entity boxes + edge points
  // (IMPLEMENTATION_SPEC.md §6).
  bool first = true;
  auto unite = [&](const QRectF& bounds) {
    if (first) {
      scene.bounds = bounds;
      first = false;
    } else {
      scene.bounds = scene.bounds.united(bounds);
    }
  };
  for (const er::ErSceneEntity& entity : scene.entities)
    unite(entity.bounds);
  for (const er::ErSceneRelationship& relationship : scene.relationships) {
    for (const QPointF& point : relationship.points)
      unite(QRectF(point, QSizeF(0.0, 0.0)));
    for (const QVector<QPointF>& segment : relationship.segments)
      for (const QPointF& point : segment)
        unite(QRectF(point, QSizeF(0.0, 0.0)));
  }
  return scene;
}
