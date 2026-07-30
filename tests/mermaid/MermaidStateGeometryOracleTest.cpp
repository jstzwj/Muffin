// State geometry parity oracle: compares Muffin's native state::StateScene
// against real mermaid 11.16.0 stateDiagram-v2 geometry captured by
// scripts/generate_mermaid_state_geometry_fixture.mjs (headless Chrome).
//
// ASSERTS (font-independent):
//   - node count + edge count (topology)
//   - the multiset of transition tuples (from, to). This proves Muffin reproduces
//     mermaid's state machine exactly — the same transitions between the same
//     states, including branching, fork/join fan-out and convergence, self-loops,
//     and start/end ([*]) pseudo-states. (State edges carry no LS-/LE- class
//     encoding; the fixture recovers from/to by matching each edge's data-points
//     endpoints to the nearest node — node ids line up directly: root_start/
//     root_end/<name>.)
//   - per-node HEIGHT (start/end circles are a fixed radius; normal-state height
//     is label-row-driven with font ascent/descent stable across Qt/Chrome, like
//     the class/ER oracles).
//
// REPORTS (font-coupled, printed not failed): per-node WIDTH (~1px/text per-glyph
// advance, Qt-vs-Chrome). Node positions and edge paths are doubly font-coupled
// via dagre and are not captured.
//
// editor::MermaidRenderCache::getSync -> entry.scene (dynamic_cast to StateScene) -> StateScene::toJsonObject().

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/scene/ParityDiff.h"
#include "mermaid/state/StateScene.h"

#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {

// Asserted (font-independent) tolerances.
constexpr qreal kHeight = 1.5;   // state node height: fixed-radius circles + label rows
// Reported (font-coupled) — printed, not failed.
constexpr qreal kReport = 0.5;   // state node width: per-glyph advance (Qt vs Chrome)

[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "%s\n", qPrintable(message));
  std::fflush(stderr);
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

// Transition tuple key: from -> to (font-independent identity).
QString transitionTuple(const QJsonObject& edge, const char* fromKey, const char* toKey) {
  return edge.value(QLatin1String(fromKey)).toString() + QStringLiteral(" -> ") +
         edge.value(QLatin1String(toKey)).toString();
}

QStringList sortedTuples(const QJsonArray& edges, const char* fromKey, const char* toKey) {
  QStringList tuples;
  for (const QJsonValue& e : edges) tuples << transitionTuple(e.toObject(), fromKey, toKey);
  std::sort(tuples.begin(), tuples.end());
  return tuples;
}

qreal boundsHeight(const QJsonValue& node) {
  return node.toObject().value(QStringLiteral("bounds")).toObject()
      .value(QLatin1String("height")).toDouble();
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();

  require(argc == 2, QStringLiteral("Expected state geometry fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("mermaidVersion")).toString() == QLatin1String("11.16.0"),
          QStringLiteral("State geometry: mermaidVersion drifted"));
  require(root.value(QStringLiteral("oracle")).toString().startsWith(
              QLatin1String("stateDiagram.render")),
          QStringLiteral("State geometry: oracle contract drifted"));

  editor::MermaidRenderCache cache;
  for (const QJsonValue& caseValue : root.value(QStringLiteral("cases")).toArray()) {
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const auto entry = cache.getSync(cache.makeKey(source), source);
    const auto* stateScene = dynamic_cast<const state::StateScene*>(entry.scene.get());
    require(entry.status == editor::MermaidRenderStatus::Ready && stateScene != nullptr,
            id + QStringLiteral(": native state render failed: ") + entry.errorMessage);
    const QJsonObject actual = stateScene->toJsonObject();

    QStringList assertErrors;
    QStringList reportNotes;

    const QJsonArray expectedNodes = expected.value(QStringLiteral("nodes")).toArray();
    const QJsonArray actualNodes = actual.value(QStringLiteral("nodes")).toArray();
    const QJsonArray expectedEdges = expected.value(QStringLiteral("edges")).toArray();
    const QJsonArray actualEdges = actual.value(QStringLiteral("edges")).toArray();

    // Counts (font-independent topology).
    if (actualNodes.size() != expectedNodes.size())
      assertErrors << id + QStringLiteral(": node count %1 != mermaid %2")
                          .arg(actualNodes.size()).arg(expectedNodes.size());
    if (actualEdges.size() != expectedEdges.size())
      assertErrors << id + QStringLiteral(": edge count %1 != mermaid %2")
                          .arg(actualEdges.size()).arg(expectedEdges.size());

    // Transition multiset (font-independent state-machine parity). The fixture
    // uses from/to; Muffin's StateScene uses start/end.
    const QStringList expectedTuples = sortedTuples(expectedEdges, "from", "to");
    const QStringList actualTuples = sortedTuples(actualEdges, "start", "end");
    if (actualTuples != expectedTuples) {
      assertErrors << id + QStringLiteral(": transition multiset diverged");
      assertErrors << QStringLiteral("  mermaid: %1").arg(expectedTuples.join(QStringLiteral(", ")));
      assertErrors << QStringLiteral("  muffin : %1").arg(actualTuples.join(QStringLiteral(", ")));
    }

    // Node geometry, matched by id. Height is font-independent (asserted); width
    // is font-coupled (reported).
    QHash<QString, QJsonValue> expectedNodeById;
    for (const QJsonValue& n : expectedNodes)
      expectedNodeById.insert(n.toObject().value(QStringLiteral("id")).toString(), n);
    const parity::Tier heightTier{kHeight};
    for (const QJsonValue& n : actualNodes) {
      const QJsonObject node = n.toObject();
      const QString nodeId = node.value(QStringLiteral("id")).toString();
      if (!expectedNodeById.contains(nodeId)) {
        assertErrors << id + QStringLiteral(": node '%1' has no mermaid reference").arg(nodeId);
        continue;
      }
      const QJsonValue exp = expectedNodeById.value(nodeId);
      const QString prefix = id + QStringLiteral("/node/%1").arg(nodeId);
      assertErrors += parity::compareNumber(boundsHeight(n),
                                            exp.toObject().value(QStringLiteral("height")).toDouble(),
                                            heightTier, prefix + "/height");
      reportNotes += parity::compareNumber(node.value(QStringLiteral("bounds")).toObject()
                                              .value(QStringLiteral("width")).toDouble(),
                                           exp.toObject().value(QStringLiteral("width")).toDouble(),
                                           parity::Tier{kReport}, prefix + "/width");
    }

    if (!reportNotes.isEmpty()) {
      std::fprintf(stderr, "[State geometry] %s: %llu font-coupled delta(s) (reported, not asserted):\n",
                   qPrintable(id), static_cast<unsigned long long>(reportNotes.size()));
      for (const QString& note : reportNotes) std::fprintf(stderr, "  %s\n", qPrintable(note));
    }
    if (!assertErrors.isEmpty()) {
      for (const QString& e : assertErrors) std::fprintf(stderr, "%s\n", qPrintable(e));
      fail(id + QStringLiteral(": state geometry parity regression (topology/transition multiset/height)"));
    }
  }
  return 0;
}
