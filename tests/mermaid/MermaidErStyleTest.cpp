// Verifies ER entity styling (classDef + cssClass + inline style) reaches the
// resolved scene entity via MermaidStyleResolve. ER previously used a single
// global ErSceneStyle (IMPLEMENTATION_SPEC §9 deferred classDef); this lifts
// the deferral with the family-agnostic cascade.

#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/erdiagram/ErScene.h"

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
const er::ErSceneEntity* entityById(const editor::MermaidRenderEntry& e, const QString& id) {
  require(e.erScene != nullptr, QStringLiteral("missing ER scene"));
  for (const auto& entity : e.erScene->entities)
    if (entity.id == id) return &entity;
  return nullptr;
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();

  editor::MermaidRenderCache cache;

  // classDef + cssClass: CUSTOMER gets the "red" class.
  const QString classSource = QStringLiteral(
      "erDiagram\n"
      "CUSTOMER ||--o{ ORDER : places\n"
      "classDef red fill:#ff0000,stroke:#00ff00\n"
      "cssClass CUSTOMER red");
  const auto classEntry = cache.getSync(cache.makeKey(classSource), classSource);
  require(classEntry.status == editor::MermaidRenderStatus::Ready,
          QStringLiteral("classDef ER diagram did not render: ") + classEntry.errorMessage);
  const er::ErSceneEntity* customer = entityById(classEntry, QStringLiteral("CUSTOMER"));
  require(customer, QStringLiteral("CUSTOMER entity not found"));
  require(customer->fill == QLatin1String("#ff0000"),
          QStringLiteral("classDef fill did not resolve; got ") + customer->fill);
  require(customer->stroke == QLatin1String("#00ff00"),
          QStringLiteral("classDef stroke did not resolve; got ") + customer->stroke);

  // Inline style overrides.
  const QString styleSource = QStringLiteral(
      "erDiagram\n"
      "CUSTOMER ||--o{ ORDER : places\n"
      "style CUSTOMER fill:#0000ff");
  const auto styleEntry = cache.getSync(cache.makeKey(styleSource), styleSource);
  require(styleEntry.status == editor::MermaidRenderStatus::Ready,
          QStringLiteral("inline-style ER diagram did not render"));
  const er::ErSceneEntity* styled = entityById(styleEntry, QStringLiteral("CUSTOMER"));
  require(styled, QStringLiteral("CUSTOMER entity not found (style case)"));
  require(styled->fill == QLatin1String("#0000ff"),
          QStringLiteral("inline style fill did not resolve; got ") + styled->fill);

  return 0;
}
