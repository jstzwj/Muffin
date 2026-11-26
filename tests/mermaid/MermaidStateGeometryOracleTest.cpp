// State geometry parity oracle: compares Muffin's native state::StateScene
// against real mermaid 11.16.0 stateDiagram-v2 geometry captured by
// scripts/generate_mermaid_state_geometry_fixture.mjs (headless Chrome, in the
// bundled Noto fonts, which is also what the port measures text in, so no
// number here depends on the machine's fonts).
//
// ASSERTS:
//   - node count + edge count (topology)
//   - the multiset of transition tuples (from, to). This proves Muffin reproduces
//     mermaid's state machine exactly — the same transitions between the same
//     states, including branching, fork/join fan-out and convergence, self-loops,
//     and start/end ([*]) pseudo-states. The fixture takes each transition's
//     ends from the state DB's getData(), the logical ends the renderer hands
//     the layout, read by the id of the path it drew (node ids line up
//     directly: root_start/root_end/<name>).
//   - per-node HEIGHT (start/end circles are a fixed radius; normal-state height
//     is label-row-driven with font ascent/descent stable across Qt/Chrome, like
//     the class/ER oracles).
//   - per-node WIDTH (text measured in the same font).
// Node positions and edge paths are not captured.
//
// editor::MermaidRenderCache::getSync -> entry.scene (dynamic_cast to StateScene) -> StateScene::toJsonObject().

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/scene/ParityDiff.h"
#include "mermaid/state/StateScene.h"

#include <QCryptographicHash>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {

// Tolerances in px.
constexpr qreal kHeight = 1.5; // state node height: fixed-radius circles + label rows
constexpr qreal kWidth = 0.05;  // state node width: text measured in the same font

QStringList failures;

void require(bool condition, const QString& message) {
  if (!condition)
    failures << message;
}

// Transition tuple key: from -> to (font-independent identity).
QString transitionTuple(const QJsonObject& edge, const char* fromKey, const char* toKey) {
  return edge.value(QLatin1String(fromKey)).toString() + QStringLiteral(" -> ") +
         edge.value(QLatin1String(toKey)).toString();
}

QStringList sortedTuples(const QJsonArray& edges, const char* fromKey, const char* toKey) {
  QStringList tuples;
  for (const QJsonValue& e : edges)
    tuples << transitionTuple(e.toObject(), fromKey, toKey);
  std::sort(tuples.begin(), tuples.end());
  return tuples;
}

qreal boundsHeight(const QJsonValue& node) {
  return node.toObject().value(QStringLiteral("bounds")).toObject().value(QLatin1String("height")).toDouble();
}

} // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();

  require(argc == 2, QStringLiteral("Expected state geometry fixture path"));
  if (argc != 2) {
    for (const QString& failure : failures)
      std::fprintf(stderr, "FAIL: %s\n", qPrintable(failure));
    return 1;
  }
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  QByteArray bytes = file.readAll();
  // The generator emits LF; a CRLF checkout must not change the provenance.
  bytes.replace("\r\n", "\n");
  require(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() ==
              QByteArrayLiteral("284986ded2183b5408375d2cef4dc9729a297acd6773bca0400a184751fd91eb"),
          QStringLiteral("State geometry fixture bytes changed"));
  const QJsonObject root = QJsonDocument::fromJson(bytes).object();
  require(root.value(QStringLiteral("mermaidVersion")).toString() == QLatin1String("11.16.0"),
          QStringLiteral("State geometry: mermaidVersion drifted"));
  require(root.value(QStringLiteral("oracle")).toString().startsWith(QLatin1String("stateDiagram.render")),
          QStringLiteral("State geometry: oracle contract drifted"));
  const QJsonArray cases = root.value(QStringLiteral("cases")).toArray();
  require(cases.size() == 15, QStringLiteral("State geometry case count drifted: %1").arg(cases.size()));

  editor::MermaidRenderCache cache;
  for (const QJsonValue& caseValue : cases) {
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const auto entry = cache.getSync(cache.makeKey(source), source);
    const auto* stateScene = dynamic_cast<const state::StateScene*>(entry.scene.get());
    if (!(entry.status == editor::MermaidRenderStatus::Ready && stateScene != nullptr)) {
      failures << id + QStringLiteral(": native state render failed: ") + entry.errorMessage;
      continue;
    }
    const QJsonObject actual = stateScene->toJsonObject();
    QStringList divergences;

    const QJsonArray expectedNodes = expected.value(QStringLiteral("nodes")).toArray();
    const QJsonArray actualNodes = actual.value(QStringLiteral("nodes")).toArray();
    const QJsonArray expectedEdges = expected.value(QStringLiteral("edges")).toArray();
    const QJsonArray actualEdges = actual.value(QStringLiteral("edges")).toArray();

    // Counts (font-independent topology).
    if (actualNodes.size() != expectedNodes.size())
      divergences << QStringLiteral("%1: %2 nodes, upstream %3")
                         .arg(id)
                         .arg(actualNodes.size())
                         .arg(expectedNodes.size());
    if (actualEdges.size() != expectedEdges.size())
      divergences << QStringLiteral("%1: %2 transitions, upstream %3")
                         .arg(id)
                         .arg(actualEdges.size())
                         .arg(expectedEdges.size());

    // Transition multiset (font-independent state-machine parity). The fixture
    // uses from/to; Muffin's StateScene uses start/end.
    const QStringList expectedTuples = sortedTuples(expectedEdges, "from", "to");
    const QStringList actualTuples = sortedTuples(actualEdges, "start", "end");
    if (actualTuples != expectedTuples)
      divergences << QStringLiteral("%1: transitions differ\n  upstream: %2\n  native : %3")
                         .arg(id, expectedTuples.join(QStringLiteral(", ")), actualTuples.join(QStringLiteral(", ")));

    // Node geometry, matched by id.
    QHash<QString, QJsonValue> expectedNodeById;
    for (const QJsonValue& n : expectedNodes)
      expectedNodeById.insert(n.toObject().value(QStringLiteral("id")).toString(), n);
    const parity::Tier heightTier{kHeight};
    const parity::Tier widthTier{kWidth};
    for (const QJsonValue& n : actualNodes) {
      const QJsonObject node = n.toObject();
      const QString nodeId = node.value(QStringLiteral("id")).toString();
      if (!expectedNodeById.contains(nodeId)) {
        divergences << id + QStringLiteral(": node '%1' has no mermaid reference").arg(nodeId);
        continue;
      }
      const QJsonValue exp = expectedNodeById.value(nodeId);
      const QString prefix = id + QStringLiteral("/node/%1").arg(nodeId);
      divergences += parity::compareNumber(boundsHeight(n), exp.toObject().value(QStringLiteral("height")).toDouble(),
                                           heightTier, prefix + QStringLiteral("/height"));
      divergences += parity::compareNumber(
          node.value(QStringLiteral("bounds")).toObject().value(QStringLiteral("width")).toDouble(),
          exp.toObject().value(QStringLiteral("width")).toDouble(), widthTier, prefix + QStringLiteral("/width"));
    }

    if (!divergences.isEmpty()) {
      std::fprintf(stderr, "[%s]\n", qPrintable(id));
      failures += divergences;
    }
  }

  for (const QString& failure : failures)
    std::fprintf(stderr, "FAIL: %s\n", qPrintable(failure));
  if (!failures.isEmpty()) {
    std::fprintf(stderr, "MermaidStateGeometryOracleTest: %lld divergence(s) from Mermaid 11.16.0\n",
                 static_cast<long long>(failures.size()));
    return 1;
  }
  std::printf("MermaidStateGeometryOracleTest passed: %lld cases, every transition and node size within %0.2f px\n",
              static_cast<long long>(cases.size()), kWidth);
  return 0;
}
