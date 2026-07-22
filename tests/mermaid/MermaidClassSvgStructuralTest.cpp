#include "mermaid/classdiagram/ClassDiagram.h"
#include "mermaid/classdiagram/ClassScene.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/flowchart/FlowLabel.h"

#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  qCritical().noquote() << message;
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

int orderIndex(const QJsonArray& order, const QString& value) {
  for (qsizetype index = 0; index < order.size(); ++index)
    if (order.at(index).toString() == value) return static_cast<int>(index);
  return -1;
}

int nativeMathCount(const classdiagram::ClassScene& scene) {
  int count = 0;
  const auto add = [&](const classdiagram::ClassSceneLabel& label) {
    count += label.document.math.size();
  };
  for (const auto& node : scene.nodes) {
    for (const auto& label : node.annotationLabels) add(label);
    for (const auto& label : node.nameLabels) add(label);
    for (const auto& label : node.memberLabels) add(label);
    for (const auto& label : node.methodLabels) add(label);
  }
  for (const auto& edge : scene.edges) {
    if (edge.label.isEmpty()) continue;
    count += flowchart::parseFlowLabel(
        edge.label, QStringLiteral("markdown"), true).math.size();
  }
  for (const auto& cluster : scene.clusters) {
    if (cluster.label.isEmpty()) continue;
    count += flowchart::parseFlowLabel(
        cluster.label, QStringLiteral("markdown"), true).math.size();
  }
  return count;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected class SVG structural manifest"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly),
          QStringLiteral("Could not open class SVG structural manifest"));
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("mermaidVersion")).toString() ==
              QLatin1String("11.16.0") &&
              root.value(QStringLiteral("fixtureSha256")).toString() ==
              QLatin1String("39272a26cf151587343b5a5e4840b3d710a4a52a43964c14f000840e8924b182"),
          QStringLiteral("Class SVG structural fixture drifted"));

  editor::MermaidRenderCache cache;
  QSet<QString> ids;
  int ariaCases = 0;
  int mathCases = 0;
  int orderedEntries = 0;
  int labelContainers = 0;
  int svgTextContainers = 0;
  int tspanEntries = 0;
  int markerEntries = 0;
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 17,
          QStringLiteral("Class SVG structural matrix must retain 17 cases"));
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    require(!id.isEmpty() && !ids.contains(id),
            QStringLiteral("Duplicate class structural case: %1").arg(id));
    ids.insert(id);
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const auto entry = cache.getSync(cache.makeKey(source), source);
    require(entry.status == editor::MermaidRenderStatus::Ready && entry.classScene,
            QStringLiteral("%1 native class scene failed: %2")
                .arg(id, entry.errorMessage));
    const auto& scene = *entry.classScene;
    const QJsonObject structure = fixture.value(QStringLiteral("svgStructure")).toObject();
    const QJsonObject rootAttributes = structure.value(QStringLiteral("root")).toObject();
    require(rootAttributes.value(QStringLiteral("xmlns")).toString() ==
                QLatin1String("http://www.w3.org/2000/svg") &&
                rootAttributes.value(QStringLiteral("class")).toString() ==
                QLatin1String("classDiagram") &&
                rootAttributes.value(QStringLiteral("role")).toString() ==
                QLatin1String("graphics-document document") &&
                rootAttributes.value(QStringLiteral("aria-roledescription")).toString() ==
                QLatin1String("class"),
            QStringLiteral("%1 root SVG role/class drifted").arg(id));
    const QStringList viewBox = rootAttributes.value(QStringLiteral("viewBox"))
                                    .toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QJsonObject expectedViewBox = fixture.value(QStringLiteral("viewBox")).toObject();
    require(viewBox.size() == 4 &&
                std::abs(viewBox.at(2).toDouble() -
                         expectedViewBox.value(QStringLiteral("width")).toDouble()) < 0.001 &&
                std::abs(viewBox.at(3).toDouble() -
                         expectedViewBox.value(QStringLiteral("height")).toDouble()) < 0.001,
            QStringLiteral("%1 root viewBox drifted").arg(id));

    const QJsonObject counts = structure.value(QStringLiteral("counts")).toObject();
    require(counts.value(QStringLiteral("nodes")).toInt() == scene.nodes.size() &&
                counts.value(QStringLiteral("clusters")).toInt() == scene.clusters.size() &&
                counts.value(QStringLiteral("edgePaths")).toInt() == scene.edges.size() &&
                counts.value(QStringLiteral("defs")).toInt() >= 1,
            QStringLiteral("%1 browser/native structural counts differ").arg(id));
    const QJsonArray labels = structure.value(QStringLiteral("labelContainers")).toArray();
    int htmlLabels = 0;
    int svgLabels = 0;
    for (const QJsonValue& labelValue : labels) {
      const QJsonObject label = labelValue.toObject();
      const bool htmlBacked =
          label.value(QStringLiteral("foreignObjectCount")).toInt() == 1;
      const bool svgBacked = label.value(QStringLiteral("textCount")).toInt() >= 1 &&
                             label.value(QStringLiteral("tspanCount")).toInt() >= 1;
      require(label.value(QStringLiteral("tag")).toString() == QLatin1String("g") &&
                  htmlBacked != svgBacked &&
                  !label.value(QStringLiteral("class")).toString().isEmpty(),
              QStringLiteral("%1 label container structure drifted").arg(id));
      htmlLabels += htmlBacked;
      svgLabels += svgBacked;
      tspanEntries += label.value(QStringLiteral("tspanCount")).toInt();
    }
    require(htmlLabels == counts.value(QStringLiteral("foreignObject")).toInt(),
            QStringLiteral("%1 foreignObject/label count drifted").arg(id));
    svgTextContainers += svgLabels;
    labelContainers += labels.size();
    const int browserMath = counts.value(QStringLiteral("math")).toInt();
    require(nativeMathCount(scene) == browserMath,
            QStringLiteral("%1 native/browser MathML classification differs").arg(id));
    mathCases += browserMath > 0;

    const QJsonArray markers = structure.value(QStringLiteral("markers")).toArray();
    require(markers.size() == 19 && scene.markers.size() >= markers.size(),
            QStringLiteral("%1 marker definition count drifted").arg(id));
    for (const QJsonValue& markerValue : markers) {
      const QJsonObject marker = markerValue.toObject();
      const QJsonObject attributes = marker.value(QStringLiteral("attributes")).toObject();
      const QString markerId = attributes.value(QStringLiteral("id")).toString();
      const qsizetype separator = markerId.lastIndexOf(QLatin1String("_class-"));
      require(separator >= 0 &&
                  !attributes.value(QStringLiteral("refX")).toString().isEmpty() &&
                  !attributes.value(QStringLiteral("refY")).toString().isEmpty() &&
                  !attributes.value(QStringLiteral("markerWidth")).toString().isEmpty() &&
                  !attributes.value(QStringLiteral("markerHeight")).toString().isEmpty() &&
                  attributes.value(QStringLiteral("orient")).toString() == QLatin1String("auto") &&
                  !marker.value(QStringLiteral("childTag")).toString().isEmpty(),
              QStringLiteral("%1 marker attributes are incomplete").arg(id));
      const QString suffix = markerId.mid(separator + 7);
      require(std::any_of(scene.markers.cbegin(), scene.markers.cend(),
                          [&](const auto& candidate) {
                            return candidate.suffix == suffix;
                          }),
              QStringLiteral("%1 native marker %2 is missing").arg(id, suffix));
    }
    markerEntries += markers.size();

    const QJsonArray order = structure.value(QStringLiteral("domOrder")).toArray();
    require(!order.isEmpty() && orderIndex(order, QStringLiteral("defs:")) == 0,
            QStringLiteral("%1 DOM order lost defs root").arg(id));
    const int edgePaths = orderIndex(order, QStringLiteral("g:edgePaths"));
    const int edgeLabels = orderIndex(order, QStringLiteral("g:edgeLabels"));
    const int nodes = orderIndex(order, QStringLiteral("g:nodes"));
    require(edgePaths >= 0 && edgeLabels > edgePaths && nodes > edgeLabels,
            QStringLiteral("%1 class SVG layer order drifted").arg(id));
    orderedEntries += order.size();

    const auto data = classdiagram::ClassDiagram::parse(source).data();
    const QString labelledBy = rootAttributes.value(QStringLiteral("aria-labelledby")).toString();
    if (!labelledBy.isEmpty()) {
      ++ariaCases;
      require(!data.accTitle.isEmpty() && !data.accDescription.isEmpty() &&
                  structure.value(QStringLiteral("ariaTitle")).toString() == data.accTitle &&
                  structure.value(QStringLiteral("ariaDescription")).toString() ==
                      data.accDescription,
              QStringLiteral("%1 accessibility title/description drifted").arg(id));
    } else {
      require(data.accTitle.isEmpty() && data.accDescription.isEmpty(),
              QStringLiteral("%1 lost accessibility references").arg(id));
    }
  }
  require(ariaCases == 1 && mathCases == 2 && labelContainers == 65 &&
              svgTextContainers == 6 && tspanEntries == 19 &&
              markerEntries == 323 && orderedEntries >= 900,
          QStringLiteral("Class SVG structural coverage matrix regressed"));
  qDebug() << "MermaidClassSvgStructuralTest:" << cases.size()
           << "cases," << orderedEntries << "ordered DOM entries passed";
  return 0;
}
