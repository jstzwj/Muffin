#include "mermaid/classdiagram/ClassDiagram.h"
#include "mermaid/classdiagram/ClassLayout.h"

#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace muffin::mermaid::classdiagram;

namespace {
qreal coordinateTolerance = 0.01;
qreal measurementTolerance = 0.2;
[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}
void require(bool condition, const QString& message) { if (!condition) fail(message); }

QJsonObject memberJson(const ClassLayoutMemberInput& value) {
  return {{QStringLiteral("text"), value.text},
          {QStringLiteral("cssStyle"), value.cssStyle}};
}

QJsonObject nodeJson(const ClassLayoutNodeInput& value) {
  QJsonArray members, methods;
  for (const auto& member : value.members) members.append(memberJson(member));
  for (const auto& method : value.methods) methods.append(memberJson(method));
  return {{QStringLiteral("id"), value.id},
          {QStringLiteral("label"), value.label},
          {QStringLiteral("text"), value.text},
          {QStringLiteral("shape"), value.shape},
          {QStringLiteral("parentId"), value.parentId},
          {QStringLiteral("cssClasses"), value.cssClasses},
          {QStringLiteral("cssStyles"), QJsonArray::fromStringList(value.cssStyles)},
          {QStringLiteral("styles"), QJsonArray::fromStringList(value.styles)},
          {QStringLiteral("annotations"), QJsonArray::fromStringList(value.annotations)},
          {QStringLiteral("members"), members},
          {QStringLiteral("methods"), methods},
          {QStringLiteral("padding"), value.padding ? QJsonValue(*value.padding)
                                                     : QJsonValue(QJsonValue::Null)},
          {QStringLiteral("isGroup"), value.isGroup},
          {QStringLiteral("look"), value.look}};
}

QJsonObject edgeJson(const ClassLayoutEdgeInput& value) {
  return {{QStringLiteral("id"), value.id},
          {QStringLiteral("start"), value.start},
          {QStringLiteral("end"), value.end},
          {QStringLiteral("label"), value.label},
          {QStringLiteral("pattern"), value.pattern},
          {QStringLiteral("arrowTypeStart"), value.arrowTypeStart},
          {QStringLiteral("arrowTypeEnd"), value.arrowTypeEnd},
          {QStringLiteral("startLabelRight"), value.startLabelRight},
          {QStringLiteral("endLabelLeft"), value.endLabelLeft},
          {QStringLiteral("style"), QJsonArray::fromStringList(value.style)},
          {QStringLiteral("labelStyle"), QJsonArray::fromStringList(value.labelStyle)},
          {QStringLiteral("classes"), value.classes},
          {QStringLiteral("look"), value.look}};
}

QJsonObject inputJson(const ClassLayoutInput& value) {
  QJsonArray nodes, edges;
  for (const auto& node : value.nodes) nodes.append(nodeJson(node));
  for (const auto& edge : value.edges) edges.append(edgeJson(edge));
  return {{QStringLiteral("nodes"), nodes},
          {QStringLiteral("edges"), edges},
          {QStringLiteral("direction"), value.direction},
          {QStringLiteral("nodeSpacing"), value.nodeSpacing},
          {QStringLiteral("rankSpacing"), value.rankSpacing},
          {QStringLiteral("markers"), QJsonArray::fromStringList(value.markers)}};
}

QString json(const QJsonObject& value) {
  return QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Indented));
}

void requireNear(qreal actual, qreal expected, const QString& context) {
  if (std::abs(actual - expected) > coordinateTolerance)
    fail(QStringLiteral("%1: native %2, upstream %3")
             .arg(context).arg(actual, 0, 'f', 3).arg(expected, 0, 'f', 3));
}

QSizeF size(const QJsonValue& value) {
  const QJsonObject object = value.toObject();
  return {object.value(QStringLiteral("width")).toDouble(),
          object.value(QStringLiteral("height")).toDouble()};
}

QVector<ClassTextMeasurement> measurements(const QJsonArray& values,
                                           bool svgText) {
  QVector<ClassTextMeasurement> result;
  for (const QJsonValue& value : values) {
    const QJsonObject object = value.toObject();
    result.append({QRectF(object.value(QStringLiteral("x")).toDouble(),
                          object.value(QStringLiteral("y")).toDouble(),
                          object.value(QStringLiteral("width")).toDouble(),
                          object.value(QStringLiteral("height")).toDouble()),
                   object.value(QStringLiteral("lineCount")).toInteger(1),
                   object.value(QStringLiteral("svgText")).toBool(svgText)});
  }
  return result;
}

QRectF rect(const QJsonValue& value) {
  const QJsonObject object = value.toObject();
  return {object.value(QStringLiteral("x")).toDouble(),
          object.value(QStringLiteral("y")).toDouble(),
          object.value(QStringLiteral("width")).toDouble(),
          object.value(QStringLiteral("height")).toDouble()};
}

QPointF translation(const QString& value) {
  static const QRegularExpression pattern(
      QStringLiteral("^translate\\(([-+0-9.eE]+)(?:, | )([-+0-9.eE]+)\\)$"));
  const auto match = pattern.match(value);
  require(match.hasMatch(), QStringLiteral("Invalid class compartment transform: %1").arg(value));
  return {match.captured(1).toDouble(), match.captured(2).toDouble()};
}

void requireRectNear(const QRectF& actual, const QRectF& expected,
                     const QString& context) {
  requireNear(actual.x(), expected.x(), context + QStringLiteral(".x"));
  requireNear(actual.y(), expected.y(), context + QStringLiteral(".y"));
  requireNear(actual.width(), expected.width(), context + QStringLiteral(".width"));
  requireNear(actual.height(), expected.height(), context + QStringLiteral(".height"));
}

void requireCompartmentNear(const ClassCompartmentGeometry& actual,
                            const QJsonObject& expected, const QString& context) {
  requireRectNear(actual.localBounds, rect(expected.value(QStringLiteral("bbox"))),
                  context + QStringLiteral(".bbox"));
  const QPointF point = translation(expected.value(QStringLiteral("transform")).toString());
  requireNear(actual.translation.x(), point.x(), context + QStringLiteral(".translateX"));
  requireNear(actual.translation.y(), point.y(), context + QStringLiteral(".translateY"));
}

void requireMeasurementsNear(const QVector<ClassTextMeasurement>& actual,
                      const QJsonArray& expected,
                      const QString& context) {
  require(actual.size() == expected.size(),
          QStringLiteral("%1 item count: native %2, upstream %3")
              .arg(context).arg(actual.size()).arg(expected.size()));
  for (qsizetype index = 0; index < actual.size(); ++index) {
    const QSizeF upstream = size(expected.at(index));
    const QString item = context + QStringLiteral("[%1]").arg(index);
    const QRectF bounds = actual.at(index).bounds;
    require(std::abs(bounds.x() - expected.at(index).toObject()
                                  .value(QStringLiteral("x")).toDouble()) <= measurementTolerance &&
                std::abs(bounds.y() - expected.at(index).toObject()
                                  .value(QStringLiteral("y")).toDouble()) <= measurementTolerance,
            QStringLiteral("%1.bounds: native %2,%3 %4x%5; upstream %6,%7 %8x%9")
                .arg(item).arg(bounds.x()).arg(bounds.y())
                .arg(bounds.width()).arg(bounds.height())
                .arg(expected.at(index).toObject().value(QStringLiteral("x")).toDouble())
                .arg(expected.at(index).toObject().value(QStringLiteral("y")).toDouble())
                .arg(upstream.width()).arg(upstream.height()));
    require(std::abs(bounds.width() - upstream.width()) <= measurementTolerance,
            QStringLiteral("%1.width: native %2, upstream %3")
                .arg(item).arg(bounds.width()).arg(upstream.width()));
    require(std::abs(bounds.height() - upstream.height()) <= measurementTolerance,
            QStringLiteral("%1.height: native %2, upstream %3")
                .arg(item).arg(bounds.height()).arg(upstream.height()));
  }
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected class layout fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), QStringLiteral("Could not open class layout fixture"));
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("mermaidVersion")).toString() == QLatin1String("11.16.0") &&
              root.value(QStringLiteral("oracle")).toString() ==
                  QLatin1String("ClassDB.getData+classBox.svg+dagre.svg+structure.svg"),
          QStringLiteral("Class layout upstream contract drifted"));
  require(root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("e1d7c6e3f17cc8ccacdbc208a3a52e378dd6a8d4174ebd3eb0f5c113aaf0c419"),
          QStringLiteral("Class layout fixture changed; audit input mapping and update its digest"));

  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 20, QStringLiteral("Class layout case count drifted"));
  QSet<QString> ids, shapes, markers;
  for (const QJsonValue& caseValue : cases) {
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    require(!id.isEmpty() && !ids.contains(id), QStringLiteral("Duplicate class layout case: %1").arg(id));
    ids.insert(id);
    const ClassDiagram diagram = ClassDiagram::parse(fixture.value(QStringLiteral("source")).toString());
    ClassLayoutOptions options;
    options.hierarchicalNamespaces = fixture.value(QStringLiteral("hierarchicalNamespaces"))
                                         .toBool(true);
    options.hideEmptyMembersBox =
        fixture.value(QStringLiteral("hideEmptyMembersBox")).toBool(false);
    options.htmlLabels = fixture.value(QStringLiteral("htmlLabels")).toBool(true);
    coordinateTolerance = options.htmlLabels ? 0.01 : 0.2;
    const QString source = fixture.value(QStringLiteral("source")).toString();
    measurementTolerance = !options.htmlLabels && std::any_of(
        source.cbegin(), source.cend(), [](QChar ch) { return ch.unicode() > 0x7f; })
        ? 0.5 : options.htmlLabels ? 0.2 : 0.3;
    const ClassLayoutInput input = buildClassLayoutInput(diagram.data(), options);
    const QJsonObject actual = inputJson(input);
    QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const QJsonArray renderedNodes = expected.take(QStringLiteral("renderedNodes")).toArray();
    const QJsonObject placement = expected.take(QStringLiteral("placement")).toObject();
    expected.remove(QStringLiteral("structure"));
    require(actual == expected,
            QStringLiteral("Class layout input mismatch for %1\nNative:\n%2\nUpstream:\n%3")
                .arg(id, json(actual), json(expected)));

    ClassLayoutMeasurements measurements;
    for (const QJsonValue& renderedValue : renderedNodes) {
      const QJsonObject rendered = renderedValue.toObject();
      const QJsonObject measured = rendered.value(QStringLiteral("measured")).toObject();
      const bool svgText = !options.htmlLabels;
      measurements.insert(rendered.value(QStringLiteral("id")).toString(), {
          ::measurements(measured.value(QStringLiteral("annotation")).toArray(), svgText),
          ::measurements(measured.value(QStringLiteral("label")).toArray(), svgText),
          ::measurements(measured.value(QStringLiteral("members")).toArray(), svgText),
          ::measurements(measured.value(QStringLiteral("methods")).toArray(), svgText),
          svgText ? 3.0 : 0.0});
    }
    const QVector<ClassBoxGeometry> geometries = layoutClassBoxes(input, measurements, options);
    ClassLabelMeasureOptions nativeMeasureOptions;
    nativeMeasureOptions.htmlLabels = options.htmlLabels;
    const ClassLayoutMeasurements nativeMeasurements =
        measureClassLayoutLabels(input, nativeMeasureOptions);
    require(geometries.size() == renderedNodes.size(),
            QStringLiteral("Class geometry count mismatch for %1").arg(id));
    for (const QJsonValue& renderedValue : renderedNodes) {
      const QJsonObject rendered = renderedValue.toObject();
      const QString nodeId = rendered.value(QStringLiteral("id")).toString();
      const auto found = std::find_if(geometries.cbegin(), geometries.cend(),
          [&](const ClassBoxGeometry& geometry) { return geometry.id == nodeId; });
      require(found != geometries.cend(),
              QStringLiteral("Missing native class geometry for %1/%2").arg(id, nodeId));
      const QJsonObject geometry = rendered.value(QStringLiteral("geometry")).toObject();
      const QJsonObject measured = rendered.value(QStringLiteral("measured")).toObject();
      const QString context = id + QLatin1Char('/') + nodeId;
      const ClassNodeMeasurements nativeMeasured = nativeMeasurements.value(nodeId);
      requireMeasurementsNear(nativeMeasured.annotations,
          measured.value(QStringLiteral("annotation")).toArray(), context + QStringLiteral(".measured.annotation"));
      requireMeasurementsNear(nativeMeasured.labels,
          measured.value(QStringLiteral("label")).toArray(), context + QStringLiteral(".measured.label"));
      requireMeasurementsNear(nativeMeasured.members,
          measured.value(QStringLiteral("members")).toArray(), context + QStringLiteral(".measured.members"));
      requireMeasurementsNear(nativeMeasured.methods,
          measured.value(QStringLiteral("methods")).toArray(), context + QStringLiteral(".measured.methods"));
      requireRectNear(found->bounds, rect(geometry.value(QStringLiteral("bbox"))),
                      context + QStringLiteral(".bbox"));
      requireRectNear(found->outerRect, rect(geometry.value(QStringLiteral("outer"))),
                      context + QStringLiteral(".outer"));
      requireCompartmentNear(found->annotation,
          geometry.value(QStringLiteral("annotation")).toObject(), context + QStringLiteral(".annotation"));
      requireCompartmentNear(found->label,
          geometry.value(QStringLiteral("label")).toObject(), context + QStringLiteral(".label"));
      requireCompartmentNear(found->members,
          geometry.value(QStringLiteral("members")).toObject(), context + QStringLiteral(".members"));
      requireCompartmentNear(found->methods,
          geometry.value(QStringLiteral("methods")).toObject(), context + QStringLiteral(".methods"));
      const QJsonArray dividers = geometry.value(QStringLiteral("dividers")).toArray();
      require(found->dividers.size() == dividers.size(),
              QStringLiteral("Divider count mismatch for %1").arg(context));
      for (qsizetype divider = 0; divider < found->dividers.size(); ++divider)
        requireRectNear(found->dividers.at(divider), rect(dividers.at(divider)),
                        context + QStringLiteral(".divider%1").arg(divider));
    }

    {
      ClassDagreMeasurements dagreMeasurements;
      const QJsonArray placedNodes = placement.value(QStringLiteral("nodes")).toArray();
      for (const QJsonValue& nodeValue : placedNodes) {
        const QJsonObject node = nodeValue.toObject();
        dagreMeasurements.nodes.insert(node.value(QStringLiteral("id")).toString(),
            QSizeF(node.value(QStringLiteral("width")).toDouble(),
                   node.value(QStringLiteral("height")).toDouble()));
      }
      const QJsonArray edgeLabels = placement.value(QStringLiteral("edgeLabels")).toArray();
      require(edgeLabels.size() == input.edges.size(),
              QStringLiteral("Class edge label count mismatch for %1").arg(id));
      for (qsizetype edgeIndex = 0; edgeIndex < edgeLabels.size(); ++edgeIndex) {
        const QJsonObject box = edgeLabels.at(edgeIndex).toObject()
                                    .value(QStringLiteral("bbox")).toObject();
        dagreMeasurements.edgeLabels.insert(input.edges.at(edgeIndex).id,
            QSizeF(box.value(QStringLiteral("width")).toDouble(),
                   box.value(QStringLiteral("height")).toDouble()));
      }
      const ClassDagreMeasurements nativeDagreMeasurements =
          measureClassDagreInput(input, geometries, nativeMeasureOptions);
      for (auto iterator = dagreMeasurements.nodes.cbegin();
           iterator != dagreMeasurements.nodes.cend(); ++iterator) {
        const QSizeF native = nativeDagreMeasurements.nodes.value(iterator.key());
        require(std::abs(native.width() - iterator.value().width()) <= 0.2 &&
                    std::abs(native.height() - iterator.value().height()) <= 0.2,
                QStringLiteral("%1/%2 native Dagre node measurement mismatch: %3x%4 vs %5x%6")
                    .arg(id, iterator.key()).arg(native.width()).arg(native.height())
                    .arg(iterator.value().width()).arg(iterator.value().height()));
      }
      for (auto iterator = dagreMeasurements.edgeLabels.cbegin();
           iterator != dagreMeasurements.edgeLabels.cend(); ++iterator) {
        const QSizeF native = nativeDagreMeasurements.edgeLabels.value(iterator.key());
        require(std::abs(native.width() - iterator.value().width()) <= 0.2 &&
                    std::abs(native.height() - iterator.value().height()) <= 0.2,
                QStringLiteral("%1/%2 native Dagre edge measurement mismatch")
                    .arg(id, iterator.key()));
      }
      const ClassPlacementResult nativePlacement =
          layoutClassDiagramDagre(input, dagreMeasurements);
      require(nativePlacement.nodes.size() == placedNodes.size(),
              QStringLiteral("Class Dagre node count mismatch for %1").arg(id));
      for (const QJsonValue& upstreamValue : placedNodes) {
        const QJsonObject upstream = upstreamValue.toObject();
        const QString placedId = upstream.value(QStringLiteral("id")).toString();
        const auto nativeNode = std::find_if(nativePlacement.nodes.cbegin(),
            nativePlacement.nodes.cend(), [&](const ClassPlacementNode& node) {
              return node.id == placedId;
            });
        require(nativeNode != nativePlacement.nodes.cend(),
                QStringLiteral("Missing Class Dagre node for %1/%2").arg(id, placedId));
        const ClassPlacementNode& node = *nativeNode;
        requireNear(node.x, upstream.value(QStringLiteral("dx")).toDouble(),
                    id + QLatin1Char('/') + node.id + QStringLiteral(".dagreX"));
        requireNear(node.y, upstream.value(QStringLiteral("dy")).toDouble(),
                    id + QLatin1Char('/') + node.id + QStringLiteral(".dagreY"));
        requireNear(node.width, upstream.value(QStringLiteral("width")).toDouble(),
                    id + QLatin1Char('/') + node.id + QStringLiteral(".dagreWidth"));
        requireNear(node.height, upstream.value(QStringLiteral("height")).toDouble(),
                    id + QLatin1Char('/') + node.id + QStringLiteral(".dagreHeight"));
      }
      require(nativePlacement.edges.size() == input.edges.size(),
              QStringLiteral("Class Dagre edge count mismatch for %1").arg(id));
      for (qsizetype edgeIndex = 0; edgeIndex < input.edges.size(); ++edgeIndex) {
        const ClassLayoutEdgeInput& edge = input.edges.at(edgeIndex);
        const auto nativeEdge = std::find_if(nativePlacement.edges.cbegin(),
            nativePlacement.edges.cend(), [&](const ClassPlacementEdge& placed) {
              return placed.id == edge.id;
            });
        require(nativeEdge != nativePlacement.edges.cend(),
                QStringLiteral("Missing Class Dagre edge for %1/%2").arg(id, edge.id));
        const QJsonObject upstream = edgeLabels.at(edgeIndex).toObject();
        if (!upstream.value(QStringLiteral("dx")).isNull()) {
          require(nativeEdge->labelPosition.has_value(),
                  QStringLiteral("Missing Class Dagre edge label for %1/%2").arg(id, edge.id));
          requireNear(nativeEdge->labelPosition->x(),
                      upstream.value(QStringLiteral("dx")).toDouble(),
                      id + QLatin1Char('/') + edge.id + QStringLiteral(".labelX"));
          requireNear(nativeEdge->labelPosition->y(),
                      upstream.value(QStringLiteral("dy")).toDouble(),
                      id + QLatin1Char('/') + edge.id + QStringLiteral(".labelY"));
        }
      }
      const QJsonArray upstreamClusters =
          placement.value(QStringLiteral("clusters")).toArray();
      require(nativePlacement.clusters.size() == upstreamClusters.size(),
              QStringLiteral("Class cluster count mismatch for %1").arg(id));
      for (const QJsonValue& clusterValue : upstreamClusters) {
        const QJsonObject upstream = clusterValue.toObject();
        const QString clusterId = upstream.value(QStringLiteral("id")).toString();
        const auto nativeCluster = std::find_if(nativePlacement.clusters.cbegin(),
            nativePlacement.clusters.cend(), [&](const ClassPlacementCluster& cluster) {
              return cluster.id == clusterId;
            });
        require(nativeCluster != nativePlacement.clusters.cend(),
                QStringLiteral("Missing class cluster for %1/%2").arg(id, clusterId));
        const QJsonObject box = upstream.value(QStringLiteral("bbox")).toObject();
        requireNear(nativeCluster->x, upstream.value(QStringLiteral("dx")).toDouble(),
                    id + QLatin1Char('/') + clusterId + QStringLiteral(".clusterX"));
        requireNear(nativeCluster->y, upstream.value(QStringLiteral("dy")).toDouble(),
                    id + QLatin1Char('/') + clusterId + QStringLiteral(".clusterY"));
        requireNear(nativeCluster->width, box.value(QStringLiteral("width")).toDouble(),
                    id + QLatin1Char('/') + clusterId + QStringLiteral(".clusterWidth"));
        requireNear(nativeCluster->height, box.value(QStringLiteral("height")).toDouble(),
                    id + QLatin1Char('/') + clusterId + QStringLiteral(".clusterHeight"));
      }
    }
    for (const auto& node : input.nodes) shapes.insert(node.shape);
    for (const auto& edge : input.edges) {
      markers.insert(edge.arrowTypeStart);
      markers.insert(edge.arrowTypeEnd);
    }
  }
  for (const QString& shape : {QStringLiteral("classBox"), QStringLiteral("rect"),
                               QStringLiteral("note")})
    require(shapes.contains(shape), QStringLiteral("Class layout shape is uncovered: %1").arg(shape));
  for (const QString& marker : {QStringLiteral("aggregation"), QStringLiteral("extension"),
                                QStringLiteral("composition"), QStringLiteral("dependency"),
                                QStringLiteral("lollipop")})
    require(markers.contains(marker), QStringLiteral("Class marker is uncovered: %1").arg(marker));

  qDebug() << "MermaidClassLayoutOracleTest:" << cases.size()
           << "ClassDB.getData cases passed";
  return 0;
}
