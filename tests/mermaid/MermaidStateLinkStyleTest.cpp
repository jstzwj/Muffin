// Verifies state-diagram transition styling (linkStyle + edge classDef) reaches
// the resolved scene edge via MermaidStyleResolve. State edges previously
// hard-coded `fill:none` with a global transition colour; this exercises the
// parse -> resolve -> scene path (mirroring the class linkStyle wire).

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
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();

  editor::MermaidRenderCache cache;

  // linkStyle by index: edge 0 (A->B) stroke + width override the theme.
  const QString linkSource = QStringLiteral(
      "stateDiagram-v2\nA --> B\nC --> D\nlinkStyle 0 stroke:#ff0000,stroke-width:3px");
  const auto linkEntry = cache.getSync(cache.makeKey(linkSource), linkSource);
  require(linkEntry.status == editor::MermaidRenderStatus::Ready,
          QStringLiteral("linkStyle state diagram did not render: ") + linkEntry.errorMessage);
  const state::StateSceneEdge* linkEdge = edgeFrom(linkEntry, QStringLiteral("A"));
  require(linkEdge, QStringLiteral("A->B transition not found"));
  require(linkEdge->stroke == QLatin1String("#ff0000"),
          QStringLiteral("linkStyle stroke did not resolve; got ") + linkEdge->stroke);
  require(linkEdge->strokeWidth == QLatin1String("3px"),
          QStringLiteral("linkStyle stroke-width did not resolve; got ") + linkEdge->strokeWidth);

  // Edge classDef: transitions carry the built-in "transition" class, so a
  // `classDef transition ...` reaches every transition.
  const QString classDefSource = QStringLiteral(
      "stateDiagram-v2\nA --> B\nclassDef transition stroke:#0000ff");
  const auto classDefEntry = cache.getSync(cache.makeKey(classDefSource), classDefSource);
  require(classDefEntry.status == editor::MermaidRenderStatus::Ready,
          QStringLiteral("classDef-transition state diagram did not render"));
  const state::StateSceneEdge* classDefEdge = edgeFrom(classDefEntry, QStringLiteral("A"));
  require(classDefEdge, QStringLiteral("A->B transition not found (classDef case)"));
  require(classDefEdge->stroke == QLatin1String("#0000ff"),
          QStringLiteral("edge classDef stroke did not resolve; got ") + classDefEdge->stroke);

  return 0;
}
