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
    if (it == result_.nodes.end()) {
      result_.nodes.append(std::move(node));
    } else {
      // Upstream uses Object.assign(existingNodeData, nodeData). A root-level
      // reference has no parentId property, so it cannot detach a state that
      // was already parented while traversing a composite document.
      if (node.parentId.isEmpty()) node.parentId = it->parentId;
      *it = std::move(node);
    }
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
        // Upstream keeps NodeData.description as an ARRAY (empty when the
        // description had a single row — the row IS the label).
        QJsonArray descriptions;
        for (qsizetype i = 1; i < labels.size(); ++i) descriptions.append(labels.at(i));
        node.description = descriptions;
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
    bool handDrawn, quint32 handDrawnSeed, bool shapeHidden,
    const QHash<QString, StateMeasureCss>* nodeCss,
    const QHash<QString, StateMeasureCss>* edgeLabelCss) {
  StateLayoutMeasurements result;
  auto optionsFor = [&](const QString& family, qreal size) {
    flowchart::FlowTextOptions options;
    options.fontFamily = family.isEmpty() ? fontFamily : family;
    options.fontPixelSize = size > 0.0 ? size : fontSize;
    options.lineHeight = options.fontPixelSize * 1.5;
    options.horizontalPadding = 16.0;
    options.verticalPadding = 16.0;
    // State labels are foreignObject inline boxes. Use Blink's HarfBuzz /
    // LayoutUnit contract before RoughJS and Dagre consume their dimensions.
    options.chromiumInlineWidth = true;
    return options;
  };
  const flowchart::FlowTextOptions defaultOptions = optionsFor(QString(), 0.0);
  for (const StateLayoutNodeInput& node : input.nodes) {
    if (node.isGroup) {
      if (node.shape != QLatin1String("noteGroup")) {
        // Cluster titles measure with their cluster-label <p>'s computed
        // font (themeCSS `.cluster-label {font-size:31px}` grows the box);
        // display:none on the p collapses the title band to nothing.
        const StateMeasureCss* clusterCss = nullptr;
        if (nodeCss) {
          const auto it = nodeCss->constFind(node.id);
          if (it != nodeCss->constEnd() &&
              (it->fontSize > 0.0 || !it->fontFamily.isEmpty() || it->labelHidden))
            clusterCss = &it.value();
        }
        result.clusterLabels.insert(
            node.id, clusterCss && clusterCss->labelHidden
                ? QSizeF(0.0, 0.0)
                : flowchart::measureLabel(node.label.toString(),
                                          QStringLiteral("markdown"),
                                          clusterCss
                                              ? optionsFor(clusterCss->fontFamily,
                                                           clusterCss->fontSize)
                                              : defaultOptions));
      }
      continue;
    }
    const StateMeasureCss* css = nullptr;
    if (nodeCss) {
      const auto it = nodeCss->constFind(node.id);
      if (it != nodeCss->constEnd() &&
          (it->fontSize > 0.0 || !it->fontFamily.isEmpty() || it->labelHidden ||
           it->descFontSize > 0.0 || !it->descFontFamily.isEmpty() ||
           it->descHidden))
        css = &it.value();
    }
    const flowchart::FlowTextOptions options =
        css ? optionsFor(css->fontFamily, css->fontSize) : defaultOptions;
    const QString shape = node.shape.isEmpty() ? QStringLiteral("rect") : node.shape;
    // `.node rect { display:none }` collapses the drawState temp group for
    // rect-bearing shapes (rect/rectWithTitle measure 0x0). Note nodes keep
    // their size: the note shape measures label+padding via labelHelper, not
    // the rect-only temp group (browser-verified: the hidden case's note
    // still reserves its full box).
    const bool hidden = shape != QLatin1String("note") &&
        (shapeHidden || (nodeCss && nodeCss->value(node.id).shapeHidden));
    QSizeF size;
    QSizeF paintedSize;
    if (shape == QLatin1String("stateStart") || shape == QLatin1String("stateEnd"))
      size = paintedSize = QSizeF(14.0, 14.0);
    else if (shape == QLatin1String("fork") || shape == QLatin1String("join")) {
      // The dagre box carries 2px of padding on every side of the painted
      // 70x10 bar (browser-verified: single-column fork layouts center at
      // margin + 37, putting the painted left edge at 10 — the viewBox
      // origin's +2 in the pseudostates fixture).
      size = QSizeF(74.0, 14.0);
      paintedSize = QSizeF(70.0, 10.0);
    } else if (shape == QLatin1String("choice"))
      size = paintedSize = QSizeF(28.0, 28.0);
    if (hidden && shape != QLatin1String("note") &&
        shape != QLatin1String("rect") && shape != QLatin1String("rectWithTitle")) {
      // display:none on a pseudostate's circle/paths also collapses the
      // drawState temp group to 0x0 — dagre gets a point node while the
      // label (if any) keeps painting at the centre.
      paintedSize = size;
      size = QSizeF(0.0, 0.0);
    } else if (shape != QLatin1String("stateStart") &&
               shape != QLatin1String("stateEnd") &&
               shape != QLatin1String("fork") &&
               shape != QLatin1String("join") &&
               shape != QLatin1String("choice")) {
      QStringList lines;
      if (node.label.isString()) lines.append(node.label.toString());
      QStringList descLines;
      if (node.description.isArray())
        for (const QJsonValue& value : node.description.toArray())
          descLines.append(value.toString());
      lines += descLines;
      // display:none on the label <p> collapses the fo content: the label
      // box measures 0x0 (an EMPTY measureLabel still carries one line box —
      // height 1.5em), and the node keeps only its shape padding (a plain
      // rect becomes 16x16 — browser-verified), while the text (if any)
      // still paints at the centre.
      if (css && css->labelHidden) {
        size = QSizeF(0.0, 0.0);
      } else if (shape == QLatin1String("rectWithTitle")) {
        // rectWithTitle measures its TITLE and its DESCRIPTION ROWS
        // separately (two foreignObjects, each with its own <p>): the rows
        // use the desc p's computed font — width = max(title, rows), height
        // = title + rows (identical to the joined measure when the fonts
        // match). A display:none desc p (or no description statements at
        // all) collapses the second fo to 0x0, and a 0x0 foreignObject is
        // EXCLUDED from label.getBBox(): the box is the title alone and the
        // 9px title-rows gap vanishes (vertical add-on 17 -> 8 below).
        const bool descCollapsed =
            descLines.isEmpty() || (css && css->descHidden);
        const flowchart::FlowTextOptions descOptions =
            css && (css->descFontSize > 0.0 || !css->descFontFamily.isEmpty())
            ? optionsFor(css->descFontFamily, css->descFontSize) : options;
        const QSizeF title = flowchart::measureLabel(
            node.label.toString(), QStringLiteral("markdown"), options);
        if (descCollapsed) {
          size = title;
        } else {
          const QSizeF rows = flowchart::measureLabel(
              descLines.join(QLatin1Char('\n')), QStringLiteral("markdown"),
              descOptions);
          size = QSizeF(std::max(title.width(), rows.width()),
                        title.height() + rows.height());
        }
      } else {
        size = flowchart::measureLabel(lines.join(QLatin1Char('\n')), QStringLiteral("markdown"), options);
      }
      // drawState measures a temporary group that contains only the shape
      // (labels are inserted later at the dagre node centre), so the dagre
      // node size = rect box = label + 2*padding. `.node rect { display:none }`
      // removes that box: dagre gets a 0x0 node (rank centres collapse to the
      // bare ranksep) while the label still paints at the centre (probed vs
      // 11.16.0: nodes 24 tall, centre gap 50, painted gap 26).
      if (hidden) {
        paintedSize = size;
        size = QSizeF(0.0, 0.0);
      } else {
        // rectWithTitle vertical add-on: rows render -> 9px gap after the
        // title (halfPadding + 5 upstream) + 8px padding; rows collapsed ->
        // padding only (browser: 65 -> 32 tall with the same title).
        if (shape == QLatin1String("rectWithTitle"))
          size += QSizeF(8.0, descLines.isEmpty() || (css && css->descHidden)
                                  ? 8.0 : 17.0);
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
    const StateMeasureCss* css = nullptr;
    if (edgeLabelCss) {
      const auto it = edgeLabelCss->constFind(edge.id);
      if (it != edgeLabelCss->constEnd()) css = &it.value();
    }
    // display:none on the label <p> collapses the whole label box: the edge
    // reserves NO space (paths and viewBox shrink — the browser drops the fo
    // content from getBBox), not just the paint.
    result.edgeLabels.insert(edge.id, css && css->labelHidden
        ? QSizeF(0.0, 0.0)
        : flowchart::measureLabel(
              projected.text, QStringLiteral("markdown"),
              css ? optionsFor(css->fontFamily, css->fontSize) : defaultOptions));
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
      group.hasExplicitDir = node.explicitDirection;
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
    // arrow_barb_neo (the neo look) shortens the rendered path by 5.5px so
    // the -margin marker keeps its stroke gap (upstream markerOffsets).
    projectedEdge.type = edge.arrowTypeEnd;
    projected.edges.append(std::move(projectedEdge));
  }
  flowchart::FlowLayoutOptions options;
  options.nodeSpacing = nodeSpacing;
  options.rankSpacing = rankSpacing;
  options.nodePadding = 8.0;
  // The shared dagre-wrapper render() hardcodes marginx/marginy = 8 for
  // EVERY family (dagre-VKFMJZFB buildGraph) — translateGraph then places
  // the content bbox at (8, 8), which is what the un-translated
  // setupViewPortForSVG viewBox origin observes (node left edges sit at
  // exactly 8, browser-verified). The absolute coordinates must survive
  // extraction (no first-vertex re-centering) so the scene bounds carry
  // the browser's raw origin.
  options.diagramPadding = 8.0;
  options.preserveDagreCoordinates = true;
  options.measuredEdgeLabels = measurements.edgeLabels;
  for (const StateLayoutNodeInput& node : input.nodes)
    options.nodeInsertionOrder.append(node.id);
  // Composite width mirrors roundedWithTitle's render-time max
  // (`node.width <= bbox.width + node.padding ? bbox.width + padding : ...`)
  // — the cluster title (css font included) can outgrow the dagre box.
  options.measuredClusterLabels = measurements.clusterLabels;
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
