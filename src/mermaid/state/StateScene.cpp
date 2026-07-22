#include "mermaid/state/StateScene.h"

#include <algorithm>
#include <utility>

namespace muffin::mermaid::state {
namespace {
flowchart::FlowLabelDocument prepareLabel(const QString& text, qreal fontSize) {
  auto document = flowchart::parseFlowLabel(text, QStringLiteral("markdown"), true);
  document.formattingContext =
      flowchart::FlowLabelFormattingContext::FlowForeignObjectFlex;
  flowchart::prepareFlowLabelMath(document, fontSize);
  return document;
}
QStringList descriptions(const QJsonValue& value) {
  QStringList result;
  if (value.isArray())
    for (const QJsonValue& item : value.toArray()) result.append(item.toString());
  return result;
}
const StateLayoutNodeInput* inputNode(const StateLayoutInput& input, const QString& id) {
  const auto it = std::find_if(input.nodes.cbegin(), input.nodes.cend(),
      [&](const StateLayoutNodeInput& node) { return node.id == id; });
  return it == input.nodes.cend() ? nullptr : &*it;
}
void include(QRectF& bounds, bool& initialized, const QRectF& value) {
  if (!value.isValid()) return;
  if (!initialized) { bounds = value; initialized = true; }
  else bounds = bounds.united(value);
}
}

StateScene buildStateScene(const StateLayoutInput& input,
                           const StatePlacementResult& placement,
                           StateSceneStyle style) {
  StateScene scene;
  scene.style = std::move(style);
  bool initialized = false;
  for (const StatePlacementCluster& placed : placement.clusters) {
    const StateLayoutNodeInput* source = inputNode(input, placed.id);
    if (!source) continue;
    StateSceneNode node;
    node.id = source->id;
    node.shape = source->shape;
    node.label = source->label.toString();
    node.descriptions = descriptions(source->description);
    node.cssClasses = source->cssClasses;
    node.bounds = QRectF(placed.center - QPointF(placed.size.width() / 2.0,
                                                 placed.size.height() / 2.0), placed.size);
    node.group = true;
    node.labelDocument = prepareLabel(node.label, scene.style.fontSize);
    scene.clusters.append(node);
    include(scene.bounds, initialized, node.bounds);
  }
  for (const StatePlacementNode& placed : placement.nodes) {
    const StateLayoutNodeInput* source = inputNode(input, placed.id);
    if (!source) continue;
    StateSceneNode node;
    node.id = source->id;
    node.shape = source->shape.isEmpty() ? QStringLiteral("rect") : source->shape;
    node.label = source->label.toString();
    node.descriptions = descriptions(source->description);
    node.cssClasses = source->cssClasses;
    node.labelDocument = prepareLabel(node.label, scene.style.fontSize);
    for (const QString& description : node.descriptions)
      node.descriptionDocuments.append(prepareLabel(description, scene.style.fontSize));
    node.bounds = QRectF(placed.center - QPointF(placed.paintedSize.width() / 2.0,
                                                 placed.paintedSize.height() / 2.0),
                         placed.paintedSize);
    scene.nodes.append(node);
    include(scene.bounds, initialized, node.bounds);
  }
  for (const StatePlacementEdge& placed : placement.edges) {
    const auto it = std::find_if(input.edges.cbegin(), input.edges.cend(),
        [&](const StateLayoutEdgeInput& edge) { return edge.id == placed.id; });
    if (it == input.edges.cend()) continue;
    StateSceneEdge edge;
    edge.id = it->id; edge.start = it->start; edge.end = it->end;
    edge.label = it->label.toString(); edge.markerEnd = it->arrowTypeEnd;
    edge.classes = it->classes; edge.points = placed.points;
    edge.segments = placed.segments; edge.labelPosition = placed.labelPosition;
    edge.path = placed.path;
    edge.labelDocument = prepareLabel(edge.label, scene.style.fontSize);
    scene.edges.append(std::move(edge));
    for (const QPointF& point : placed.points)
      include(scene.bounds, initialized, QRectF(point - QPointF(0.5, 0.5), QSizeF(1.0, 1.0)));
  }
  if (initialized) scene.bounds.adjust(-8.0, -8.0, 8.0, 8.0);
  return scene;
}

}  // namespace muffin::mermaid::state
