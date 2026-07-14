// Level-2 intermediate-state golden (milestone G2). Captures the native Dagre
// pipeline's graph state at the rank / parentDummyChains / addBorderSegments /
// order phase boundaries as a layered, deterministic JSON golden, so any future
// change that alters an intermediate phase is caught and LOCALIZED (the spec's
// "分层保存，便于定位失败").
//
// Parity scope note: upstream-relative of these intermediates is blocked because
// mermaid bundles dagre with its phase names minified away (the dagre source is
// unreachable from the rendered SVG). Full-pipeline upstream parity is already
// certified by the final-geometry L2 golden (MermaidDagreLayoutTest, ≤0.002px).
// These snapshots are therefore a NATIVE-REGRESSION golden: they lock native
// phase behaviour. The values are all integers/strings (rank/order/minlen/
// weight/parent/dummy-type), so comparison is exact — no tolerance.
//
// Goldenfile pattern: the snapshot golden (flowchart-dagre-snapshots.json) lives
// next to the geometry fixture. If absent, the test WRITES it (regeneration =
// delete + rerun); if present, it compares. ctest always runs in compare mode.

#include "mermaid/dagre/Layout.h"
#include "mermaid/flowchart/Flowchart.h"
#include "mermaid/flowchart/FlowchartLayout.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdlib>
#include <vector>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) { qCritical().noquote() << message; std::exit(1); }
void require(bool condition, const QString& message) { if (!condition) fail(message); }

// Build the case→snapshots JSON from a native Dagre run.
QJsonArray computeAll(const QJsonArray& cases) {
  QJsonArray out;
  for (const QJsonValue& value : cases) {
    const QJsonObject fixture = value.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const flowchart::Flowchart chart = flowchart::Flowchart::parse(fixture.value(QStringLiteral("source")).toString());
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    QMap<QString, QSizeF> sizes;
    for (const QJsonValue& nodeValue : expected.value(QStringLiteral("nodes")).toArray()) {
      const QJsonObject node = nodeValue.toObject();
      sizes.insert(node.value(QStringLiteral("id")).toString(),
                   QSizeF(node.value(QStringLiteral("width")).toDouble(), node.value(QStringLiteral("height")).toDouble()));
    }
    flowchart::FlowLayoutOptions options;
    options.curve = fixture.value(QStringLiteral("curve")).toString();
    for (const QJsonValue& edgeValue : expected.value(QStringLiteral("edges")).toArray()) {
      const QJsonObject edge = edgeValue.toObject();
      const QJsonObject label = edge.value(QStringLiteral("label")).toObject();
      if (!label.isEmpty() && label.value(QStringLiteral("width")).toDouble() > 0.0)
        options.measuredEdgeLabels.insert(edge.value(QStringLiteral("id")).toString(),
                                          QSizeF(label.value(QStringLiteral("width")).toDouble(), label.value(QStringLiteral("height")).toDouble()));
    }
    std::vector<dagre::DagreSnapshot> snapshots;
    flowchart::layoutFlowchartNodesDagre(chart.data(), sizes, options, &snapshots);
    QJsonArray snapJson;
    for (const dagre::DagreSnapshot& snap : snapshots) {
      QJsonObject s;
      s.insert(QStringLiteral("phase"), snap.phase);
      s.insert(QStringLiteral("state"), snap.state);
      snapJson.append(s);
    }
    QJsonObject caseJson;
    caseJson.insert(QStringLiteral("id"), id);
    caseJson.insert(QStringLiteral("snapshots"), snapJson);
    out.append(caseJson);
  }
  return out;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  require(argc == 2, QStringLiteral("Expected geometry fixture path"));
  const QString geometryPath = QString::fromLocal8Bit(argv[1]);
  const QDir dir = QFileInfo(geometryPath).absoluteDir();
  const QString snapshotPath = dir.filePath(QStringLiteral("flowchart-dagre-snapshots.json"));

  QFile geometryFile(geometryPath);
  require(geometryFile.open(QIODevice::ReadOnly), QStringLiteral("Could not open geometry fixture"));
  const QJsonObject geometryRoot = QJsonDocument::fromJson(geometryFile.readAll()).object();
  require(geometryRoot.value(QStringLiteral("upstream")).toObject().value(QStringLiteral("version")).toString() ==
              QLatin1String("11.16.0"),
          QStringLiteral("Geometry fixture version drifted"));
  const QJsonArray cases = geometryRoot.value(QStringLiteral("cases")).toArray();
  const QJsonArray computed = computeAll(cases);

  QFile snapshotFile(snapshotPath);
  const bool updateGolden = qEnvironmentVariableIntValue("MUFFIN_UPDATE_GOLDENS") == 1;
  if (updateGolden || !snapshotFile.exists()) {
    QJsonObject root;
    root.insert(QStringLiteral("upstream"), geometryRoot.value(QStringLiteral("upstream")).toObject());
    root.insert(QStringLiteral("cases"), computed);
    require(snapshotFile.open(QIODevice::WriteOnly | QIODevice::Truncate),
            QStringLiteral("Could not write snapshot golden"));
    snapshotFile.write(QJsonDocument(root).toJson());
    qDebug().noquote() << "MermaidDagreSnapshotTest: generated" << snapshotPath;
    return 0;
  }

  require(snapshotFile.open(QIODevice::ReadOnly), QStringLiteral("Could not open snapshot golden"));
  const QJsonObject goldenRoot = QJsonDocument::fromJson(snapshotFile.readAll()).object();
  const QJsonArray goldenCases = goldenRoot.value(QStringLiteral("cases")).toArray();
  require(goldenCases.size() == computed.size(),
          QStringLiteral("Snapshot case count drift: golden=%1 native=%2").arg(goldenCases.size()).arg(computed.size()));

  for (int i = 0; i < computed.size(); ++i) {
    const QJsonObject goldenCase = goldenCases.at(i).toObject();
    const QJsonObject nativeCase = computed.at(i).toObject();
    const QString id = nativeCase.value(QStringLiteral("id")).toString();
    require(goldenCase.value(QStringLiteral("id")).toString() == id, QStringLiteral("Snapshot case id order mismatch at %1").arg(i));
    const QJsonValue goldenSnaps = goldenCase.value(QStringLiteral("snapshots"));
    const QJsonValue nativeSnaps = nativeCase.value(QStringLiteral("snapshots"));
    if (goldenSnaps != nativeSnaps) {
      fail(QStringLiteral("Case %1 dagre snapshots changed — intermediate pipeline regression. "
                          "Re-run after deleting %2 if this change is intended.")
               .arg(id, snapshotPath));
    }
  }

  qDebug().noquote() << "MermaidDagreSnapshotTest:" << computed.size() << "cases × 4 phase snapshots match golden";
  return 0;
}
