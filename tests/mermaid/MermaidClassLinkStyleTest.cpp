// Verifies class-diagram edge styling (linkStyle + edge classDef) reaches the
// resolved scene edge via MermaidStyleResolve. class previously parsed no
// linkStyle and dropped edge classDef; this exercises the full
// parse -> expose -> resolve -> scene path.

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/classdiagram/ClassScene.h"
#include "mermaid/editor/MermaidRenderCache.h"

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
const classdiagram::ClassSceneEdge& firstEdge(const editor::MermaidRenderEntry& e) {
  const auto* classScene = dynamic_cast<const classdiagram::ClassScene*>(e.scene.get());
  require(classScene && !classScene->edges.isEmpty(),
          QStringLiteral("class scene has no edges"));
  return classScene->edges.constFirst();
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();

  editor::MermaidRenderCache cache;

  // linkStyle by index: stroke + stroke-width override the theme defaults.
  const QString linkSource = QStringLiteral(
      "classDiagram\n"
      "class A\n"
      "class B\n"
      "A --> B\n"
      "linkStyle 0 stroke:#ff0000,stroke-width:3px");
  const auto linkEntry = cache.getSync(cache.makeKey(linkSource), linkSource);
  require(linkEntry.status == editor::MermaidRenderStatus::Ready,
          QStringLiteral("linkStyle class diagram did not render: ") + linkEntry.errorMessage);
  const auto& linkEdge = firstEdge(linkEntry);
  require(linkEdge.stroke == QLatin1String("#ff0000"),
          QStringLiteral("linkStyle stroke did not resolve; got ") + linkEdge.stroke);
  require(linkEdge.strokeWidth == QLatin1String("3px"),
          QStringLiteral("linkStyle stroke-width did not resolve; got ") + linkEdge.strokeWidth);

  // Edge classDef: edges carry the built-in "relation" class, so a `classDef
  // relation ...` reaches every edge.
  const QString classDefSource = QStringLiteral(
      "classDiagram\n"
      "class A\n"
      "class B\n"
      "A --> B\n"
      "classDef relation stroke:#0000ff");
  const auto classDefEntry = cache.getSync(cache.makeKey(classDefSource), classDefSource);
  require(classDefEntry.status == editor::MermaidRenderStatus::Ready,
          QStringLiteral("classDef-relation class diagram did not render"));
  const auto& classDefEdge = firstEdge(classDefEntry);
  require(classDefEdge.stroke == QLatin1String("#0000ff"),
          QStringLiteral("edge classDef stroke did not resolve; got ") + classDefEdge.stroke);

  return 0;
}
