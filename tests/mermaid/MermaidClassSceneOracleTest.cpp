#include "mermaid/classdiagram/ClassDiagram.h"
#include "mermaid/classdiagram/ClassLayout.h"
#include "mermaid/classdiagram/ClassScene.h"
#include "mermaid/classdiagram/ClassScenePainter.h"

#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace muffin::mermaid::classdiagram;

namespace {
[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}
void require(bool condition, const QString& message) { if (!condition) fail(message); }
void near(qreal actual, qreal expected, const QString& context) {
  if (std::abs(actual - expected) > 0.01)
    fail(QStringLiteral("%1: native %2, upstream %3")
             .arg(context).arg(actual, 0, 'f', 4).arg(expected, 0, 'f', 4));
}
void nearLabelMetric(qreal actual, qreal expected, const QString& context) {
  if (std::abs(actual - expected) > 0.2)
    fail(QStringLiteral("%1: native %2, upstream %3")
             .arg(context).arg(actual, 0, 'f', 4).arg(expected, 0, 'f', 4));
}
QSizeF size(const QJsonObject& value) {
  return {value.value(QStringLiteral("width")).toDouble(),
          value.value(QStringLiteral("height")).toDouble()};
}
QVector<QSizeF> sizes(const QJsonArray& values) {
  QVector<QSizeF> result;
  for (const QJsonValue& value : values) result.append(size(value.toObject()));
  return result;
}
qreal number(const QString& value) {
  if (value.isEmpty()) return 0.0;
  bool ok = false;
  const qreal result = value.toDouble(&ok);
  require(ok, QStringLiteral("Invalid numeric marker attribute '%1'").arg(value));
  return result;
}
QVector<QJsonValue> pathTokens(const QString& path) {
  static const QRegularExpression token(
      QStringLiteral("[A-Za-z]|[-+]?(?:\\d*\\.\\d+|\\d+\\.?)(?:[eE][-+]?\\d+)?"));
  QVector<QJsonValue> result;
  auto matches = token.globalMatch(path);
  while (matches.hasNext()) {
    const QString value = matches.next().captured();
    if (value.front().isLetter()) result.append(value);
    else result.append(value.toDouble());
  }
  return result;
}
void comparePath(const QString& actual, const QJsonArray& expected,
                 const QString& context) {
  const QVector<QJsonValue> tokens = pathTokens(actual);
  require(tokens.size() == expected.size(),
          QStringLiteral("%1 token count: native %2, upstream %3\n%4")
              .arg(context).arg(tokens.size()).arg(expected.size()).arg(actual));
  for (qsizetype i = 0; i < tokens.size(); ++i) {
    if (expected.at(i).isString())
      require(tokens.at(i).toString() == expected.at(i).toString(),
              QStringLiteral("%1 command %2 mismatch").arg(context).arg(i));
    else
      if (std::abs(tokens.at(i).toDouble() - expected.at(i).toDouble()) > 0.01)
        fail(QStringLiteral("%1[%2]: native %3, upstream %4")
                 .arg(context).arg(i)
                 .arg(tokens.at(i).toDouble(), 0, 'f', 4)
                 .arg(expected.at(i).toDouble(), 0, 'f', 4));
  }
}

ClassLayoutMeasurements browserMeasurements(const QJsonArray& nodes) {
  ClassLayoutMeasurements result;
  for (const QJsonValue& value : nodes) {
    const QJsonObject node = value.toObject();
    const QJsonObject measured = node.value(QStringLiteral("measured")).toObject();
    ClassNodeMeasurements item;
    item.annotations = sizes(measured.value(QStringLiteral("annotation")).toArray());
    item.labels = sizes(measured.value(QStringLiteral("label")).toArray());
    item.members = sizes(measured.value(QStringLiteral("members")).toArray());
    item.methods = sizes(measured.value(QStringLiteral("methods")).toArray());
    result.insert(node.value(QStringLiteral("id")).toString(), std::move(item));
  }
  return result;
}

ClassDagreMeasurements dagreMeasurements(const ClassLayoutInput& input,
                                         const QJsonObject& placement) {
  ClassDagreMeasurements result;
  for (const QJsonValue& value : placement.value(QStringLiteral("nodes")).toArray()) {
    const QJsonObject node = value.toObject();
    result.nodes.insert(node.value(QStringLiteral("id")).toString(), size(node));
  }
  const QJsonArray labels = placement.value(QStringLiteral("edgeLabels")).toArray();
  require(labels.size() == input.edges.size(), QStringLiteral("Edge label fixture order drifted"));
  for (qsizetype i = 0; i < labels.size(); ++i)
    result.edgeLabels.insert(input.edges.at(i).id,
        size(labels.at(i).toObject().value(QStringLiteral("bbox")).toObject()));
  return result;
}

const ClassSceneEdge& sceneEdge(const ClassScene& scene, const QString& id) {
  const auto found = std::find_if(scene.edges.cbegin(), scene.edges.cend(),
      [&](const auto& edge) { return edge.id == id; });
  require(found != scene.edges.cend(), QStringLiteral("Missing class scene edge %1").arg(id));
  return *found;
}

QPointF translation(const QString& value, const QString& context) {
  static const QRegularExpression pattern(QStringLiteral(
      R"(^translate\(([-+0-9.eE]+)(?:,\s*|\s+)([-+0-9.eE]+)\)$)"));
  const auto match = pattern.match(value);
  require(match.hasMatch(), context + QStringLiteral(": invalid transform ") + value);
  return {match.captured(1).toDouble(), match.captured(2).toDouble()};
}

void compareLabelGroup(const QVector<ClassSceneLabel>& actual,
                       const QJsonObject& expected, const QString& context) {
  const QJsonArray items = expected.value(QStringLiteral("itemDetails")).toArray();
  require(actual.size() == items.size(),
          QStringLiteral("%1 label count: native %2, upstream %3")
              .arg(context).arg(actual.size()).arg(items.size()));
  const QPointF groupOffset = translation(
      expected.value(QStringLiteral("transform")).toString(), context);
  for (qsizetype index = 0; index < actual.size(); ++index) {
    const QJsonObject item = items.at(index).toObject();
    const QJsonObject box = item.value(QStringLiteral("bbox")).toObject();
    const QPointF itemOffset = translation(
        item.value(QStringLiteral("transform")).toString(),
        context + QStringLiteral("[%1]").arg(index));
    const QPointF center = groupOffset + itemOffset + QPointF(
        box.value(QStringLiteral("x")).toDouble() +
            box.value(QStringLiteral("width")).toDouble() / 2.0,
        box.value(QStringLiteral("y")).toDouble() +
            box.value(QStringLiteral("height")).toDouble() / 2.0);
    near(actual.at(index).center.x(), center.x(),
         context + QStringLiteral("[%1].x").arg(index));
    near(actual.at(index).center.y(), center.y(),
         context + QStringLiteral("[%1].y").arg(index));
    nearLabelMetric(actual.at(index).size.width(),
                    box.value(QStringLiteral("width")).toDouble(),
                    context + QStringLiteral("[%1].width").arg(index));
    nearLabelMetric(actual.at(index).size.height(),
                    box.value(QStringLiteral("height")).toDouble(),
                    context + QStringLiteral("[%1].height").arg(index));
    require(actual.at(index).document.text ==
                item.value(QStringLiteral("text")).toString(),
            context + QStringLiteral("[%1].text").arg(index));
  }
}

const ClassSceneNode& sceneNode(const ClassScene& scene, const QString& id) {
  const auto found = std::find_if(scene.nodes.cbegin(), scene.nodes.cend(),
      [&](const auto& node) { return node.id == id; });
  require(found != scene.nodes.cend(), QStringLiteral("Missing class scene node %1").arg(id));
  return *found;
}

void compareMarkerDefinitions(const QVector<ClassMarkerDefinition>& actual,
                              const QJsonArray& expected) {
  require(actual.size() == 20, QStringLiteral("Native class marker table must have 20 variants"));
  require(expected.size() == actual.size(),
          QStringLiteral("Upstream class marker table has %1 entries").arg(expected.size()));
  for (const QJsonValue& value : expected) {
    const QJsonObject object = value.toObject();
    const QJsonObject attributes = object.value(QStringLiteral("attributes")).toObject();
    const QString id = attributes.value(QStringLiteral("id")).toString();
    const qsizetype separator = id.indexOf(QStringLiteral("_class-"));
    require(separator >= 0, QStringLiteral("Invalid upstream marker id %1").arg(id));
    const QString suffix = id.mid(separator + 7);
    const auto found = std::find_if(actual.cbegin(), actual.cend(),
        [&](const auto& marker) { return marker.suffix == suffix; });
    require(found != actual.cend(), QStringLiteral("Missing native marker %1").arg(suffix));
    const ClassMarkerDefinition& marker = *found;
    require(attributes.value(QStringLiteral("class")).toString() == marker.cssClass,
            suffix + QStringLiteral(".class"));
    near(marker.refX, number(attributes.value(QStringLiteral("refX")).toString()), suffix + QStringLiteral(".refX"));
    near(marker.refY, number(attributes.value(QStringLiteral("refY")).toString()), suffix + QStringLiteral(".refY"));
    near(marker.markerWidth, number(attributes.value(QStringLiteral("markerWidth")).toString()), suffix + QStringLiteral(".width"));
    near(marker.markerHeight, number(attributes.value(QStringLiteral("markerHeight")).toString()), suffix + QStringLiteral(".height"));
    require(marker.markerUnits == attributes.value(QStringLiteral("markerUnits")).toString(), suffix + QStringLiteral(".units"));
    require(marker.orient == attributes.value(QStringLiteral("orient")).toString(), suffix + QStringLiteral(".orient"));
    require(marker.viewBox == attributes.value(QStringLiteral("viewBox")).toString(), suffix + QStringLiteral(".viewBox"));
    const QJsonObject child = object.value(QStringLiteral("children")).toArray().first().toObject();
    const QJsonObject childAttributes = child.value(QStringLiteral("attributes")).toObject();
    require(marker.child.tag == child.value(QStringLiteral("tag")).toString(), suffix + QStringLiteral(".child.tag"));
    require(marker.child.path == childAttributes.value(QStringLiteral("d")).toString(), suffix + QStringLiteral(".child.d"));
    require(marker.child.points == childAttributes.value(QStringLiteral("points")).toString(), suffix + QStringLiteral(".child.points"));
    near(marker.child.cx, number(childAttributes.value(QStringLiteral("cx")).toString()), suffix + QStringLiteral(".child.cx"));
    near(marker.child.cy, number(childAttributes.value(QStringLiteral("cy")).toString()), suffix + QStringLiteral(".child.cy"));
    near(marker.child.radius, number(childAttributes.value(QStringLiteral("r")).toString()), suffix + QStringLiteral(".child.r"));
    require(marker.child.fill == childAttributes.value(QStringLiteral("fill")).toString(), suffix + QStringLiteral(".child.fill"));
    require(marker.child.style == childAttributes.value(QStringLiteral("style")).toString(), suffix + QStringLiteral(".child.style"));
    require(marker.child.strokeWidth == childAttributes.value(QStringLiteral("stroke-width")).toString(), suffix + QStringLiteral(".child.strokeWidth"));
  }
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected class layout fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Could not open class layout fixture"));
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("mermaidVersion")).toString() == QLatin1String("11.16.0"),
          QStringLiteral("Class scene oracle version drifted"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  bool comparedMarkers = false;
  qsizetype edgeCount = 0;
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    ClassLayoutOptions options;
    options.hierarchicalNamespaces = fixture.value(QStringLiteral("hierarchicalNamespaces")).toBool(true);
    options.hideEmptyMembersBox = fixture.value(QStringLiteral("hideEmptyMembersBox")).toBool(false);
    options.htmlLabels = fixture.value(QStringLiteral("htmlLabels")).toBool(true);
    const ClassLayoutInput input = buildClassLayoutInput(
        ClassDiagram::parse(fixture.value(QStringLiteral("source")).toString()).data(), options);
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const ClassLayoutMeasurements measurements = browserMeasurements(
        expected.value(QStringLiteral("renderedNodes")).toArray());
    const QVector<ClassBoxGeometry> boxes = layoutClassBoxes(input, measurements, options);
    const QJsonObject placementJson = expected.value(QStringLiteral("placement")).toObject();
    const ClassPlacementResult placement = layoutClassDiagramDagre(
        input, dagreMeasurements(input, placementJson));
    const ClassScene scene = buildClassScene(input, boxes, measurements, placement);
    const QImage painted = renderClassSceneToImage(scene);
    require(!painted.isNull() && painted.width() > 0 && painted.height() > 0,
            id + QStringLiteral(".paintedImage"));
    require(scene.nodes.size() == placement.nodes.size(), id + QStringLiteral(".nodeCount"));
    require(scene.clusters.size() == placement.clusters.size(), id + QStringLiteral(".clusterCount"));
    const QJsonObject structure = expected.value(QStringLiteral("structure")).toObject();
    const QJsonArray expectedEdges = structure.value(QStringLiteral("edges")).toArray();
    require(scene.edges.size() == expectedEdges.size(), id + QStringLiteral(".edgeCount"));
    for (const QJsonValue& renderedValue :
         expected.value(QStringLiteral("renderedNodes")).toArray()) {
      const QJsonObject rendered = renderedValue.toObject();
      const QString nodeId = rendered.value(QStringLiteral("id")).toString();
      const ClassSceneNode& node = sceneNode(scene, nodeId);
      const QJsonObject geometry = rendered.value(QStringLiteral("geometry")).toObject();
      const QString context = id + QLatin1Char('/') + nodeId;
      compareLabelGroup(node.annotationLabels,
                        geometry.value(QStringLiteral("annotation")).toObject(),
                        context + QStringLiteral(".annotation"));
      compareLabelGroup(node.nameLabels,
                        geometry.value(QStringLiteral("label")).toObject(),
                        context + QStringLiteral(".label"));
      compareLabelGroup(node.memberLabels,
                        geometry.value(QStringLiteral("members")).toObject(),
                        context + QStringLiteral(".members"));
      compareLabelGroup(node.methodLabels,
                        geometry.value(QStringLiteral("methods")).toObject(),
                        context + QStringLiteral(".methods"));
    }
    for (const QJsonValue& placedValue :
         placementJson.value(QStringLiteral("nodes")).toArray()) {
      const QJsonObject placed = placedValue.toObject();
      const ClassSceneNode& node = sceneNode(
          scene, placed.value(QStringLiteral("id")).toString());
      if (node.shape == QLatin1String("classBox")) continue;
      const QJsonArray labels = placed.value(QStringLiteral("labels")).toArray();
      require(node.nameLabels.size() == labels.size(),
              id + QLatin1Char('/') + node.id + QStringLiteral(".noteLabelCount"));
      for (qsizetype index = 0; index < labels.size(); ++index) {
        const QJsonObject expectedLabel = labels.at(index).toObject();
        const QJsonObject box = expectedLabel.value(QStringLiteral("bbox")).toObject();
        const ClassSceneLabel& label = node.nameLabels.at(index);
        near(label.size.width(), box.value(QStringLiteral("width")).toDouble(),
             id + QLatin1Char('/') + node.id + QStringLiteral(".noteLabelWidth"));
        near(label.size.height(), box.value(QStringLiteral("height")).toDouble(),
             id + QLatin1Char('/') + node.id + QStringLiteral(".noteLabelHeight"));
        near(node.center.x() + label.center.x(),
             expectedLabel.value(QStringLiteral("dx")).toDouble(),
             id + QLatin1Char('/') + node.id + QStringLiteral(".noteLabelX"));
        near(node.center.y() + label.center.y(),
             expectedLabel.value(QStringLiteral("dy")).toDouble(),
             id + QLatin1Char('/') + node.id + QStringLiteral(".noteLabelY"));
      }
    }
    const QJsonArray expectedClusters =
        placementJson.value(QStringLiteral("clusters")).toArray();
    require(scene.clusters.size() == expectedClusters.size(),
            id + QStringLiteral(".clusterTitleCount"));
    for (const QJsonValue& clusterValue : expectedClusters) {
      const QJsonObject expectedCluster = clusterValue.toObject();
      const QString clusterId = expectedCluster.value(QStringLiteral("id")).toString();
      const auto cluster = std::find_if(scene.clusters.cbegin(), scene.clusters.cend(),
          [&](const auto& candidate) { return candidate.id == clusterId; });
      require(cluster != scene.clusters.cend(),
              id + QLatin1Char('/') + clusterId + QStringLiteral(".clusterTitle"));
      const QJsonArray labels = expectedCluster.value(QStringLiteral("labels")).toArray();
      require(labels.size() == 1 && !cluster->titleLabel.text.isEmpty(),
              id + QLatin1Char('/') + clusterId + QStringLiteral(".clusterLabelCount"));
      const QJsonObject expectedLabel = labels.first().toObject();
      const QJsonObject box = expectedLabel.value(QStringLiteral("bbox")).toObject();
      nearLabelMetric(cluster->titleLabel.size.width(),
                      box.value(QStringLiteral("width")).toDouble(),
                      id + QLatin1Char('/') + clusterId + QStringLiteral(".clusterLabelWidth"));
      nearLabelMetric(cluster->titleLabel.size.height(),
                      box.value(QStringLiteral("height")).toDouble(),
                      id + QLatin1Char('/') + clusterId + QStringLiteral(".clusterLabelHeight"));
      near(cluster->titleLabel.center.x(),
           expectedLabel.value(QStringLiteral("dx")).toDouble(),
           id + QLatin1Char('/') + clusterId + QStringLiteral(".clusterLabelX"));
      near(cluster->titleLabel.center.y(),
           expectedLabel.value(QStringLiteral("dy")).toDouble(),
           id + QLatin1Char('/') + clusterId + QStringLiteral(".clusterLabelY"));
    }
    for (const QJsonValue& edgeValue : expectedEdges) {
      const QJsonObject edgeObject = edgeValue.toObject();
      const QString edgeId = edgeObject.value(QStringLiteral("id")).toString();
      const ClassSceneEdge& edge = sceneEdge(scene, edgeId);
      const QJsonArray segments = edgeObject.value(QStringLiteral("segments")).toArray();
      const QJsonArray points = edgeObject.value(QStringLiteral("normalizedPoints")).toArray();
      if (segments.isEmpty()) {
        require(edge.points.size() == points.size(), id + QLatin1Char('/') + edgeId + QStringLiteral(".pointCount"));
        for (qsizetype point = 0; point < edge.points.size(); ++point) {
          const QJsonObject expectedPoint = points.at(point).toObject();
          near(edge.points.at(point).x(), expectedPoint.value(QStringLiteral("x")).toDouble(),
               id + QLatin1Char('/') + edgeId + QStringLiteral(".point%1.x").arg(point));
          near(edge.points.at(point).y(), expectedPoint.value(QStringLiteral("y")).toDouble(),
               id + QLatin1Char('/') + edgeId + QStringLiteral(".point%1.y").arg(point));
        }
        comparePath(edge.path, edgeObject.value(QStringLiteral("normalizedPath")).toArray(),
                    id + QLatin1Char('/') + edgeId + QStringLiteral(".path"));
      } else {
        require(edge.segments.size() == segments.size() &&
                    edge.paths.size() == segments.size(),
                id + QLatin1Char('/') + edgeId + QStringLiteral(".segmentCount"));
        for (qsizetype segmentIndex = 0; segmentIndex < segments.size(); ++segmentIndex) {
          const QJsonObject expectedSegment = segments.at(segmentIndex).toObject();
          const QJsonArray expectedPoints =
              expectedSegment.value(QStringLiteral("normalizedPoints")).toArray();
          const QVector<QPointF>& actualPoints = edge.segments.at(segmentIndex);
          require(actualPoints.size() == expectedPoints.size(),
                  id + QLatin1Char('/') + edgeId +
                      QStringLiteral(".segment%1.pointCount").arg(segmentIndex));
          for (qsizetype point = 0; point < actualPoints.size(); ++point) {
            const QJsonObject expectedPoint = expectedPoints.at(point).toObject();
            near(actualPoints.at(point).x(),
                 expectedPoint.value(QStringLiteral("x")).toDouble(),
                 id + QLatin1Char('/') + edgeId +
                     QStringLiteral(".segment%1.point%2.x").arg(segmentIndex).arg(point));
            near(actualPoints.at(point).y(),
                 expectedPoint.value(QStringLiteral("y")).toDouble(),
                 id + QLatin1Char('/') + edgeId +
                     QStringLiteral(".segment%1.point%2.y").arg(segmentIndex).arg(point));
          }
          comparePath(edge.paths.at(segmentIndex),
                      expectedSegment.value(QStringLiteral("normalizedPath")).toArray(),
                      id + QLatin1Char('/') + edgeId +
                          QStringLiteral(".segment%1.path").arg(segmentIndex));
        }
      }
      const QJsonObject terminals = edgeObject.value(QStringLiteral("terminals")).toObject();
      if (edge.startLabelRight) {
        const QJsonObject terminal = terminals.value(QStringLiteral("startLabelRight")).toObject();
        near(edge.startLabelRight->center.x(), terminal.value(QStringLiteral("dx")).toDouble(), id + QLatin1Char('/') + edgeId + QStringLiteral(".startLabel.x"));
        near(edge.startLabelRight->center.y(), terminal.value(QStringLiteral("dy")).toDouble(), id + QLatin1Char('/') + edgeId + QStringLiteral(".startLabel.y"));
      }
      if (edge.endLabelLeft) {
        const QJsonObject terminal = terminals.value(QStringLiteral("endLabelLeft")).toObject();
        near(edge.endLabelLeft->center.x(), terminal.value(QStringLiteral("dx")).toDouble(), id + QLatin1Char('/') + edgeId + QStringLiteral(".endLabel.x"));
        near(edge.endLabelLeft->center.y(), terminal.value(QStringLiteral("dy")).toDouble(), id + QLatin1Char('/') + edgeId + QStringLiteral(".endLabel.y"));
      }
      edgeCount++;
    }
    if (!comparedMarkers) {
      compareMarkerDefinitions(scene.markers, structure.value(QStringLiteral("markers")).toArray());
      comparedMarkers = true;
    }
    require(structure.value(QStringLiteral("order")).toArray() ==
                QJsonArray{QStringLiteral("clusters"), QStringLiteral("edgePaths"),
                           QStringLiteral("edgeLabels"), QStringLiteral("nodes")},
            id + QStringLiteral(".rootOrder"));
  }
  qDebug() << "MermaidClassSceneOracleTest:" << cases.size()
           << "cases and" << edgeCount << "edges passed";
  return 0;
}
