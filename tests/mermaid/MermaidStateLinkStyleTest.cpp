// State diagrams have NO transition styling channel in 11.16.0: the grammar
// has no linkStyle production, so `linkStyle 0 stroke:red` parses as plain
// state tokens ("linkStyle", "0", and "stroke" carrying the raw remainder as
// its description — browser-verified), and edges never receive compiled
// classDef styles. This locks both inerts so the renderer cannot quietly
// grow an upstream-invisible styling path.

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/state/StateScene.h"

#include <QGuiApplication>

#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "%s\n", qPrintable(message));
  std::fflush(stderr);
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}
const state::StateSceneEdge* edgeFrom(const editor::MermaidRenderEntry& e, const QString& start) {
  const auto* stateScene = dynamic_cast<const state::StateScene*>(e.scene.get());
  require(stateScene != nullptr, QStringLiteral("missing state scene"));
  for (const auto& edge : stateScene->edges)
    if (edge.start == start) return &edge;
  return nullptr;
}
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();

  editor::MermaidRenderCache cache;

  // `linkStyle …` is not a keyword: the line becomes three plain states and
  // every transition keeps the theme transition colour — the edge paint must
  // equal a baseline render WITHOUT the line, field by field.
  const QString linkSource = QStringLiteral(
      "stateDiagram-v2\nA --> B\nC --> D\nlinkStyle 0 stroke:#ff0000,stroke-width:3px");
  const auto linkEntry = cache.getSync(cache.makeKey(linkSource), linkSource);
  require(linkEntry.status == editor::MermaidRenderStatus::Ready,
          QStringLiteral("linkStyle state diagram did not render: ") + linkEntry.errorMessage);
  const auto* linkScene = dynamic_cast<const state::StateScene*>(linkEntry.scene.get());
  require(linkScene != nullptr, QStringLiteral("missing state scene"));
  QStringList nodeIds;
  for (const auto& node : linkScene->nodes) nodeIds.append(node.id);
  for (const QString& token : {QStringLiteral("linkStyle"), QStringLiteral("0"),
                                QStringLiteral("stroke")})
    require(nodeIds.contains(token),
            QStringLiteral("linkStyle line did not become state token ") + token +
                QStringLiteral(" (got: ") + nodeIds.join(QLatin1Char(',')) +
                QStringLiteral(")"));
  const state::StateSceneNode* strokeNode = nullptr;
  for (const auto& node : linkScene->nodes)
    if (node.id == QLatin1String("stroke")) strokeNode = &node;
  // Single-description states collapse to a plain rect whose label IS the
  // description (upstream keeps description=["#ff0000,…"] as the label).
  require(strokeNode != nullptr &&
              strokeNode->label ==
                  QStringLiteral("#ff0000,stroke-width:3px"),
          QStringLiteral("stroke token did not carry the raw remainder as its label"));
  const QString baselineSource = QStringLiteral("stateDiagram-v2\nA --> B\nC --> D");
  const auto baselineEntry = cache.getSync(cache.makeKey(baselineSource), baselineSource);
  require(baselineEntry.status == editor::MermaidRenderStatus::Ready,
          QStringLiteral("baseline state diagram did not render"));
  const state::StateSceneEdge* linkEdge = edgeFrom(linkEntry, QStringLiteral("A"));
  const state::StateSceneEdge* baselineEdge = edgeFrom(baselineEntry, QStringLiteral("A"));
  require(linkEdge && baselineEdge, QStringLiteral("A->B transition not found"));
  require(linkEdge->stroke == baselineEdge->stroke &&
              linkEdge->strokeWidth == baselineEdge->strokeWidth &&
              linkEdge->strokeDasharray == baselineEdge->strokeDasharray,
          QStringLiteral("linkStyle changed state edge paint: stroke=") +
              linkEdge->stroke + QStringLiteral(" width=") + linkEdge->strokeWidth);

  // classDef never reaches state edges either (upstream only compiles class
  // styles into NODE cssCompiledStyles; edges carry just the built-in
  // "transition"/"note-edge" classes) — again field-identical to baseline.
  const QString classDefSource = QStringLiteral(
      "stateDiagram-v2\nA --> B\nclassDef transition stroke:#0000ff");
  const auto classDefEntry = cache.getSync(cache.makeKey(classDefSource), classDefSource);
  require(classDefEntry.status == editor::MermaidRenderStatus::Ready,
          QStringLiteral("classDef-transition state diagram did not render"));
  const state::StateSceneEdge* classDefEdge = edgeFrom(classDefEntry, QStringLiteral("A"));
  require(classDefEdge, QStringLiteral("A->B transition not found (classDef case)"));
  require(classDefEdge->stroke == baselineEdge->stroke &&
              classDefEdge->strokeWidth == baselineEdge->strokeWidth,
          QStringLiteral("edge classDef changed state edge paint: stroke=") +
              classDefEdge->stroke);

  return 0;
}
