// Requirement geometry parity oracle: compares Muffin's native
// requirement::RequirementScene against real mermaid 11.16.0 requirementDiagram
// geometry captured by scripts/generate_mermaid_requirement_geometry_fixture.mjs
// (headless Chrome via the sibling mermaid-cli checkout, in the bundled Noto
// fonts and under the default configuration; the port measures text in the
// same fonts, so no number here depends on the machine).
//
// ASSERTS:
//   - node count + edge count (topology)
//   - the multiset of relationship edge tuples (pattern, markerStart, markerEnd,
//     relationship type) — proves all 7 relationship types (contains solid +
//     start marker; 6 dashed + end marker) render with correct arrows + labels.
//   - per-node body-row COUNT (exact — both sides agree on which fields are
//     non-empty, which is the requirementBox compartment model).
//   - per-node DIVIDER presence (exact — drawn iff body rows exist).
//   - per-node node TYPE string ("Functional Requirement", "Element", ...).
//   - per-node HEIGHT, DIVIDER position and WIDTH.
//
// editor::MermaidRenderCache::getSync -> entry.scene ->
// dynamic_cast to RequirementScene -> toJsonObject().

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/requirement/RequirementScene.h"
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

// Tolerances in px.
constexpr qreal kHeight = 0.05;
constexpr qreal kDividerY = 0.05;
constexpr qreal kWidth = 0.05;

[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "%s\n", qPrintable(message));
  std::fflush(stderr);
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition)
    fail(message);
}

// Normalize a marker field: Muffin's "" and mermaid's null both mean "no marker".
QString normMarker(const QJsonValue& v) {
  const QString s = v.toString();
  return (s.isEmpty() || s == QLatin1String("none")) ? QString() : s;
}

// Edge tuple key: pattern | start-kind | end-kind | relationship-type
// (font-independent identity — proves the full 7-type parity).
QString edgeTuple(const QJsonObject& edge) {
  return edge.value(QStringLiteral("pattern")).toString() + QLatin1Char('|') +
         normMarker(edge.value(QStringLiteral("markerStart"))) + QLatin1Char('|') +
         normMarker(edge.value(QStringLiteral("markerEnd"))) + QLatin1Char('|') +
         edge.value(QStringLiteral("type")).toString();
}

QStringList sortedTuples(const QJsonArray& edges) {
  QStringList tuples;
  for (const QJsonValue& e : edges)
    tuples << edgeTuple(e.toObject());
  std::sort(tuples.begin(), tuples.end());
  return tuples;
}

} // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();

  require(argc == 2, QStringLiteral("Expected requirement geometry fixture path"));
  QFile file(QString::fromLocal8Bit(argv[1]));
  require(file.open(QIODevice::ReadOnly), file.errorString());
  const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
  require(root.value(QStringLiteral("mermaidVersion")).toString() == QLatin1String("11.16.0"),
          QStringLiteral("Requirement geometry: mermaidVersion drifted"));
  require(root.value(QStringLiteral("oracle")).toString().startsWith(QLatin1String("requirementDiagram.render")),
          QStringLiteral("Requirement geometry: oracle contract drifted"));

  editor::MermaidRenderCache cache;
  for (const QJsonValue& caseValue : root.value(QStringLiteral("cases")).toArray()) {
    const QJsonObject fixture = caseValue.toObject();
    const QString id = fixture.value(QStringLiteral("id")).toString();
    const QString source = fixture.value(QStringLiteral("source")).toString();
    const QJsonObject expected = fixture.value(QStringLiteral("expected")).toObject();
    const auto entry = cache.getSync(cache.makeKey(source), source);
    const auto* reqScene = dynamic_cast<const requirement::RequirementScene*>(entry.scene.get());
    require(entry.status == editor::MermaidRenderStatus::Ready && reqScene != nullptr,
            id + QStringLiteral(": native requirement render failed: ") + entry.errorMessage);
    const QJsonObject actual = reqScene->toJsonObject();

    QStringList assertErrors;


    const QJsonArray expectedNodes = expected.value(QStringLiteral("nodes")).toArray();
    const QJsonArray actualNodes = actual.value(QStringLiteral("nodes")).toArray();
    const QJsonArray expectedEdges = expected.value(QStringLiteral("edges")).toArray();
    const QJsonArray actualEdges = actual.value(QStringLiteral("edges")).toArray();

    // Counts (font-independent topology).
    if (actualNodes.size() != expectedNodes.size())
      assertErrors
          << id + QStringLiteral(": node count %1 != mermaid %2").arg(actualNodes.size()).arg(expectedNodes.size());
    if (actualEdges.size() != expectedEdges.size())
      assertErrors
          << id + QStringLiteral(": edge count %1 != mermaid %2").arg(actualEdges.size()).arg(expectedEdges.size());

    // Edge-tuple multiset (font-independent arrow + label parity).
    const QStringList expectedTuples = sortedTuples(expectedEdges);
    const QStringList actualTuples = sortedTuples(actualEdges);
    if (actualTuples != expectedTuples) {
      assertErrors << id + QStringLiteral(": edge-tuple multiset diverged");
      assertErrors << QStringLiteral("  mermaid: %1").arg(expectedTuples.join(QStringLiteral(", ")));
      assertErrors << QStringLiteral("  muffin : %1").arg(actualTuples.join(QStringLiteral(", ")));
    }

    // Per-node body-row count + divider + type (font-independent, exact) +
    // height (font-coupled, asserted within tolerance), matched by id.
    QHash<QString, QJsonValue> expectedNodeById;
    for (const QJsonValue& n : expectedNodes)
      expectedNodeById.insert(n.toObject().value(QStringLiteral("id")).toString(), n);
    const parity::Tier heightTier{kHeight};
    const parity::Tier widthTier{kWidth};
    for (const QJsonValue& n : actualNodes) {
      const QJsonObject node = n.toObject();
      const QString nodeId = node.value(QStringLiteral("id")).toString();
      if (!expectedNodeById.contains(nodeId)) {
        assertErrors << id + QStringLiteral(": node '%1' has no mermaid reference").arg(nodeId);
        continue;
      }
      const QJsonObject exp = expectedNodeById.value(nodeId).toObject();
      const QString prefix = id + QStringLiteral("/node/%1").arg(nodeId);
      // Body-row count (exact — font-independent compartment model).
      const int actualRows = static_cast<int>(node.value(QStringLiteral("bodyRows")).toDouble());
      const int expectedRows = static_cast<int>(exp.value(QStringLiteral("bodyRows")).toDouble());
      if (actualRows != expectedRows)
        assertErrors << prefix + QStringLiteral("/bodyRows: muffin=%1 mermaid=%2").arg(actualRows).arg(expectedRows);
      // Divider presence (exact).
      const int actualDiv = static_cast<int>(node.value(QStringLiteral("dividers")).toDouble());
      const int expectedDiv = static_cast<int>(exp.value(QStringLiteral("dividers")).toDouble());
      if (actualDiv != expectedDiv)
        assertErrors << prefix + QStringLiteral("/dividers: muffin=%1 mermaid=%2").arg(actualDiv).arg(expectedDiv);
      // Node type string (exact).
      const QString actualType = node.value(QStringLiteral("type")).toString();
      const QString expectedType = exp.value(QStringLiteral("type")).toString();
      if (actualType != expectedType)
        assertErrors << prefix + QStringLiteral("/type: muffin='%1' mermaid='%2'").arg(actualType, expectedType);
      // Height.
      assertErrors +=
          parity::compareNumber(node.value(QStringLiteral("height")).toDouble(),
                                exp.value(QStringLiteral("height")).toDouble(), heightTier, prefix + "/height");
      // Divider Y for nodes that have a divider.
      if (exp.value(QStringLiteral("dividerY")).isDouble() && node.contains(QStringLiteral("dividerY"))) {
        const parity::Tier dividerTier{kDividerY};
        assertErrors +=
            parity::compareNumber(node.value(QStringLiteral("dividerY")).toDouble(),
                                  exp.value(QStringLiteral("dividerY")).toDouble(), dividerTier, prefix + "/dividerY");
      }
      assertErrors +=
          parity::compareNumber(node.value(QStringLiteral("width")).toDouble(),
                                exp.value(QStringLiteral("width")).toDouble(), widthTier, prefix + "/width");
    }

    if (!assertErrors.isEmpty()) {
      for (const QString& e : assertErrors)
        std::fprintf(stderr, "%s\n", qPrintable(e));
      fail(id + QStringLiteral(
                    ": requirement geometry parity regression (topology/edge tuples/body rows/dividers/type/size)"));
    }
  }
  return 0;
}
