#include "mermaid/classdiagram/ClassLayout.h"

#include "mermaid/flowchart/FlowLabel.h"
#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/flowchart/FlowchartLayout.h"
#include "mermaid/dagre/DagreLabels.h"
#include "mermaid/dagre/Layout.h"

#include <QSet>
#include <QTextCharFormat>

#include <algorithm>

namespace muffin::mermaid::classdiagram {
namespace {

QString classifierStyle(const ClassMember& member) {
  if (member.classifier == QLatin1String("*")) return QStringLiteral("font-style:italic;");
  if (member.classifier == QLatin1String("$")) return QStringLiteral("text-decoration:underline;");
  return {};
}

QString markerName(const QJsonValue& type) {
  if (!type.isDouble()) return QStringLiteral("none");
  switch (type.toInt()) {
    case 0: return QStringLiteral("aggregation");
    case 1: return QStringLiteral("extension");
    case 2: return QStringLiteral("composition");
    case 3: return QStringLiteral("dependency");
    case 4: return QStringLiteral("lollipop");
    default: return QStringLiteral("none");
  }
}

QString explicitAncestor(const QString& parentId,
                         const QVector<ClassNamespace>& namespaces) {
  QString current = parentId;
  while (!current.isEmpty()) {
    const auto found = std::find_if(namespaces.cbegin(), namespaces.cend(),
        [&](const ClassNamespace& value) { return value.id == current; });
    if (found == namespaces.cend()) return {};
    if (found->explicitDeclaration) return current;
    current = found->parent;
  }
  return {};
}

ClassLayoutMemberInput memberInput(const ClassMember& member) {
  return {member.text, classifierStyle(member)};
}

QRectF compartmentBounds(const QVector<ClassTextMeasurement>& items, qreal gap,
                         bool firstOnly = false) {
  QRectF bounds;
  bool initialized = false;
  qreal offset = 0.0;
  const qsizetype count = firstOnly ? std::min<qsizetype>(items.size(), 1)
                                    : items.size();
  for (qsizetype index = 0; index < count; ++index) {
    const ClassTextMeasurement& measured = items.at(index);
    const qsizetype lineCount = std::max<qsizetype>(1, measured.lineCount);
    const qreal labelOffset =
        -measured.bounds.height() / (2.0 * lineCount) + offset;
    const QRectF item = measured.bounds.translated(0.0, labelOffset);
    bounds = initialized ? bounds.united(item) : item;
    initialized = true;
    offset += measured.bounds.height() + gap;
  }
  return bounds;
}

void uniteTranslated(QRectF& bounds, bool& initialized,
                     const QRectF& local, const QPointF& translation) {
  if (local.isNull()) return;
  const QRectF translated = local.translated(translation);
  bounds = initialized ? bounds.united(translated) : translated;
  initialized = true;
}

ClassTextMeasurement measureClassText(
    const QString& source, const QString& cssStyle,
    const ClassLabelMeasureOptions& options, bool bold = false) {
  const bool svgText = !options.htmlLabels &&
      !source.contains(QStringLiteral("$$"));
  flowchart::FlowLabelDocument document = svgText
      ? flowchart::parseFlowSvgLabel(source, QStringLiteral("markdown"))
      : flowchart::parseFlowLabel(source, QStringLiteral("markdown"), true);
  if (!svgText)
    document.formattingContext =
        flowchart::FlowLabelFormattingContext::FlowForeignObjectFlex;
  if (bold || !cssStyle.isEmpty()) {
    QTextCharFormat format;
    if (bold) format.setFontWeight(QFont::Bold);
    if (cssStyle.contains(QStringLiteral("font-style:italic")))
      format.setFontItalic(true);
    if (cssStyle.contains(QStringLiteral("text-decoration:underline")))
      format.setFontUnderline(true);
    document.formats.append({0, static_cast<int>(document.text.size()), format});
  }
  const flowchart::FlowLabelLayoutMetrics layout = flowchart::layoutFlowLabel(
      document, options.fontFamily, options.fontPixelSize, options.lineHeight);
  ClassTextMeasurement measured;
  measured.lineCount = std::max<qsizetype>(1, layout.lines.size());
  measured.svgText = svgText;
  measured.bounds = svgText
      ? flowchart::measureFlowSvgTextBounds(
            document, options.fontFamily, options.fontPixelSize)
      : QRectF(QPointF(0.0, 0.0), layout.size);
  return measured;
}

}  // namespace

QString classBoxLabelMarkup(const QString& label, const QString& text) {
  QString markup = text.isEmpty() ? label : text;
  const qsizetype opening = markup.lastIndexOf(QLatin1Char('<'));
  if (opening < 0 || !markup.endsWith(QLatin1Char('>')) ||
      opening + 1 >= markup.size())
    return markup;

  const QChar first = markup.at(opening + 1);
  if (!((first >= QLatin1Char('A') && first <= QLatin1Char('Z')) ||
        (first >= QLatin1Char('a') && first <= QLatin1Char('z'))))
    return markup;
  for (qsizetype i = opening + 1; i + 1 < markup.size(); ++i) {
    const QChar ch = markup.at(i);
    if (ch.isSpace() || ch == QLatin1Char('<') || ch == QLatin1Char('/'))
      return markup;
  }
  markup.truncate(opening);
  return markup;
}

ClassLayoutInput buildClassLayoutInput(const ClassDiagramData& data,
                                       ClassLayoutOptions options) {
  ClassLayoutInput result;
  result.direction = data.direction;
  result.nodeSpacing = options.nodeSpacing;
  result.rankSpacing = options.rankSpacing;

  for (const ClassNamespace& value : data.namespaces) {
    if (!options.hierarchicalNamespaces && !value.explicitDeclaration) continue;
    ClassLayoutNodeInput node;
    node.id = value.id;
    node.label = options.hierarchicalNamespaces ? value.label : value.id;
    node.shape = QStringLiteral("rect");
    node.parentId = options.hierarchicalNamespaces ? value.parent : QString{};
    node.padding = options.padding;
    node.isGroup = true;
    node.look = options.look;
    result.nodes.append(std::move(node));
  }

  for (const ClassNode& value : data.classes) {
    ClassLayoutNodeInput node;
    node.id = value.id;
    node.label = value.label;
    node.text = value.text;
    node.shape = QStringLiteral("classBox");
    node.parentId = options.hierarchicalNamespaces
        ? value.parent : explicitAncestor(value.parent, data.namespaces);
    node.cssClasses = value.cssClasses;
    node.styles = value.styles;
    node.annotations = value.annotations;
    node.look = options.look;
    for (const ClassMember& member : value.members) node.members.append(memberInput(member));
    for (const ClassMember& method : value.methods) node.methods.append(memberInput(method));
    result.nodes.append(std::move(node));
  }

  for (const ClassNote& value : data.notes) {
    ClassLayoutNodeInput node;
    node.id = value.id;
    node.label = value.text;
    node.shape = QStringLiteral("note");
    node.parentId = options.hierarchicalNamespaces
        ? value.parent : explicitAncestor(value.parent, data.namespaces);
    node.cssStyles = {QStringLiteral("text-align: left"), QStringLiteral("white-space: nowrap"),
                      QStringLiteral("fill: #fff5ad"), QStringLiteral("stroke: #aaaa33")};
    node.padding = options.padding;
    node.look = options.look;
    result.nodes.append(std::move(node));
    if (!value.className.isEmpty()) {
      ClassLayoutEdgeInput edge;
      edge.id = QStringLiteral("edgeNote%1").arg(value.index);
      edge.start = value.id;
      edge.end = value.className;
      edge.pattern = QStringLiteral("dotted");
      edge.arrowTypeStart = QStringLiteral("none");
      edge.arrowTypeEnd = QStringLiteral("none");
      edge.style = {QStringLiteral("fill: none")};
      edge.labelStyle = {QString{}};
      edge.look = options.look;
      result.edges.append(std::move(edge));
    }
  }

  for (const ClassInterface& value : data.interfaces) {
    ClassLayoutNodeInput node;
    node.id = value.id;
    node.label = value.label;
    node.shape = QStringLiteral("rect");
    node.cssStyles = {QStringLiteral("opacity: 0;")};
    node.look = options.look;
    result.nodes.append(std::move(node));
  }

  int counter = 0;
  for (const ClassRelation& value : data.relations) {
    ClassLayoutEdgeInput edge;
    edge.id = QStringLiteral("id_%1_%2_%3").arg(value.id1, value.id2).arg(++counter);
    edge.start = value.id1;
    edge.end = value.id2;
    edge.label = value.title;
    edge.pattern = value.lineType == 1 ? QStringLiteral("dashed") : QStringLiteral("solid");
    edge.arrowTypeStart = markerName(value.type1);
    edge.arrowTypeEnd = markerName(value.type2);
    edge.startLabelRight = value.relationTitle1 == QLatin1String("none")
        ? QString{} : value.relationTitle1;
    edge.endLabelLeft = value.relationTitle2 == QLatin1String("none")
        ? QString{} : value.relationTitle2;
    edge.style = value.style;  // linkStyle declarations (parse-time)
    edge.look = options.look;
    result.edges.append(std::move(edge));
  }
  return result;
}

QVector<ClassBoxGeometry> layoutClassBoxes(
    const ClassLayoutInput& input, const ClassLayoutMeasurements& measurements,
    ClassLayoutOptions options) {
  QVector<ClassBoxGeometry> result;
  const qreal padding = options.padding;
  const qreal gap = options.htmlLabels ? 0.0 : 3.0;
  // classBox.ts applies this explicit correction after positioning every text
  // group. It is part of Mermaid's SVG-label contract, not a font heuristic.
  constexpr qreal kSvgTextGroupBaselineCorrection = -4.0;

  for (const ClassLayoutNodeInput& node : input.nodes) {
    if (node.shape != QLatin1String("classBox")) continue;
    const ClassNodeMeasurements measured = measurements.value(node.id);
    ClassBoxGeometry geometry;
    geometry.id = node.id;
    geometry.annotation.localBounds =
        compartmentBounds(measured.annotations, gap, true);
    geometry.label.localBounds = compartmentBounds(measured.labels, gap, true);
    geometry.members.localBounds = compartmentBounds(measured.members, gap);
    geometry.methods.localBounds = compartmentBounds(measured.methods, gap);

    const qreal annotationHeight = geometry.annotation.localBounds.height();
    const qreal labelHeight = geometry.label.localBounds.height();
    qreal memberHeight = geometry.members.localBounds.height();
    if (memberHeight <= 0.0) memberHeight = padding / 2.0;

    geometry.annotation.translation =
        QPointF(-geometry.annotation.localBounds.width() / 2.0, 0.0);
    geometry.label.translation =
        QPointF(-geometry.label.localBounds.width() / 2.0, annotationHeight);
    geometry.members.translation =
        QPointF(0.0, annotationHeight + labelHeight + padding * 2.0);
    geometry.methods.translation = QPointF(
        0.0, annotationHeight + labelHeight +
                 (memberHeight > 0.0 ? memberHeight + padding * 4.0
                                     : padding * 2.0));
    const qreal annotationInitialY = geometry.annotation.translation.y();
    const qreal labelInitialY = geometry.label.translation.y();
    const qreal membersInitialY = geometry.members.translation.y();
    const qreal methodsInitialY = geometry.methods.translation.y();

    QRectF textBounds;
    bool hasTextBounds = false;
    uniteTranslated(textBounds, hasTextBounds, geometry.annotation.localBounds,
                    geometry.annotation.translation);
    uniteTranslated(textBounds, hasTextBounds, geometry.label.localBounds,
                    geometry.label.translation);
    uniteTranslated(textBounds, hasTextBounds, geometry.members.localBounds,
                    geometry.members.translation);
    uniteTranslated(textBounds, hasTextBounds, geometry.methods.localBounds,
                    geometry.methods.translation);

    const bool noMembers = node.members.isEmpty();
    const bool noMethods = node.methods.isEmpty();
    const bool drawEmptyCompartments =
        noMembers && noMethods && !options.hideEmptyMembersBox;
    const qreal textWidth = hasTextBounds ? textBounds.width() : 0.0;
    qreal textHeight = hasTextBounds ? textBounds.height() : 0.0;
    if (noMembers && noMethods)
      textHeight += padding;
    else if (!noMembers && noMethods)
      textHeight += padding * 2.0;

    const qreal left = -textWidth / 2.0;
    const qreal top = -textHeight / 2.0;
    qreal extraHeight = drawEmptyCompartments ? padding * 2.0
        : noMembers && noMethods ? -padding : 0.0;
    const qreal outerTop = top - padding -
        (drawEmptyCompartments ? padding
         : noMembers && noMethods ? -padding / 2.0 : 0.0);
    geometry.outerRect = QRectF(left - padding, outerTop,
                               textWidth + padding * 2.0,
                               textHeight + padding * 2.0 + extraHeight);
    geometry.bounds = geometry.outerRect;

    qreal annotationMetric = geometry.annotation.localBounds.height() -
        (drawEmptyCompartments ? padding / 2.0 : 0.0);
    qreal labelMetric = geometry.label.localBounds.height() -
        (drawEmptyCompartments ? padding / 2.0 : 0.0);
    qreal memberMetric = geometry.members.localBounds.height() -
        (drawEmptyCompartments ? padding / 2.0 : 0.0);
    const qreal commonOffset = top + padding -
        (drawEmptyCompartments ? padding
         : noMembers && noMethods ? -padding / 2.0 : 0.0);
    geometry.annotation.translation.setY(commonOffset);
    geometry.label.translation.setY(annotationHeight + commonOffset);
    geometry.members.translation = QPointF(left,
        annotationHeight + labelHeight + padding * 2.0 + commonOffset);

    const qreal memberBlock = std::max(memberMetric, padding / 2.0);
    geometry.methods.translation = QPointF(left,
        annotationMetric + labelMetric + memberBlock + top +
        padding * 5.0);
    if (noMembers && noMethods && options.hideEmptyMembersBox) {
      const qreal annotationOffset = node.annotations.isEmpty() ? 0.0 : -padding;
      geometry.annotation.translation.setY(annotationInitialY + annotationOffset);
      geometry.label.translation.setY(labelInitialY + annotationOffset);
      geometry.members.translation.setY(membersInitialY + annotationOffset);
      geometry.methods.translation.setY(methodsInitialY + annotationOffset);
    }
    if (!options.htmlLabels) {
      geometry.annotation.translation.ry() += kSvgTextGroupBaselineCorrection;
      geometry.label.translation.ry() += kSvgTextGroupBaselineCorrection;
      geometry.members.translation.ry() += kSvgTextGroupBaselineCorrection;
      geometry.methods.translation.ry() += kSvgTextGroupBaselineCorrection;
    }

    if (!noMembers || !noMethods || drawEmptyCompartments) {
      const qreal y = annotationMetric + labelMetric + top + padding;
      geometry.dividers.append(
          QRectF(geometry.outerRect.x(), y, geometry.outerRect.width(), 0.001));
    }
    if (drawEmptyCompartments || !noMembers || !noMethods) {
      const qreal y = annotationMetric + labelMetric + memberMetric +
                      top + padding * 3.0;
      geometry.dividers.append(
          QRectF(geometry.outerRect.x(), y, geometry.outerRect.width(), 0.001));
    }
    result.append(std::move(geometry));
  }
  return result;
}

ClassLayoutMeasurements measureClassLayoutLabels(
    const ClassLayoutInput& input, ClassLabelMeasureOptions options) {
  ClassLayoutMeasurements result;
  for (const ClassLayoutNodeInput& node : input.nodes) {
    if (node.shape != QLatin1String("classBox")) continue;
    ClassNodeMeasurements measured;
    measured.textPadding = options.htmlLabels ? 0.0 : 3.0;
    if (!node.annotations.isEmpty()) {
      measured.annotations.append(measureClassText(
          QString(QChar(0x00ab)) + node.annotations.first() + QChar(0x00bb),
          {}, options));
    }
    measured.labels.append(measureClassText(
        classBoxLabelMarkup(node.label, node.text), {}, options, true));
    for (const ClassLayoutMemberInput& member : node.members)
      measured.members.append(measureClassText(member.text, member.cssStyle, options));
    for (const ClassLayoutMemberInput& method : node.methods)
      measured.methods.append(measureClassText(method.text, method.cssStyle, options));
    result.insert(node.id, std::move(measured));
  }
  return result;
}

ClassDagreMeasurements measureClassDagreInput(
    const ClassLayoutInput& input, const QVector<ClassBoxGeometry>& boxes,
    ClassLabelMeasureOptions options) {
  ClassDagreMeasurements result;
  QHash<QString, QSizeF> classBoxes;
  for (const ClassBoxGeometry& box : boxes)
    classBoxes.insert(box.id, box.bounds.size());
  for (const ClassLayoutNodeInput& node : input.nodes) {
    if (node.isGroup) continue;
    if (node.shape == QLatin1String("classBox")) {
      result.nodes.insert(node.id, classBoxes.value(node.id));
      continue;
    }
    const QSizeF label = measureClassText(node.label, {}, options).bounds.size();
    const qreal padding = node.padding.value_or(0.0);
    result.nodes.insert(node.id,
        QSizeF(label.width() + padding * 2.0,
               label.height() + padding * 2.0));
  }
  for (const ClassLayoutEdgeInput& edge : input.edges) {
    if (edge.label.isEmpty()) {
      result.edgeLabels.insert(edge.id, QSizeF(0.0, 0.0));
      continue;
    }
    result.edgeLabels.insert(
        edge.id, measureClassText(edge.label, {}, options).bounds.size());
  }
  return result;
}

ClassPlacementResult layoutClassDiagramDagre(
    const ClassLayoutInput& input, const ClassDagreMeasurements& measurements) {
  constexpr qreal kDagreMarginX = 8.0;
  const bool compound = std::any_of(input.nodes.cbegin(), input.nodes.cend(),
      [](const ClassLayoutNodeInput& node) { return node.isGroup; });
  const bool hasSelfEdge = std::any_of(input.edges.cbegin(), input.edges.cend(),
      [](const ClassLayoutEdgeInput& edge) { return edge.start == edge.end; });
  if (compound || hasSelfEdge) {
    flowchart::FlowchartData projected;
    projected.direction = input.direction;
    for (const ClassLayoutNodeInput& node : input.nodes) {
      if (node.isGroup) {
        flowchart::FlowSubgraph group;
        group.id = node.id;
        group.title = node.label;
        for (const ClassLayoutNodeInput& child : input.nodes)
          if (child.parentId == node.id) group.nodes.append(child.id);
        // Generic Mermaid data inserts groups outer-first. FlowchartData's
        // adapter consumes subgraphs in reverse DB order, so preserve the
        // generic renderer's observable extraction order by projecting them
        // inner-first here.
        projected.subgraphs.prepend(std::move(group));
      } else {
        flowchart::FlowVertex vertex;
        vertex.id = node.id;
        vertex.text = node.label;
        vertex.type = QStringLiteral("rect");
        projected.vertices.append(std::move(vertex));
      }
    }
    for (const ClassLayoutEdgeInput& edge : input.edges) {
      flowchart::FlowEdge projectedEdge;
      projectedEdge.id = edge.id;
      projectedEdge.start = edge.start;
      projectedEdge.end = edge.end;
      projectedEdge.text = edge.label;
      projected.edges.append(std::move(projectedEdge));
    }
    flowchart::FlowLayoutOptions options;
    options.nodeSpacing = input.nodeSpacing;
    options.rankSpacing = input.rankSpacing;
    options.measuredEdgeLabels = measurements.edgeLabels;
    const flowchart::FlowLayoutResult placed =
        flowchart::layoutFlowchartNodesDagre(projected, measurements.nodes, options);
    ClassPlacementResult result;
    for (const flowchart::FlowLayoutNode& node : placed.nodes)
      result.nodes.append({node.id, node.x, node.y, node.width, node.height, node.rank});
    for (const flowchart::FlowLayoutEdge& edge : placed.edges) {
      ClassPlacementEdge projectedEdge;
      projectedEdge.id = edge.id;
      projectedEdge.points = edge.points;
      projectedEdge.segments = edge.segments;
      if (edge.hasLabelPosition)
        projectedEdge.labelPosition = QPointF(edge.labelX, edge.labelY);
      result.edges.append(std::move(projectedEdge));
    }
    for (const flowchart::FlowLayoutCluster& cluster : placed.clusters)
      result.clusters.append({cluster.id, cluster.x, cluster.y,
                              cluster.width, cluster.height});
    QPointF origin;
    const auto firstSemantic = std::find_if(input.nodes.cbegin(), input.nodes.cend(),
        [](const ClassLayoutNodeInput& node) { return !node.isGroup; });
    if (firstSemantic != input.nodes.cend()) {
      const auto firstPlaced = std::find_if(result.nodes.cbegin(), result.nodes.cend(),
          [&](const ClassPlacementNode& node) { return node.id == firstSemantic->id; });
      if (firstPlaced != result.nodes.cend()) origin = QPointF(firstPlaced->x, firstPlaced->y);
    }
    for (ClassPlacementNode& node : result.nodes) {
      node.x -= origin.x();
      node.y -= origin.y();
    }
    for (ClassPlacementCluster& cluster : result.clusters) {
      cluster.x -= origin.x();
      cluster.y -= origin.y();
    }
    for (ClassPlacementEdge& edge : result.edges) {
      for (QPointF& point : edge.points) point -= origin;
      for (QVector<QPointF>& segment : edge.segments)
        for (QPointF& point : segment) point -= origin;
      if (edge.labelPosition) *edge.labelPosition -= origin;
    }

    // recursiveRender() stores only getBBox().width on an extracted cluster,
    // while its child root starts at Dagre's marginx. positionNode() centers
    // that width, leaving half the horizontal margin outside the logical atom
    // on vertical layouts. A single-child namespace wrapper is collapsed by
    // clusterDb; only nested clusters with a sibling get this CTM shift.
    QSet<QString> shiftedClusters;
    const QString direction = input.direction.toUpper();
    if (direction == QLatin1String("TB") || direction == QLatin1String("BT")) {
      for (const ClassLayoutNodeInput& group : input.nodes) {
        if (!group.isGroup || group.parentId.isEmpty()) continue;
        const bool hasSibling = std::any_of(input.nodes.cbegin(), input.nodes.cend(),
            [&](const ClassLayoutNodeInput& candidate) {
              return candidate.id != group.id && candidate.parentId == group.parentId;
            });
        if (hasSibling) shiftedClusters.insert(group.id);
      }
    }
    const auto horizontalShift = [&](QString id) {
      qreal shift = 0.0;
      QSet<QString> seen;
      while (!id.isEmpty() && !seen.contains(id)) {
        seen.insert(id);
        const auto node = std::find_if(input.nodes.cbegin(), input.nodes.cend(),
            [&](const ClassLayoutNodeInput& candidate) { return candidate.id == id; });
        if (node == input.nodes.cend()) break;
        if (shiftedClusters.contains(node->parentId))
          shift -= kDagreMarginX / 2.0;
        id = node->parentId;
      }
      return shift;
    };
    for (ClassPlacementNode& node : result.nodes) node.x += horizontalShift(node.id);
    for (ClassPlacementCluster& cluster : result.clusters) {
      if (shiftedClusters.contains(cluster.id))
        cluster.x -= kDagreMarginX / 2.0;
      const auto semantic = std::find_if(input.nodes.cbegin(), input.nodes.cend(),
          [&](const ClassLayoutNodeInput& node) { return node.id == cluster.id; });
      if (semantic == input.nodes.cend()) continue;
      QString nestedId = cluster.id;
      while (!nestedId.isEmpty()) {
        QVector<const ClassLayoutNodeInput*> children;
        for (const ClassLayoutNodeInput& child : input.nodes)
          if (child.parentId == nestedId) children.append(&child);
        if (children.size() != 1 || !children.first()->isGroup) break;
        cluster.x += kDagreMarginX / 2.0;
        nestedId = children.first()->id;
      }
    }
    for (ClassPlacementEdge& edge : result.edges) {
      const auto semantic = std::find_if(input.edges.cbegin(), input.edges.cend(),
          [&](const ClassLayoutEdgeInput& candidate) { return candidate.id == edge.id; });
      if (semantic == input.edges.cend()) continue;
      const qreal startShift = horizontalShift(semantic->start);
      const qreal endShift = horizontalShift(semantic->end);
      if (!qFuzzyCompare(startShift + 1.0, endShift + 1.0)) continue;
      const QPointF correction(startShift, 0.0);
      for (QPointF& point : edge.points) point += correction;
      for (QVector<QPointF>& segment : edge.segments)
        for (QPointF& point : segment) point += correction;
      if (edge.labelPosition) *edge.labelPosition += correction;
    }
    return result;
  }

  namespace d = muffin::mermaid::dagre;
  d::DagreGraph graph({.directed = true, .multigraph = true, .compound = true});
  d::DagreGraphLabel graphLabel;
  graphLabel.rankdir = input.direction;
  graphLabel.nodesep = input.nodeSpacing;
  graphLabel.edgesep = 20.0;
  graphLabel.ranksep = input.rankSpacing;
  graphLabel.marginx = kDagreMarginX;
  graphLabel.marginy = 8.0;
  graph.setGraph(graphLabel);

  for (const ClassLayoutNodeInput& node : input.nodes) {
    const QSizeF size = measurements.nodes.value(node.id);
    d::DagreNodeLabel label;
    label.width = size.width();
    label.height = size.height();
    graph.setNode(node.id, label);
  }
  for (const ClassLayoutNodeInput& node : input.nodes) {
    if (!node.parentId.isEmpty()) graph.setParent(node.id, node.parentId);
  }
  for (const ClassLayoutEdgeInput& edge : input.edges) {
    const QSizeF labelSize = measurements.edgeLabels.value(edge.id);
    d::DagreEdgeLabel label;
    label.minlen = 1;
    label.weight = 1;
    label.width = labelSize.width();
    label.height = labelSize.height();
    label.labelpos = QStringLiteral("c");
    label.labeloffset = 10;
    graph.setEdge(edge.start, edge.end, label, edge.id);
  }

  d::runDagreLayout(graph);
  QPointF origin;
  for (const ClassLayoutNodeInput& node : input.nodes) {
    if (node.isGroup) continue;
    if (const d::DagreNodeLabel* value = graph.node(node.id))
      origin = QPointF(value->x.value_or(0.0), value->y.value_or(0.0));
    break;
  }

  ClassPlacementResult result;
  for (const ClassLayoutNodeInput& node : input.nodes) {
    if (node.isGroup) continue;
    const d::DagreNodeLabel* value = graph.node(node.id);
    if (!value) continue;
    result.nodes.append({node.id, value->x.value_or(0.0) - origin.x(),
                         value->y.value_or(0.0) - origin.y(), value->width,
                         value->height, value->rank.value_or(0)});
  }
  for (const ClassLayoutEdgeInput& edge : input.edges) {
    const d::DagreEdgeLabel* value = graph.edge(edge.start, edge.end, edge.id);
    if (!value) continue;
    ClassPlacementEdge placed;
    placed.id = edge.id;
    placed.points.reserve(value->points.size());
    for (const QPointF& point : value->points) placed.points.append(point - origin);
    if (value->x && value->y)
      placed.labelPosition = QPointF(*value->x - origin.x(), *value->y - origin.y());
    result.edges.append(std::move(placed));
  }
  return result;
}

}  // namespace muffin::mermaid::classdiagram
