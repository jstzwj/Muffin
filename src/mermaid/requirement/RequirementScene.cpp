// RequirementScene — scene builder + JSON serialization for the requirementDiagram
// family. Mirrors classdiagram::ClassScene.cpp / er::ErScene.cpp.
//
// Per CLAUDE.md / the lupdate convention this .cpp has NO `namespace muffin {}`
// block; helpers live in an anonymous namespace and public functions use
// fully-qualified names.

#include "mermaid/requirement/RequirementScene.h"

#include "mermaid/flowchart/D3Curves.h"
#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/theme/MermaidColor.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace flowchart = muffin::mermaid::flowchart;
namespace color = muffin::mermaid::color;
namespace req = muffin::mermaid::requirement;

namespace {

constexpr qreal kPadding = 20.0;
constexpr qreal kGap = 20.0;

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

// Bounding rect of edge points (+ 18px margin, matching ClassScene's pointsBounds).
QRectF edgePointsBounds(const QVector<QPointF>& points,
                        const QVector<QVector<QPointF>>& segments) {
  QRectF bounds;
  bool initialized = false;
  auto includePoint = [&](const QPointF& point) {
    const QRectF pixel(point - QPointF(0.5, 0.5), QSizeF(1.0, 1.0));
    if (!initialized) {
      bounds = pixel;
      initialized = true;
    } else {
      bounds = bounds.united(pixel);
    }
  };
  for (const QPointF& point : points) includePoint(point);
  for (const QVector<QPointF>& segment : segments)
    for (const QPointF& point : segment) includePoint(point);
  return initialized ? bounds.adjusted(-18.0, -18.0, 18.0, 18.0) : QRectF{};
}

// getStrokeDashArray (chunk-BNCO5QFQ.mjs): split on \s+, single value ->
// [v, v], two values -> [a, b], NaN -> 0. Returns {0,0} for an empty value.
QVector<qreal> getStrokeDashArray(const QString& strokeDasharrayStyle) {
  if (strokeDasharrayStyle.trimmed().isEmpty()) return {0.0, 0.0};
  static const QRegularExpression ws(QStringLiteral("\\s+"));
  const QStringList raw = strokeDasharrayStyle.trimmed().split(ws, Qt::SkipEmptyParts);
  QVector<qreal> arr;
  for (const QString& p : raw) {
    bool ok = false;
    const qreal v = p.trimmed().toDouble(&ok);
    arr.append(ok ? v : 0.0);  // NaN -> 0
  }
  if (arr.size() == 1) return {arr.first(), arr.first()};
  if (arr.size() >= 2) return {arr.at(0), arr.at(1)};
  return {0.0, 0.0};
}

// compileStyles (chunk-BNCO5QFQ.mjs) for the box: build a Map (last value per
// key wins) from the node's event-ordered declaration list — styles2Map splits
// each "key:value" on the FIRST ':' — then userNodeOverrides reads fill/stroke/
// stroke-width/stroke-dasharray. Box-outline and divider share strokeWidth and
// dashArray; the divider follows an explicit stroke, else keeps the theme
// divider color. labelStyle keys (color/font-*/...) are split out by isLabelStyle
// upstream and do not affect the box — they are ignored here (text phase).
void resolveBoxStyle(const QStringList& cssStyles, const req::RequirementSceneStyle& base,
                     req::RequirementSceneNode& node) {
  node.fill = base.boxFill;
  node.stroke = base.boxStroke;
  node.dividerStroke = base.dividerColor;
  node.strokeValid = true;
  node.strokeWidth = base.strokeWidth;
  node.dashArray = {0.0, 0.0};

  if (cssStyles.isEmpty()) return;

  QHash<QString, QString> map;  // styles2Map: last value per key wins
  for (const QString& decl : cssStyles) {
    const int colon = decl.indexOf(QLatin1Char(':'));
    if (colon <= 0) continue;  // stray token (e.g. the "2" lost from "5,2")
    map.insert(decl.left(colon).trimmed(), decl.mid(colon + 1).trimmed());
  }

  // fill: invalid -> inherited foreground (mermaid drops the declaration).
  if (map.contains(QStringLiteral("fill"))) {
    const QString v = map.value(QStringLiteral("fill"));
    node.fill = color::isParsableColor(v) ? v : base.foregroundFallback;
  }
  // stroke: invalid -> none (applies to box outline AND divider).
  if (map.contains(QStringLiteral("stroke"))) {
    const QString v = map.value(QStringLiteral("stroke"));
    if (color::isParsableColor(v))
      node.stroke = node.dividerStroke = v;
    else
      node.strokeValid = false;
  }
  // stroke-width: userNodeOverrides `sw.replace("px","") || default`; em is
  // resolved against the root font (themeVariables.fontSize = style.fontSize).
  if (map.contains(QStringLiteral("stroke-width"))) {
    QString s = map.value(QStringLiteral("stroke-width"));
    s.remove(QStringLiteral("px"));  // userNodeOverrides: replace("px","") (case-sensitive)
    bool ok = false;
    if (s.endsWith(QLatin1String("em"), Qt::CaseInsensitive)) {
      const qreal n = s.left(s.size() - 2).toDouble(&ok);
      node.strokeWidth = ok ? n * base.fontSize : base.strokeWidth;
    } else {
      const qreal n = s.toDouble(&ok);
      node.strokeWidth = ok ? n : base.strokeWidth;
    }
  }
  // stroke-dasharray.
  if (map.contains(QStringLiteral("stroke-dasharray")))
    node.dashArray = getStrokeDashArray(map.value(QStringLiteral("stroke-dasharray")));
}

}  // namespace

namespace muffin::mermaid::requirement {

QVector<RequirementMarkerDefinition> requirementMarkerDefinitions() {
  QVector<RequirementMarkerDefinition> result;
  // requirement_contains (Start marker): refX=0, refY=10, 20×20. A circle r=9
  // (cx=cy=10, fill=none) + horizontal line (1,10→19,10) + vertical line
  // (10,1→10,19). chunk-52WLFC77.mjs ~1034-1039.
  RequirementMarkerDefinition contains;
  contains.type = QStringLiteral("requirement_contains");
  contains.suffix = QStringLiteral("Start");
  contains.refX = 0.0;
  contains.refY = 10.0;
  contains.markerWidth = 20.0;
  contains.markerHeight = 20.0;
  contains.isContains = true;
  result.append(contains);
  // requirement_arrow (End marker): refX=20, refY=10, 20×20. Open V:
  // M0,0 L20,10 M20,10 L0,20. chunk-52WLFC77.mjs ~1013-1021.
  RequirementMarkerDefinition arrow;
  arrow.type = QStringLiteral("requirement_arrow");
  arrow.suffix = QStringLiteral("End");
  arrow.refX = 20.0;
  arrow.refY = 10.0;
  arrow.markerWidth = 20.0;
  arrow.markerHeight = 20.0;
  arrow.isContains = false;
  arrow.arrowPath = QStringLiteral("M0,0 L20,10 M20,10 L0,20");
  result.append(arrow);
  return result;
}

RequirementScene buildRequirementScene(
    const RequirementLayoutInput& input,
    const RequirementLayoutMeasurements& measurements,
    const RequirementPlacementResult& placement,
    RequirementSceneStyle style) {
  RequirementScene scene;
  scene.style = std::move(style);
  scene.markers = requirementMarkerDefinitions();

  QHash<QString, RequirementPlacementNode> placedNodes;
  for (const auto& node : placement.nodes) placedNodes.insert(node.id, node);

  for (const RequirementLayoutNodeInput& node : input.nodes) {
    if (!placedNodes.contains(node.id)) continue;
    const RequirementPlacementNode placed = placedNodes.value(node.id);
    const RequirementNodeMeasurement measured = measurements.value(node.id);
    RequirementSceneNode rendered;
    rendered.id = node.id;
    rendered.displayType = node.displayType;
    rendered.isElement = node.isElement;
    rendered.center = QPointF(placed.x, placed.y);
    rendered.size = QSizeF(placed.width, placed.height);
    rendered.bodyRowCount = static_cast<int>(std::max<qsizetype>(0, node.rows.size() - 2));
    const qreal totalHeight = placed.height;
    const qreal totalWidth = placed.width;
    rendered.hasDivider = node.hasBody;
    // Mermaid draws the divider at the body top: lineY = y + typeHeight +
    // nameHeight + gap where y = -totalHeight/2 is the box top (no padding) —
    // chunk-ZGVPDNZ5.mjs / chunk-65BZPYT2.mjs. Computed once here from the
    // measurements so the painter does not re-derive it from row centers.
    if (node.hasBody) {
      rendered.dividerY = -totalHeight / 2.0 +
                          measured.typeHeight + measured.nameHeight + kGap;
    }
    // Resolve the box paint (fill/stroke/stroke-width/dash) from the node's
    // event-ordered cssStyles, last-wins over the theme base. The divider
    // follows the explicit stroke/stroke-width and otherwise keeps the theme
    // divider color; both default to the 1.3 requirement border size.
    resolveBoxStyle(node.cssStyles, scene.style, rendered);

    // Row positions (relative to node center). Mermaid positions rows at
    // sequential y-offsets, then shifts so the box is centered. Row i's text
    // center Y (relative) = yoffset_i - totalHeight/2 + padding, where
    // totalHeight = the dagre box height.
    qreal yoffset = 0.0;
    for (qsizetype i = 0; i < node.rows.size(); ++i) {
      const RequirementLayoutRow& row = node.rows.at(i);
      const qreal rowHeight = measured.rowHeights.value(i, 0.0);
      // After the name row (i==1), insert the gap before body rows.
      // The gap is already baked into totalHeight; here we advance yoffset.
      RequirementSceneRow sceneRow;
      sceneRow.text = row.text;
      sceneRow.bold = row.bold;
      sceneRow.size = QSizeF(0.0, rowHeight);  // width filled by measuring the doc below
      // Horizontal: type(0) + name(1) are centered (x=0); body rows are
      // left-aligned at -totalWidth/2 + padding/2 (mermaid's translateX for i>=2).
      qreal centerX = 0.0;
      if (i >= 2) {
        // Measure this row's width to center the label rect on its left-anchored
        // position.
        const auto doc = requirementRowDocument(row.text, scene.style.fontSize, row.bold);
        const QRectF ink = flowchart::measureFlowSvgTextBounds(
            doc, scene.style.fontFamily, scene.style.fontSize);
        sceneRow.size.setWidth(ink.width());
        centerX = -totalWidth / 2.0 + kPadding / 2.0 + ink.width() / 2.0;
      } else {
        const auto doc = requirementRowDocument(row.text, scene.style.fontSize, row.bold);
        const QRectF ink = flowchart::measureFlowSvgTextBounds(
            doc, scene.style.fontFamily, scene.style.fontSize);
        sceneRow.size.setWidth(ink.width());
      }
      const qreal centerY = yoffset - totalHeight / 2.0 + kPadding;
      sceneRow.center = QPointF(centerX, centerY);
      sceneRow.document = requirementRowDocument(row.text, scene.style.fontSize, row.bold);
      rendered.rows.append(std::move(sceneRow));
      yoffset += rowHeight;
      if (i == 1 && node.hasBody) yoffset += kGap;
    }
    scene.nodes.append(std::move(rendered));
  }

  // Edges: deduplicate by id (matching mermaid's shared dagre pipeline, which
  // renders one edge per id). The mermaid DB assigns id `${src}-${dst}-0` to
  // every relation, so same-pair relations collide and the LAST one wins. We
  // iterate in reverse and skip already-seen ids so the last input edge per id
  // is the one rendered.
  QSet<QString> seenEdgeIds;
  QVector<const RequirementLayoutEdgeInput*> edgesInRenderOrder;
  for (auto it = input.edges.rbegin(); it != input.edges.rend(); ++it) {
    if (seenEdgeIds.contains(it->id)) continue;
    seenEdgeIds.insert(it->id);
    edgesInRenderOrder.prepend(&*it);
  }
  for (const RequirementLayoutEdgeInput* edgePtr : edgesInRenderOrder) {
    const RequirementLayoutEdgeInput& edge = *edgePtr;
    const auto found = std::find_if(placement.edges.cbegin(), placement.edges.cend(),
        [&](const auto& value) { return value.id == edge.id; });
    if (found == placement.edges.cend()) continue;
    RequirementSceneEdge rendered;
    rendered.id = edge.id;
    rendered.path = found->path;
    rendered.points = found->points;
    rendered.segments = found->segments;
    rendered.label = edge.label;
    rendered.relationshipType = edge.relationshipType;
    rendered.isContains = edge.isContains;
    rendered.pattern = edge.isContains ? QStringLiteral("solid") : QStringLiteral("dashed");
    rendered.markerStart = edge.isContains ? QStringLiteral("requirement_contains") : QString();
    rendered.markerEnd = edge.isContains ? QString() : QStringLiteral("requirement_arrow");
    rendered.labelPosition = found->labelPosition;
    if (!edge.label.isEmpty()) {
      rendered.labelDocument =
          flowchart::parseFlowLabel(edge.label, QStringLiteral("markdown"));
      rendered.labelSize = flowchart::measureFlowLabel(
          rendered.labelDocument, scene.style.fontFamily,
          scene.style.fontSize, scene.style.lineHeight);
      if (rendered.labelPosition) {
        rendered.labelBounds = QRectF(
            *rendered.labelPosition -
                QPointF(rendered.labelSize.width() / 2.0,
                        rendered.labelSize.height() / 2.0),
            rendered.labelSize);
      }
    }
    rendered.pathBounds = edgePointsBounds(found->points, found->segments);
    // If the dagre pipeline returned an empty path, synthesize a basis curve
    // from the points (matching ClassScene's path fallback).
    if (rendered.path.isEmpty() && !rendered.points.isEmpty()) {
      rendered.path = flowchart::d3curve::pathForCurve(rendered.points,
                                                       QStringLiteral("basis"));
    }
    scene.edges.append(std::move(rendered));
  }

  // Scene bounds = union of all node boxes + edge points (mirrors ClassScene).
  bool first = true;
  auto unite = [&](const QRectF& bounds) {
    if (first) {
      scene.bounds = bounds;
      first = false;
    } else {
      scene.bounds = scene.bounds.united(bounds);
    }
  };
  for (const auto& node : scene.nodes)
    unite(QRectF(node.center.x() - node.size.width() / 2.0,
                 node.center.y() - node.size.height() / 2.0,
                 node.size.width(), node.size.height()));
  for (const auto& edge : scene.edges) {
    for (const QPointF& point : edge.points)
      unite(QRectF(point, QSizeF(0.0, 0.0)));
    for (const QVector<QPointF>& segment : edge.segments)
      for (const QPointF& point : segment) unite(QRectF(point, QSizeF(0.0, 0.0)));
  }
  return scene;
}

QJsonObject RequirementScene::toJsonObject() const {
  QJsonObject o;
  o[QStringLiteral("bounds")] = rectJson(bounds);

  QJsonArray nodesArray;
  for (const RequirementSceneNode& node : nodes) {
    QJsonObject n;
    n[QStringLiteral("id")] = node.id;
    if (!node.displayType.isEmpty())
      n[QStringLiteral("type")] = node.displayType;
    n[QStringLiteral("cx")] = r3(node.center.x());
    n[QStringLiteral("cy")] = r3(node.center.y());
    n[QStringLiteral("width")] = r3(node.size.width());
    n[QStringLiteral("height")] = r3(node.size.height());
    n[QStringLiteral("bodyRows")] = node.bodyRowCount;
    n[QStringLiteral("dividers")] = node.hasDivider ? 1 : 0;
    if (node.hasDivider) n[QStringLiteral("dividerY")] = r3(node.dividerY);
    if (!node.fill.isEmpty())
      n[QStringLiteral("fill")] = node.fill;
    if (!node.stroke.isEmpty())
      n[QStringLiteral("stroke")] = node.stroke;
    if (!node.strokeValid)
      n[QStringLiteral("strokeValid")] = false;
    n[QStringLiteral("strokeWidth")] = r3(node.strokeWidth);
    if (!node.dashArray.isEmpty() &&
        (node.dashArray.at(0) != 0.0 || node.dashArray.at(1) != 0.0)) {
      QJsonArray dash;
      dash.append(r3(node.dashArray.at(0)));
      dash.append(r3(node.dashArray.at(1)));
      n[QStringLiteral("dashArray")] = dash;
    }
    nodesArray.append(n);
  }
  o[QStringLiteral("nodes")] = nodesArray;

  QJsonArray edgesArray;
  for (const RequirementSceneEdge& edge : edges) {
    QJsonObject e;
    e[QStringLiteral("id")] = edge.id;
    if (!edge.pattern.isEmpty())
      e[QStringLiteral("pattern")] = edge.pattern;
    if (!edge.markerStart.isEmpty())
      e[QStringLiteral("markerStart")] = edge.markerStart;
    if (!edge.markerEnd.isEmpty())
      e[QStringLiteral("markerEnd")] = edge.markerEnd;
    if (!edge.relationshipType.isEmpty())
      e[QStringLiteral("type")] = edge.relationshipType;
    if (!edge.label.isEmpty())
      e[QStringLiteral("label")] = edge.label;
    e[QStringLiteral("points")] = pointsJson(edge.points);
    edgesArray.append(e);
  }
  o[QStringLiteral("edges")] = edgesArray;

  QJsonArray markersArray;
  for (const RequirementMarkerDefinition& marker : markers) {
    QJsonObject m;
    m[QStringLiteral("type")] = marker.type;
    if (!marker.suffix.isEmpty())
      m[QStringLiteral("suffix")] = marker.suffix;
    markersArray.append(m);
  }
  o[QStringLiteral("markers")] = markersArray;

  return o;
}

}  // namespace muffin::mermaid::requirement
