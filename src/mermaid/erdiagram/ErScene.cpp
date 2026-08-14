#include "mermaid/erdiagram/ErScene.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QRegularExpression>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cmath>
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

qreal r3(qreal v) { return std::round(v * 1000.0) / 1000.0; }

QJsonObject rectJson(const QRectF& r) {
  return {{QStringLiteral("x"), r3(r.x())},
          {QStringLiteral("y"), r3(r.y())},
          {QStringLiteral("width"), r3(r.width())},
          {QStringLiteral("height"), r3(r.height())}};
}

QJsonObject pointJson(const QPointF& p) {
  return {{QStringLiteral("x"), r3(p.x())}, {QStringLiteral("y"), r3(p.y())}};
}

QJsonArray pointsJson(const QVector<QPointF>& pts) {
  QJsonArray a;
  for (const QPointF& p : pts) {
    QJsonArray pair;
    pair.append(r3(p.x()));
    pair.append(r3(p.y()));
    a.append(pair);
  }
  return a;
}

QString cardName(er::ErCardinality c) {
  switch (c) {
    case er::ErCardinality::ExactlyOne: return QStringLiteral("ExactlyOne");
    case er::ErCardinality::ZeroOrOne:  return QStringLiteral("ZeroOrOne");
    case er::ErCardinality::OneOrMore:  return QStringLiteral("OneOrMore");
    case er::ErCardinality::ZeroOrMore: return QStringLiteral("ZeroOrMore");
  }
  return {};
}

}  // namespace

er::ErScene er::buildErScene(const er::ErLayoutInput& input,
                             const er::ErPlacementResult& placement,
                             er::ErSceneStyle style,
                             const QVector<muffin::mermaid::style::ClassDef>& classDefs,
                             const muffin::mermaid::style::ThemeDefaults& themeDefaults) {
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
    // Resolve entity classDef / inline style via the shared cascade. Empty
    // resolved values fall back to scene.style (the painter checks).
    if (source) {
      const QStringList classes = source->cssClasses.split(
          QLatin1Char(' '), Qt::SkipEmptyParts);
      const auto resolved = muffin::mermaid::style::resolveNodeStyle(
          classes, source->styles, classDefs, themeDefaults);
      entity.fill = resolved.fill;
      entity.stroke = resolved.stroke;
    }
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

QJsonObject er::ErScene::toJsonObject() const {
  QJsonObject o;
  o[QStringLiteral("role")] = role;
  o[QStringLiteral("ariaRoleDescription")] = ariaRoleDescription;
  o[QStringLiteral("bounds")] = rectJson(bounds);

  QJsonArray entitiesArray;
  for (const er::ErSceneEntity& entity : entities) {
    QJsonObject en;
    en[QStringLiteral("id")] = entity.id;
    en[QStringLiteral("name")] = entity.name;
    en[QStringLiteral("bounds")] = rectJson(entity.bounds);
    en[QStringLiteral("headerRect")] = rectJson(entity.headerRect);
    QJsonArray attributesArray;
    for (const er::ErSceneAttribute& attribute : entity.attributes) {
      QJsonObject at;
      at[QStringLiteral("text")] = attribute.text;
      if (!attribute.keyType.isEmpty())
        at[QStringLiteral("keyType")] = attribute.keyType;
      if (!attribute.comment.isEmpty())
        at[QStringLiteral("comment")] = attribute.comment;
      attributesArray.append(at);
    }
    en[QStringLiteral("attributes")] = attributesArray;
    entitiesArray.append(en);
  }
  o[QStringLiteral("entities")] = entitiesArray;

  QJsonArray relationshipsArray;
  for (const er::ErSceneRelationship& relationship : relationships) {
    QJsonObject rel;
    rel[QStringLiteral("id")] = relationship.id;
    rel[QStringLiteral("path")] = relationship.path;
    rel[QStringLiteral("points")] = pointsJson(relationship.points);
    rel[QStringLiteral("cardA")] = cardName(relationship.cardA);
    rel[QStringLiteral("cardB")] = cardName(relationship.cardB);
    rel[QStringLiteral("identifying")] = relationship.identifying;
    if (!relationship.roleA.isEmpty())
      rel[QStringLiteral("roleA")] = relationship.roleA;
    if (!relationship.roleB.isEmpty())
      rel[QStringLiteral("roleB")] = relationship.roleB;
    if (!relationship.label.isEmpty())
      rel[QStringLiteral("label")] = relationship.label;
    if (relationship.labelPosition.has_value())
      rel[QStringLiteral("labelPosition")] = pointJson(*relationship.labelPosition);
    relationshipsArray.append(rel);
  }
  o[QStringLiteral("relationships")] = relationshipsArray;

  return o;
}

muffin::mermaid::SvgMarkerProjection er::ErScene::svgMarkerProjection() const {
  using C = er::ErCardinality;
  SvgMarkerProjection projection;
  const auto add = [&](QString key, qreal refX, qreal refY,
                       qreal width, qreal height, QString path,
                       bool circle, qreal cx, qreal cy, qreal radius) {
    SvgMarkerDefinition definition;
    definition.key = key;
    definition.idSuffix = QStringLiteral("_er-") + key;
    definition.refX = refX; definition.refY = refY;
    definition.markerWidth = width; definition.markerHeight = height;
    if (circle) {
      SvgMarkerChild child;
      child.tag = QStringLiteral("circle"); child.cx = cx; child.cy = cy;
      child.radius = radius; child.fill = QStringLiteral("white");
      child.stroke = style.relationshipColor;
      definition.children.append(child);
    }
    SvgMarkerChild child;
    child.tag = QStringLiteral("path"); child.path = std::move(path);
    child.fill = QStringLiteral("none"); child.stroke = style.relationshipColor;
    definition.children.append(child);
    projection.definitions.append(definition);
  };
  add(QStringLiteral("onlyOneStart"), 0, 9, 18, 18,
      QStringLiteral("M9,0 L9,18 M15,0 L15,18"), false, 0, 0, 0);
  add(QStringLiteral("onlyOneEnd"), 18, 9, 18, 18,
      QStringLiteral("M3,0 L3,18 M9,0 L9,18"), false, 0, 0, 0);
  add(QStringLiteral("zeroOrOneStart"), 0, 9, 30, 18,
      QStringLiteral("M9,0 L9,18"), true, 21, 9, 6);
  add(QStringLiteral("zeroOrOneEnd"), 30, 9, 30, 18,
      QStringLiteral("M21,0 L21,18"), true, 9, 9, 6);
  add(QStringLiteral("oneOrMoreStart"), 18, 18, 45, 36,
      QStringLiteral("M0,18 Q 18,0 36,18 Q 18,36 0,18 M42,9 L42,27"), false, 0, 0, 0);
  add(QStringLiteral("oneOrMoreEnd"), 27, 18, 45, 36,
      QStringLiteral("M3,9 L3,27 M9,18 Q27,0 45,18 Q27,36 9,18"), false, 0, 0, 0);
  add(QStringLiteral("zeroOrMoreStart"), 18, 18, 57, 36,
      QStringLiteral("M0,18 Q18,0 36,18 Q18,36 0,18"), true, 48, 18, 6);
  add(QStringLiteral("zeroOrMoreEnd"), 39, 18, 57, 36,
      QStringLiteral("M21,18 Q39,0 57,18 Q39,36 21,18"), true, 9, 18, 6);
  const auto key = [](C card, bool start) {
    QString value;
    switch (card) {
      case C::ExactlyOne: value = QStringLiteral("onlyOne"); break;
      case C::ZeroOrOne: value = QStringLiteral("zeroOrOne"); break;
      case C::OneOrMore: value = QStringLiteral("oneOrMore"); break;
      case C::ZeroOrMore: value = QStringLiteral("zeroOrMore"); break;
    }
    return value + (start ? QStringLiteral("Start") : QStringLiteral("End"));
  };
  for (const ErSceneRelationship& source : relationships) {
    SvgMarkerEdge edge;
    edge.id = source.id; edge.cssClass = QStringLiteral("relationshipLine");
    edge.path = source.path; edge.markerStart = key(source.cardA, true);
    edge.markerEnd = key(source.cardB, false);
    edge.stroke = style.relationshipColor;
    edge.strokeWidth = QString::number(style.relationshipStrokeWidth);
    if (!source.identifying) edge.strokeDasharray = QStringLiteral("6,4");
    projection.edges.append(edge);
  }
  return projection;
}
