// Class geometry parity oracle: compares Muffin's native
// classdiagram::ClassScene against real mermaid 11.16.0 classDiagram geometry
// captured by scripts/generate_mermaid_class_geometry_fixture.mjs (headless
// Chrome via the sibling mermaid-cli checkout).
//
// ASSERTS (font-independent):
//   - node count + edge count (topology)
//   - the multiset of relationship edge tuples (pattern, markerStart, markerEnd)
//   - per-node HEIGHT (compartment row model: font ascent/descent is stable
//     across Qt/Chrome — like the ER oracle's entity-height parity)
//   - per-node compartment DIVIDER count (exact — both sides reserve the same
//     header|members|methods structure, even for bare classes).
// The edge-tuple multiset proves Muffin draws exactly mermaid's arrows for every
// class relationship type (inheritance/realization/composition/aggregation/
// association/dependency, solid/dashed, start/end/both) — the class analogue of
// the ER oracle's cardinality assertion.
//
// REPORTS (font-coupled, printed not failed): per-node WIDTH. Width depends on
// per-glyph advance widths, which differ between Qt and Chrome by ~1px/text (a
// milder form of the same font-coupling that limits the pixel goldens). Positions
// (cx/cy) are doubly font-coupled via dagre and are not captured.
//
// Marker normalization: Muffin emits markerStart="none" for marker-less ends;
// mermaid emits null. Both normalize to empty for the tuple comparison.
//
// editor::MermaidRenderCache::getSync -> entry.classScene -> ClassScene::toJsonObject().

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/classdiagram/ClassScene.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/scene/ParityDiff.h"

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
constexpr qreal kHeight = 1.5;   // class node height: compartment row-model parity
// Reported (font-coupled) — printed, not failed.
constexpr qreal kReport = 0.5;   // class node width: per-glyph advance (Qt vs Chrome)

[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "%s\n", qPrintable(message));
  std::fflush(stderr);
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

// Normalize a marker field to a canonical token: Muffin's "none" and mermaid's
// null both mean "no marker at this end".
QString normMarker(const QJsonValue& v) {
  const QString s = v.toString();
  return (s.isEmpty() || s == QLatin1String("none")) ? QString() : s;
}

// Edge tuple key: pattern | start-kind | end-kind (font-independent identity).
QString edgeTuple(const QJsonObject& edge) {
  return edge.value(QStringLiteral("pattern")).toString() + QLatin1Char('|') +
         normMarker(edge.value(QStringLiteral("markerStart"))) + QLatin1Char('|') +
         normMarker(edge.value(QStringLiteral("markerEnd")));
}

QStringList sortedTuples(const QJsonArray& edges) {
  QStringList tuples;
  for (const QJsonValue& e : edges) tuples << edgeTuple(e.toObject());
  std::sort(tuples.begin(), tuples.end());
  return tuples;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();

  require(argc == 2, QStringLiteral("Expected class geometry fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("mermaidVersion")).toString() == QLatin1String("11.16.0"),
          QStringLiteral("Class geometry: mermaidVersion drifted"));
  require(root.value(QStringLiteral("oracle")).toString().startsWith(
              QLatin1String("classDiagram.render")),
          QStringLiteral("Class geometry: oracle contract drifted"));

  editor::MermaidRenderCache cache;
  for (const QJsonValue& caseValue : root.value(QStringLiteral("cases")).toArray()) {
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const auto entry = cache.getSync(cache.makeKey(source), source);
    require(entry.status == editor::MermaidRenderStatus::Ready && entry.classScene,
            id + QStringLiteral(": native class render failed: ") + entry.errorMessage);
    const QJsonObject actual = entry.classScene->toJsonObject();

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

    // Edge-tuple multiset (font-independent arrow parity).
    const QStringList expectedTuples = sortedTuples(expectedEdges);
    const QStringList actualTuples = sortedTuples(actualEdges);
    if (actualTuples != expectedTuples) {
      assertErrors << id + QStringLiteral(": edge-tuple multiset diverged");
      assertErrors << QStringLiteral("  mermaid: %1").arg(expectedTuples.join(QStringLiteral(", ")));
      assertErrors << QStringLiteral("  muffin : %1").arg(actualTuples.join(QStringLiteral(", ")));
    }

    // Node geometry + compartment structure (font-coupled / model-dependent) —
    // reported, matched by id.
    // Node geometry + compartment structure, matched by id.
    QHash<QString, QJsonValue> expectedNodeById;
    for (const QJsonValue& n : expectedNodes)
      expectedNodeById.insert(n.toObject().value(QStringLiteral("id")).toString(), n);
    const parity::Tier reportTier{kReport};
    const parity::Tier heightTier{kHeight};
    for (const QJsonValue& n : actualNodes) {
      const QJsonObject node = n.toObject();
      const QString nodeId = node.value(QStringLiteral("id")).toString();
      if (!expectedNodeById.contains(nodeId)) {
        assertErrors << id + QStringLiteral(": node '%1' has no mermaid reference").arg(nodeId);
        continue;
      }
      const QJsonObject exp = expectedNodeById.value(nodeId).toObject();
      const QString prefix = id + QStringLiteral("/node/%1").arg(nodeId);
      // Height + divider count are font-independent (asserted).
      assertErrors += parity::compareNumber(node.value(QStringLiteral("height")).toDouble(),
                                            exp.value(QStringLiteral("height")).toDouble(),
                                            heightTier, prefix + "/height");
      const int actualDiv = static_cast<int>(node.value(QStringLiteral("dividers")).toDouble());
      const int expectedDiv = static_cast<int>(exp.value(QStringLiteral("dividers")).toDouble());
      if (actualDiv != expectedDiv)
        assertErrors << prefix + QStringLiteral("/dividers: muffin=%1 mermaid=%2")
                            .arg(actualDiv).arg(expectedDiv);
      // Width is font-coupled (reported only).
      reportNotes += parity::compareNumber(node.value(QStringLiteral("width")).toDouble(),
                                           exp.value(QStringLiteral("width")).toDouble(),
                                           reportTier, prefix + "/width");
    }

    if (!reportNotes.isEmpty()) {
      std::fprintf(stderr, "[Class geometry] %s: %llu font-coupled delta(s) (reported, not asserted):\n",
                   qPrintable(id), static_cast<unsigned long long>(reportNotes.size()));
      for (const QString& note : reportNotes) std::fprintf(stderr, "  %s\n", qPrintable(note));
    }
    if (!assertErrors.isEmpty()) {
      for (const QString& e : assertErrors) std::fprintf(stderr, "%s\n", qPrintable(e));
      fail(id + QStringLiteral(": class geometry parity regression (topology/edge tuples/height/dividers)"));
    }
  }
  return 0;
}
