#include "mermaid/MermaidDiagramDetector.h"
#include "mermaid/MermaidPreprocessor.h"
#include "mermaid/wardley/WardleyDiagram.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {

[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::exit(1);
}

void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

void close(qreal actual, qreal expected, const QString& field) {
  require(std::abs(actual - expected) <= 1e-9,
          field + QStringLiteral(": %1 != %2")
                      .arg(actual, 0, 'g', 17)
                      .arg(expected, 0, 'g', 17));
}

QString kind(wardley::WardleyErrorKind value) {
  switch (value) {
    case wardley::WardleyErrorKind::Lexer: return QStringLiteral("Lexer");
    case wardley::WardleyErrorKind::Parser: return QStringLiteral("Parser");
    case wardley::WardleyErrorKind::Runtime: return QStringLiteral("Runtime");
  }
  return {};
}

void compareData(const wardley::WardleyData& data, const QJsonObject& db,
                 const QString& id) {
  require(data.title == db.value(QStringLiteral("title")).toString(),
          id + QStringLiteral("/title"));
  require(data.accTitle == db.value(QStringLiteral("accTitle")).toString(),
          id + QStringLiteral("/accTitle"));
  require(data.accDescr == db.value(QStringLiteral("accDescr")).toString(),
          id + QStringLiteral("/accDescr"));
  const QJsonObject expected = db.value(QStringLiteral("data")).toObject();

  const QJsonArray nodes = expected.value(QStringLiteral("nodes")).toArray();
  require(data.nodes.size() == nodes.size(), id + QStringLiteral("/node-count"));
  for (qsizetype i = 0; i < nodes.size(); ++i) {
    const QJsonObject oracle = nodes.at(i).toObject();
    const wardley::WardleyNode& actual = data.nodes.at(i);
    const QString path = id + QStringLiteral("/node/%1/").arg(i);
    require(actual.id == oracle.value(QStringLiteral("id")).toString(), path + "id");
    require(actual.label == oracle.value(QStringLiteral("label")).toString(), path + "label");
    require(actual.className == oracle.value(QStringLiteral("className")).toString(), path + "class");
    close(actual.x, oracle.value(QStringLiteral("x")).toDouble(), path + "x");
    close(actual.y, oracle.value(QStringLiteral("y")).toDouble(), path + "y");
    const bool hasX = oracle.contains(QStringLiteral("labelOffsetX"));
    const bool hasY = oracle.contains(QStringLiteral("labelOffsetY"));
    require(actual.labelOffsetX.has_value() == hasX, path + "labelOffsetX/presence");
    require(actual.labelOffsetY.has_value() == hasY, path + "labelOffsetY/presence");
    if (hasX) close(*actual.labelOffsetX, oracle.value(QStringLiteral("labelOffsetX")).toDouble(), path + "labelOffsetX");
    if (hasY) close(*actual.labelOffsetY, oracle.value(QStringLiteral("labelOffsetY")).toDouble(), path + "labelOffsetY");
    require(actual.inPipeline == oracle.value(QStringLiteral("inPipeline")).toBool(), path + "inPipeline");
    require(actual.isPipelineParent == oracle.value(QStringLiteral("isPipelineParent")).toBool(), path + "isPipelineParent");
    require(actual.inertia == oracle.value(QStringLiteral("inertia")).toBool(), path + "inertia");
    require(actual.sourceStrategy == oracle.value(QStringLiteral("sourceStrategy")).toString(), path + "sourceStrategy");
  }

  const QJsonArray links = expected.value(QStringLiteral("links")).toArray();
  require(data.links.size() == links.size(), id + QStringLiteral("/link-count"));
  for (qsizetype i = 0; i < links.size(); ++i) {
    const QJsonObject oracle = links.at(i).toObject();
    const wardley::WardleyLink& actual = data.links.at(i);
    const QString path = id + QStringLiteral("/link/%1/").arg(i);
    require(actual.source == oracle.value(QStringLiteral("source")).toString(), path + "source");
    require(actual.target == oracle.value(QStringLiteral("target")).toString(), path + "target");
    require(actual.dashed == oracle.value(QStringLiteral("dashed")).toBool(), path + "dashed");
    const bool hasLabel = oracle.contains(QStringLiteral("label"));
    require(actual.hasLabel == hasLabel, path + "label/presence");
    require(!hasLabel || actual.label == oracle.value(QStringLiteral("label")).toString(), path + "label");
    require(actual.flow == oracle.value(QStringLiteral("flow")).toString(), path + "flow");
  }

  const QJsonArray trends = expected.value(QStringLiteral("trends")).toArray();
  require(data.trends.size() == trends.size(), id + QStringLiteral("/trend-count"));
  for (qsizetype i = 0; i < trends.size(); ++i) {
    const QJsonObject oracle = trends.at(i).toObject();
    const auto& actual = data.trends.at(i);
    const QString path = id + QStringLiteral("/trend/%1/").arg(i);
    require(actual.nodeId == oracle.value(QStringLiteral("nodeId")).toString(), path + "nodeId");
    close(actual.targetX, oracle.value(QStringLiteral("targetX")).toDouble(), path + "targetX");
    close(actual.targetY, oracle.value(QStringLiteral("targetY")).toDouble(), path + "targetY");
  }

  const QJsonArray pipelines = expected.value(QStringLiteral("pipelines")).toArray();
  require(data.pipelines.size() == pipelines.size(), id + QStringLiteral("/pipeline-count"));
  for (qsizetype i = 0; i < pipelines.size(); ++i) {
    const QJsonObject oracle = pipelines.at(i).toObject();
    const auto& actual = data.pipelines.at(i);
    require(actual.nodeId == oracle.value(QStringLiteral("nodeId")).toString(), id + "/pipeline/nodeId");
    const QJsonArray components = oracle.value(QStringLiteral("componentIds")).toArray();
    require(actual.componentIds.size() == components.size(), id + "/pipeline/component-count");
    for (qsizetype j = 0; j < components.size(); ++j)
      require(actual.componentIds.at(j) == components.at(j).toString(), id + "/pipeline/component");
  }

  const QJsonArray annotations = expected.value(QStringLiteral("annotations")).toArray();
  require(data.annotations.size() == annotations.size(), id + QStringLiteral("/annotation-count"));
  for (qsizetype i = 0; i < annotations.size(); ++i) {
    const QJsonObject oracle = annotations.at(i).toObject();
    const auto& actual = data.annotations.at(i);
    require(actual.number == oracle.value(QStringLiteral("number")).toInt(), id + "/annotation/number");
    const QJsonArray coordinates = oracle.value(QStringLiteral("coordinates")).toArray();
    require(coordinates.size() == 1, id + "/annotation/coordinate-count");
    const QJsonObject coordinate = coordinates.first().toObject();
    close(actual.coordinate.x(), coordinate.value(QStringLiteral("x")).toDouble(), id + "/annotation/x");
    close(actual.coordinate.y(), coordinate.value(QStringLiteral("y")).toDouble(), id + "/annotation/y");
    const bool hasText = oracle.contains(QStringLiteral("text"));
    require(actual.hasText == hasText, id + "/annotation/text-presence");
    require(!hasText || actual.text == oracle.value(QStringLiteral("text")).toString(), id + "/annotation/text");
  }

  const QJsonArray notes = expected.value(QStringLiteral("notes")).toArray();
  require(data.notes.size() == notes.size(), id + "/note-count");
  for (qsizetype i = 0; i < notes.size(); ++i) {
    const QJsonObject item = notes.at(i).toObject();
    require(data.notes.at(i).text == item.value(QStringLiteral("text")).toString(), id + "/note/text");
    close(data.notes.at(i).coordinate.x(), item.value(QStringLiteral("x")).toDouble(), id + "/note/x");
    close(data.notes.at(i).coordinate.y(), item.value(QStringLiteral("y")).toDouble(), id + "/note/y");
  }
  const auto compareArrows = [&](const QVector<wardley::WardleyAccelerator>& actual,
                                 const QJsonArray& oracle,
                                 const QString& name) {
    require(actual.size() == oracle.size(), id + QLatin1Char('/') + name + "-count");
    for (qsizetype i = 0; i < oracle.size(); ++i) {
      const QJsonObject item = oracle.at(i).toObject();
      require(actual.at(i).name == item.value(QStringLiteral("name")).toString(), id + QLatin1Char('/') + name + "/name");
      close(actual.at(i).coordinate.x(), item.value(QStringLiteral("x")).toDouble(), id + QLatin1Char('/') + name + "/x");
      close(actual.at(i).coordinate.y(), item.value(QStringLiteral("y")).toDouble(), id + QLatin1Char('/') + name + "/y");
    }
  };
  compareArrows(data.accelerators, expected.value(QStringLiteral("accelerators")).toArray(), QStringLiteral("accelerator"));
  compareArrows(data.deaccelerators, expected.value(QStringLiteral("deaccelerators")).toArray(), QStringLiteral("deaccelerator"));

  const bool hasBox = expected.contains(QStringLiteral("annotationsBox"));
  require(data.annotationsBox.has_value() == hasBox, id + "/annotationsBox/presence");
  if (hasBox) {
    const QJsonObject box = expected.value(QStringLiteral("annotationsBox")).toObject();
    close(data.annotationsBox->x(), box.value(QStringLiteral("x")).toDouble(), id + "/annotationsBox/x");
    close(data.annotationsBox->y(), box.value(QStringLiteral("y")).toDouble(), id + "/annotationsBox/y");
  }
  const QJsonObject axes = expected.value(QStringLiteral("axes")).toObject();
  const QJsonArray stages = axes.value(QStringLiteral("stages")).toArray();
  const QJsonArray boundaries = axes.value(QStringLiteral("stageBoundaries")).toArray();
  require(data.axes.stages.size() == stages.size(), id + "/axes/stage-count");
  require(data.axes.stageBoundaries.size() == boundaries.size(), id + "/axes/boundary-count");
  for (qsizetype i = 0; i < stages.size(); ++i)
    require(data.axes.stages.at(i) == stages.at(i).toString(), id + "/axes/stage");
  for (qsizetype i = 0; i < boundaries.size(); ++i)
    close(data.axes.stageBoundaries.at(i), boundaries.at(i).toDouble(), id + "/axes/boundary");
  const bool hasSize = expected.contains(QStringLiteral("size"));
  require(data.size.has_value() == hasSize, id + "/size/presence");
  if (hasSize) {
    const QJsonObject size = expected.value(QStringLiteral("size")).toObject();
    close(data.size->width(), size.value(QStringLiteral("width")).toDouble(), id + "/size/width");
    close(data.size->height(), size.value(QStringLiteral("height")).toDouble(), id + "/size/height");
  }
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected Wardley grammar fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QByteArray bytes = file.readAll();
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("0a94e025e34e694e956c56934608b39473eb8630668542cf61d1d09cbb8d9eb0"),
          QStringLiteral("Wardley grammar fixture bytes changed"));
  QJsonParseError jsonError;
  const QJsonObject root = QJsonDocument::fromJson(bytes, &jsonError).object();
  require(jsonError.error == QJsonParseError::NoError, jsonError.errorString());
  const QJsonObject upstream = root.value(QStringLiteral("upstream")).toObject();
  require(upstream.value(QStringLiteral("version")).toString() == QLatin1String("11.16.0") &&
              upstream.value(QStringLiteral("wardleyModuleSha256")).toString() ==
                  QLatin1String("7688a218dfc9e1eccdb8fca61d89414723ab05f3aced6f970a4ca6464e9ca3ee") &&
              upstream.value(QStringLiteral("parserModuleSha256")).toString() ==
                  QLatin1String("f541603e5c4d057f0c557f0873bd5b3be3c9878caed7ef2b6ee4e6699206dd3d") &&
              root.value(QStringLiteral("fixtureSha256")).toString() ==
                  QLatin1String("795bab868d252dd346ceaf2f617968893d1ea9cf48994ba1e225d3d95144e216"),
          QStringLiteral("Wardley grammar provenance changed"));

  int accepted = 0;
  int rejected = 0;
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const bool accept = expected.value(QStringLiteral("parse")).toBool();
    const MermaidPreprocessResult pre = preprocessDiagram(source);
    bool detected = false;
    try {
      detected = detectDiagramType(pre.code, pre.config) == QLatin1String("wardley");
    } catch (const UnknownDiagramError&) {
    }
    if (!detected) {
      require(!accept, id + QStringLiteral(": accepted source was not detected"));
      require(expected.value(QStringLiteral("error")).toObject()
                      .value(QStringLiteral("name")).toString() ==
                  QLatin1String("UnknownDiagramError"),
              id + QStringLiteral(": detector mismatch"));
      ++rejected;
      continue;
    }
    if (!accept) {
      ++rejected;
      bool threw = false;
      try {
        (void)wardley::WardleyDiagram::parse(pre.code);
      } catch (const wardley::WardleyParseError& error) {
        threw = true;
        const QJsonObject oracle = expected.value(QStringLiteral("error")).toObject();
        require(kind(error.kind) == oracle.value(QStringLiteral("kind")).toString(), id + "/kind");
        require(error.line == oracle.value(QStringLiteral("line")).toInt(),
                id + QStringLiteral("/line %1 != %2")
                         .arg(error.line)
                         .arg(oracle.value(QStringLiteral("line")).toInt()));
        require(error.column == oracle.value(QStringLiteral("column")).toInt(),
                id + QStringLiteral("/column %1 != %2")
                         .arg(error.column)
                         .arg(oracle.value(QStringLiteral("column")).toInt()));
      }
      require(threw, id + QStringLiteral(": rejected source parsed"));
      continue;
    }
    ++accepted;
    compareData(wardley::WardleyDiagram::parse(pre.code),
                expected.value(QStringLiteral("db")).toObject(), id);
  }
  require(cases.size() == 46 && accepted == 30 && rejected == 16,
          QStringLiteral("Wardley grammar table not fully visited"));
  std::puts("MermaidWardleyParserTest: 46/46 passed");
  return 0;
}
