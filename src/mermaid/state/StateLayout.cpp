#include "mermaid/state/StateLayout.h"
#include "mermaid/state/StateRough.h"

#include "mermaid/flowchart/FlowchartLayout.h"

#include <QMap>

#include <algorithm>

namespace muffin::mermaid::state {
namespace {
struct CachedNode {
  QString id;
  QString shape = QStringLiteral("rect");
  QJsonValue description;
  QString cssClasses;
  QStringList cssStyles;
  QStringList styles;
  QString type;
  QString direction;
};

class Builder {
public:
  Builder(const StateDiagramData& data, QString look)
      : data_(data), look_(std::move(look)) {
    for (const StateNode& state : data.states) states_.insert(state.id, &state);
  }
  StateLayoutInput build() {
    result_.direction = documentDirection(data_.root);
    QJsonObject root{{QStringLiteral("id"), QStringLiteral("root")},
                     {QStringLiteral("doc"), data_.root}};
    fetch({}, root, true);
    return result_;
  }

private:
  QString documentDirection(const QJsonArray& document) const {
    QString result = QStringLiteral("TB");
    for (const QJsonValue& value : document)
      if (value.isObject() && value.toObject().value(QStringLiteral("stmt")) == QLatin1String("dir"))
        result = value.toObject().value(QStringLiteral("value")).toString();
    return result;
  }
  void insertOrUpdate(StateLayoutNodeInput node) {
    if (node.id.isEmpty() || node.id == QLatin1String("</join></fork>") ||
        node.id == QLatin1String("</choice>"))
      return;
    const auto it = std::find_if(result_.nodes.begin(), result_.nodes.end(),
        [&](const StateLayoutNodeInput& value) { return value.id == node.id; });
    if (it == result_.nodes.end()) result_.nodes.append(std::move(node));
    else *it = std::move(node);
  }
  QString classesFor(const QString& id) const {
    const StateNode* state = states_.value(id, nullptr);
    return state ? state->classes.join(QLatin1Char(' ')) : QString{};
  }
  QStringList stylesFor(const QString& id) const {
    const StateNode* state = states_.value(id, nullptr);
    return state ? state->styles : QStringList{};
  }
  // compileStyles (chunk-YI7H2ERT.mjs getCompiledStyles): for each class on the
  // state, push the classDef's node styles; then the inline `style` declarations.
  // The classDef parser (StateDiagram.cpp) also routes `color:X` into textStyles
  // as a transformed `fill:X` — but that is a LABEL-bucket style (label fill ==
  // text color) in upstream, distinct from the node-bucket fill. We keep only the
  // node bucket here; textColor already comes from the `color` key in styles.
  // Declarations apply in order, last-wins per key (the painter re-splits them).
  QStringList mergedStylesFor(const QString& id) const {
    const StateNode* state = states_.value(id, nullptr);
    if (!state) return {};
    QStringList result;
    for (const QString& name : state->classes)
      for (const StateStyleClass& cls : data_.styleClasses)
        if (cls.id == name) {
          result.append(cls.styles);
          break;
        }
    result.append(state->styles);
    return result;
  }
  void setupDocument(const QJsonObject& parent, const QJsonArray& document,
                     bool alternate) {
    for (const QJsonValue& value : document) {
      if (!value.isObject()) continue;
      const QJsonObject item = value.toObject();
      const QString kind = item.value(QStringLiteral("stmt")).toString();
      if (kind == QLatin1String("state")) {
        fetch(parent, item, alternate);
      } else if (kind == QLatin1String("relation")) {
        const QJsonObject left = item.value(QStringLiteral("state1")).toObject();
        const QJsonObject right = item.value(QStringLiteral("state2")).toObject();
        fetch(parent, left, alternate);
        fetch(parent, right, alternate);
        StateLayoutEdgeInput edge;
        edge.id = QStringLiteral("edge%1").arg(graphItemCount_);
        edge.start = left.value(QStringLiteral("id")).toString();
        edge.end = right.value(QStringLiteral("id")).toString();
        edge.label = item.value(QStringLiteral("description")).toString();
        edge.arrowTypeEnd = look_ == QLatin1String("neo")
            ? QStringLiteral("arrow_barb_neo") : QStringLiteral("arrow_barb");
        if (relationIndex_ < data_.relations.size())
          edge.linkStyles = data_.relations.at(relationIndex_).linkStyles;
        ++relationIndex_;
        result_.edges.append(std::move(edge));
        ++graphItemCount_;
      }
    }
  }
  void fetch(const QJsonObject& parent, const QJsonObject& parsedItem,
             bool alternate) {
    const QString itemId = parsedItem.value(QStringLiteral("id")).toString();
    if (itemId != QLatin1String("root")) {
      QString shape = QStringLiteral("rect");
      if (parsedItem.value(QStringLiteral("start")).isBool())
        shape = parsedItem.value(QStringLiteral("start")).toBool()
            ? QStringLiteral("stateStart") : QStringLiteral("stateEnd");
      const QString parsedType = parsedItem.value(QStringLiteral("type")).toString();
      if (!parsedItem.contains(QStringLiteral("type")) &&
          !parsedItem.value(QStringLiteral("start")).isBool())
        shape.clear();
      else if (parsedType != QLatin1String("default"))
        shape = parsedType;

      if (!cache_.contains(itemId)) {
        CachedNode cached;
        cached.id = itemId;
        cached.shape = shape;
        cached.description = itemId;
        cached.cssClasses = classesFor(itemId) + QStringLiteral(" statediagram-state");
        cached.cssStyles = stylesFor(itemId);
        cached.styles = mergedStylesFor(itemId);
        cache_.insert(itemId, cached);
      }
      CachedNode& cached = cache_[itemId];
      const QString description = parsedItem.value(QStringLiteral("description")).toString();
      if (!description.isEmpty()) {
        if (cached.description.isArray()) {
          QJsonArray values = cached.description.toArray();
          values.append(description);
          cached.description = values;
          cached.shape = QStringLiteral("rectWithTitle");
        } else if (!cached.description.toString().isEmpty()) {
          cached.shape = QStringLiteral("rectWithTitle");
          QJsonArray values;
          if (cached.description.toString() != itemId) values.append(cached.description);
          values.append(description);
          cached.description = values;
        } else {
          cached.shape = QStringLiteral("rect");
          cached.description = description;
        }
      }
      if (cached.description.isArray() && cached.description.toArray().size() == 1 &&
          cached.shape == QLatin1String("rectWithTitle"))
        cached.shape = cached.type == QLatin1String("group")
            ? QStringLiteral("roundedWithTitle") : QStringLiteral("rect");

      const QJsonArray childDocument = parsedItem.value(QStringLiteral("doc")).toArray();
      const bool hasDocument = parsedItem.value(QStringLiteral("doc")).isArray();
      if (cached.type.isEmpty() && hasDocument) {
        cached.type = QStringLiteral("group");
        cached.direction = documentDirection(childDocument);
        cached.shape = parsedType == QLatin1String("divider")
            ? QStringLiteral("divider") : QStringLiteral("roundedWithTitle");
        cached.cssClasses += QStringLiteral(" statediagram-cluster ") +
            (alternate ? QStringLiteral("statediagram-cluster-alt") : QString{});
      }
      StateLayoutNodeInput node;
      node.id = itemId;
      node.shape = cached.shape;
      node.label = node.shape == QLatin1String("divider") ? QJsonValue(QString{})
                                                          : cached.description;
      node.direction = cached.direction;
      // Mermaid 11.16.0 records explicitDir in its internal node cache but does
      // not copy it to NodeData. Preserve the observable getData() contract.
      node.explicitDirection = false;
      node.isGroup = cached.type == QLatin1String("group");
      node.cssClasses = cached.cssClasses;
      node.cssStyles = cached.cssStyles;
      node.styles = cached.styles;
      node.description = QJsonValue::Null;
      if (node.label.isArray()) {
        const QJsonArray labels = node.label.toArray();
        node.label = labels.isEmpty() ? QJsonValue(QString{}) : labels.first();
        QJsonArray descriptions;
        for (qsizetype i = 1; i < labels.size(); ++i) descriptions.append(labels.at(i));
        node.description = descriptions.isEmpty() ? QJsonValue::Null
                                                   : QJsonValue(descriptions);
      }
      if (!parent.isEmpty() && parent.value(QStringLiteral("id")).toString() != QLatin1String("root"))
        node.parentId = parent.value(QStringLiteral("id")).toString();

      const QJsonObject note = parsedItem.value(QStringLiteral("note")).toObject();
      if (!note.isEmpty()) {
        const QString position = note.value(QStringLiteral("position")).toString();
        const QString noteId = itemId + QStringLiteral("----note-") + QString::number(graphItemCount_);
        const QString groupId = itemId + QStringLiteral("----parent");
        StateLayoutNodeInput group;
        group.id = groupId; group.shape = QStringLiteral("noteGroup");
        group.label = note.value(QStringLiteral("text")); group.isGroup = true;
        group.cssClasses = cached.cssClasses; group.position = position;
        StateLayoutNodeInput noteNode;
        noteNode.id = noteId; noteNode.shape = QStringLiteral("note");
        noteNode.label = note.value(QStringLiteral("text")); noteNode.parentId = groupId;
        noteNode.cssClasses = QStringLiteral("statediagram-note"); noteNode.position = position;
        ++graphItemCount_;
        insertOrUpdate(group); insertOrUpdate(noteNode); insertOrUpdate(node);
        const bool left = position == QLatin1String("left of");
        StateLayoutEdgeInput edge;
        edge.id = left ? noteId + QLatin1Char('-') + itemId
                       : itemId + QLatin1Char('-') + noteId;
        edge.start = left ? noteId : itemId;
        edge.end = left ? itemId : noteId;
        edge.arrowhead = QStringLiteral("none");
        edge.arrowTypeEnd.clear();
        edge.classes = QStringLiteral("transition note-edge");
        result_.edges.append(edge);
      } else {
        insertOrUpdate(node);
      }
    }
    if (parsedItem.value(QStringLiteral("doc")).isArray())
      setupDocument(parsedItem, parsedItem.value(QStringLiteral("doc")).toArray(), !alternate);
  }

  const StateDiagramData& data_;
  QString look_;
  QMap<QString, const StateNode*> states_;
  QMap<QString, CachedNode> cache_;
  StateLayoutInput result_;
  int graphItemCount_ = 0;
  // Index into data_.relations for linkStyle attachment. Aligns with
  // data_.relations for flat diagrams (the common case); compound-state
  // nesting is a known edge case for index-based linkStyle (same semantics as
  // upstream). Bounds-checked so out-of-range edges simply get no linkStyle.
  int relationIndex_ = 0;
};

QJsonArray strings(const QStringList& values) {
  QJsonArray result;
  for (const QString& value : values) result.append(value);
  return result;
}
}

StateLayoutInput buildStateLayoutInput(const StateDiagramData& data, QString look) {
  return Builder(data, std::move(look)).build();
}

StateLayoutMeasurements measureStateLayoutInput(
    const StateLayoutInput& input, QString fontFamily, qreal fontSize,
    bool handDrawn, quint32 handDrawnSeed, bool shapeHidden) {
  StateLayoutMeasurements result;
  flowchart::FlowTextOptions options;
  options.fontFamily = std::move(fontFamily);
  options.fontPixelSize = fontSize;
  options.lineHeight = fontSize * 1.5;
  options.horizontalPadding = 16.0;
  options.verticalPadding = 16.0;
  // State labels are foreignObject inline boxes. Use Blink's HarfBuzz /
  // LayoutUnit contract before RoughJS and Dagre consume their dimensions.
  options.chromiumInlineWidth = true;
  for (const StateLayoutNodeInput& node : input.nodes) {
    if (node.isGroup) {
      if (node.shape != QLatin1String("noteGroup")) {
        result.clusterLabels.insert(
            node.id, flowchart::measureLabel(node.label.toString(),
                                              QStringLiteral("markdown"), options));
      }
      continue;
    }
    const QString shape = node.shape.isEmpty() ? QStringLiteral("rect") : node.shape;
    QSizeF size;
    QSizeF paintedSize;
    if (shape == QLatin1String("stateStart") || shape == QLatin1String("stateEnd"))
      size = paintedSize = QSizeF(14.0, 14.0);
    else if (shape == QLatin1String("fork") || shape == QLatin1String("join")) {
      size = QSizeF(70.0, 14.0);
      paintedSize = QSizeF(70.0, 10.0);
    } else if (shape == QLatin1String("choice"))
      size = paintedSize = QSizeF(28.0, 28.0);
    else {
      QStringList lines;
      if (node.label.isString()) lines.append(node.label.toString());
      if (node.description.isArray())
        for (const QJsonValue& value : node.description.toArray()) lines.append(value.toString());
      size = flowchart::measureLabel(lines.join(QLatin1Char('\n')), QStringLiteral("markdown"), options);
      // drawState measures a temporary group that contains only the shape
      // (labels are inserted later at the dagre node centre), so the dagre
      // node size = rect box = label + 2*padding. `.node rect { display:none }`
      // removes that box: dagre gets a 0x0 node (rank centres collapse to the
      // bare ranksep) while the label still paints at the centre (probed vs
      // 11.16.0: nodes 24 tall, centre gap 50, painted gap 26).
      if (shapeHidden) {
        paintedSize = size;
        size = QSizeF(0.0, 0.0);
      } else {
        if (shape == QLatin1String("rectWithTitle")) size += QSizeF(8.0, 17.0);
        else if (shape == QLatin1String("note")) size += QSizeF(30.0, 30.0);
        else size += QSizeF(16.0, 16.0);
        paintedSize = size;
      }
    }
    if (handDrawn) {
      const QRectF local(-paintedSize.width() / 2.0,
                         -paintedSize.height() / 2.0,
                         paintedSize.width(), paintedSize.height());
      const QRectF roughBounds = stateRoughBounds(
          stateRoughNodeDrawables(shape, local, handDrawnSeed));
      if (roughBounds.isValid()) {
        size = roughBounds.size();
      }
    }
    result.nodes.insert(node.id, size);
    result.paintedNodes.insert(node.id, paintedSize);
  }
  for (const StateLayoutEdgeInput& edge : input.edges) {
    if (!edge.label.isString() || edge.label.toString().isEmpty()) continue;
    flowchart::FlowEdge projected;
    projected.text = edge.label.toString();
    projected.labelType = QStringLiteral("markdown");
    result.edgeLabels.insert(edge.id, flowchart::measureLabel(
        projected.text, QStringLiteral("markdown"), options));
  }
  return result;
}

StatePlacementResult layoutStateDiagramDagre(
    const StateLayoutInput& input, const StateLayoutMeasurements& measurements,
    qreal nodeSpacing, qreal rankSpacing, bool handDrawn,
    quint32 handDrawnSeed) {
  if (input.nodes.isEmpty()) return {};
  flowchart::FlowchartData projected;
  projected.direction = input.direction;
  for (const StateLayoutNodeInput& node : input.nodes) {
    if (node.isGroup) {
      flowchart::FlowSubgraph group;
      group.id = node.id;
      group.title = node.label.toString();
      group.dir = node.direction;
      group.hasExplicitDir = !node.direction.isEmpty();
      for (const StateLayoutNodeInput& child : input.nodes)
        if (child.parentId == node.id) group.nodes.append(child.id);
      projected.subgraphs.prepend(std::move(group));
    } else {
      flowchart::FlowVertex vertex;
      vertex.id = node.id;
      vertex.text = node.label.toString();
      vertex.type = (node.shape == QLatin1String("stateStart") ||
                     node.shape == QLatin1String("stateEnd"))
          ? node.shape : QStringLiteral("rect");
      projected.vertices.append(std::move(vertex));
    }
  }
  for (const StateLayoutEdgeInput& edge : input.edges) {
    flowchart::FlowEdge projectedEdge;
    projectedEdge.id = edge.id;
    projectedEdge.start = edge.start;
    projectedEdge.end = edge.end;
    projectedEdge.text = edge.label.toString();
    projectedEdge.labelType = QStringLiteral("markdown");
    projected.edges.append(std::move(projectedEdge));
  }
  flowchart::FlowLayoutOptions options;
  options.nodeSpacing = nodeSpacing;
  options.rankSpacing = rankSpacing;
  options.nodePadding = 8.0;
  options.measuredEdgeLabels = measurements.edgeLabels;
  if (handDrawn) {
    options.preserveRecursiveSvgFrame = true;
    options.clusterSizeTransform = [&, handDrawnSeed](
        const QString& id, const QRectF& bounds) {
      const auto semantic = std::find_if(
          input.nodes.cbegin(), input.nodes.cend(),
          [&](const StateLayoutNodeInput& node) { return node.id == id; });
      if (semantic == input.nodes.cend() ||
          semantic->shape == QLatin1String("noteGroup"))
        return bounds.size();
      const qreal titleHeight = measurements.clusterLabels.value(id).height();
      const QRectF rendered = stateRoughBounds(stateRoughClusterDrawables(
          bounds, titleHeight, handDrawnSeed));
      return rendered.isValid() ? rendered.size() : bounds.size();
    };
  }
  const flowchart::FlowLayoutResult placed =
      flowchart::layoutFlowchartNodesDagre(projected, measurements.nodes, options);
  StatePlacementResult result;
  for (const flowchart::FlowLayoutNode& node : placed.nodes)
    result.nodes.append({node.id, QPointF(node.x, node.y), QSizeF(node.width, node.height),
                         measurements.paintedNodes.value(node.id, QSizeF(node.width, node.height)),
                         node.rank});
  for (const flowchart::FlowLayoutEdge& edge : placed.edges) {
    StatePlacementEdge converted;
    converted.id = edge.id;
    converted.path = edge.path;
    converted.points = edge.points;
    converted.segments = edge.segments;
    if (edge.hasLabelPosition) converted.labelPosition = QPointF(edge.labelX, edge.labelY);
    result.edges.append(std::move(converted));
  }
  for (const flowchart::FlowLayoutCluster& cluster : placed.clusters)
    result.clusters.append({cluster.id, QPointF(cluster.x, cluster.y),
                            QSizeF(cluster.width, cluster.height),
                            QSizeF(cluster.logicalWidth, cluster.logicalHeight)});

  // Note position is a semantic ordering constraint in Mermaid's state
  // renderer. The generic graph receives it before ordering; FlowchartData has
  // no equivalent field, so restore the same rank orientation after Dagre.
  if (input.direction == QLatin1String("TB") || input.direction == QLatin1String("BT")) {
    for (const StateLayoutNodeInput& noteGroup : input.nodes) {
      if (!noteGroup.isGroup || noteGroup.shape != QLatin1String("noteGroup") ||
          noteGroup.position.isEmpty()) continue;
      const QString suffix = QStringLiteral("----parent");
      if (!noteGroup.id.endsWith(suffix)) continue;
      const QString stateId = noteGroup.id.left(noteGroup.id.size() - suffix.size());
      const auto state = std::find_if(result.nodes.cbegin(), result.nodes.cend(),
          [&](const StatePlacementNode& node) { return node.id == stateId; });
      const auto group = std::find_if(result.clusters.cbegin(), result.clusters.cend(),
          [&](const StatePlacementCluster& cluster) { return cluster.id == noteGroup.id; });
      if (state == result.nodes.cend() || group == result.clusters.cend()) continue;
      const bool shouldBeRight = noteGroup.position == QLatin1String("right of");
      if ((shouldBeRight && group->center.x() >= state->center.x()) ||
          (!shouldBeRight && group->center.x() <= state->center.x())) continue;
      const qreal axis = state->center.x();
      const auto reflect = [axis](QPointF& point) { point.setX(2.0 * axis - point.x()); };
      for (StatePlacementNode& node : result.nodes) reflect(node.center);
      for (StatePlacementCluster& cluster : result.clusters) reflect(cluster.center);
      for (StatePlacementEdge& edge : result.edges) {
        for (QPointF& point : edge.points) reflect(point);
        for (QVector<QPointF>& segment : edge.segments)
          for (QPointF& point : segment) reflect(point);
        if (edge.labelPosition) reflect(*edge.labelPosition);
        edge.path.clear();
      }
      break;
    }
  }
  return result;
}

QJsonObject stateLayoutInputToJson(const StateLayoutInput& input) {
  QJsonArray nodes;
  for (const StateLayoutNodeInput& node : input.nodes) {
    QJsonObject object{{QStringLiteral("id"), node.id},
        {QStringLiteral("label"), node.label},
        {QStringLiteral("parentId"), node.parentId.isEmpty() ? QJsonValue::Null : QJsonValue(node.parentId)},
        {QStringLiteral("dir"), node.direction.isEmpty() ? QJsonValue::Null : QJsonValue(node.direction)},
        {QStringLiteral("explicitDir"), node.explicitDirection},
        {QStringLiteral("isGroup"), node.isGroup},
        {QStringLiteral("cssClasses"), node.cssClasses},
        {QStringLiteral("cssStyles"), strings(node.cssStyles)},
        {QStringLiteral("position"), node.position.isEmpty() ? QJsonValue::Null : QJsonValue(node.position)},
        {QStringLiteral("description"), node.description}};
    if (!node.shape.isEmpty()) object.insert(QStringLiteral("shape"), node.shape);
    nodes.append(object);
  }
  QJsonArray edges;
  for (const StateLayoutEdgeInput& edge : input.edges)
    edges.append(QJsonObject{{QStringLiteral("id"), edge.id},
        {QStringLiteral("start"), edge.start}, {QStringLiteral("end"), edge.end},
        {QStringLiteral("label"), edge.label}, {QStringLiteral("arrowhead"), edge.arrowhead},
        {QStringLiteral("arrowTypeEnd"), edge.arrowTypeEnd}, {QStringLiteral("style"), edge.style},
        {QStringLiteral("labelStyle"), edge.labelStyle},
        {QStringLiteral("thickness"), edge.thickness}, {QStringLiteral("classes"), edge.classes}});
  return {{QStringLiteral("direction"), input.direction},
          {QStringLiteral("nodes"), nodes}, {QStringLiteral("edges"), edges}};
}

}  // namespace muffin::mermaid::state
