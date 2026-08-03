// requirementDiagram colorIndex — per-node color cycling by insertion order.
//
// Commit 4. The requirement DB stamps each node a `colorIndex` (a single counter
// shared across requirements THEN elements, in declaration order). That index
// becomes color ONLY when the active theme provides a `borderColorArray`
// (chunk-CHAKFXHA.mjs): just `redux-color` (border+bkg) and `redux-dark-color`
// (border only, bkg empty -> fill falls back to mainBkg). All 9 other themes are
// inert. Verified against real mermaid 11.16.0 (G:/github/req-probe/
// step1c-colorindex-redux-report.json):
//   fill      = bkgColorArray[idx]            (redux-color) / mainBkg (redux-dark-color)
//   outline   = borderColorArray[idx]         (user `stroke` wins)
//   divider   = borderColorArray[idx]         (genColor `.node path` beats `.divider`)
//   text      = theme textColor               (NEVER cycles)
//   idx % 12  wraps for >12 nodes
//
// Two layers: (1) FlowTheme palette population (resolveFlowTheme ReduxColor /
// ReduxDarkColor array contents + the 9 inert themes), (2) the full production
// render path (MermaidRenderCache -> RequirementDiagramAdapter -> RequirementScene)
// under frontmatter-declared themes, asserting resolved node fill/outline/divider.
#include "mermaid/MermaidFontRegistry.h"
#include "mermaid/editor/MermaidRenderCache.h"
#include "mermaid/requirement/RequirementScene.h"
#include "mermaid/theme/FlowTheme.h"

#include <QColor>
#include <QGuiApplication>
#include <QSet>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <cstdlib>

using namespace muffin::mermaid;

namespace {
[[noreturn]] void fail(const QString& message) {
  std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
  std::fflush(stderr);
  std::exit(1);
}
void require(bool condition, const QString& message) {
  if (!condition) fail(message);
}

// The exact Tailwind palettes (chunk-CHAKFXHA.mjs:3936-3987); upstream's
// "#A3E635 " trailing space is trimmed (renders identically).
const QStringList kBorderPalette = {
    QStringLiteral("#E879F9"), QStringLiteral("#2DD4BF"), QStringLiteral("#FB923C"),
    QStringLiteral("#22D3EE"), QStringLiteral("#4ADE80"), QStringLiteral("#A78BFA"),
    QStringLiteral("#F87171"), QStringLiteral("#FACC15"), QStringLiteral("#818CF8"),
    QStringLiteral("#A3E635"), QStringLiteral("#38BDF8"), QStringLiteral("#FB7185")};
const QStringList kBkgPalette = {
    QStringLiteral("#FDF4FF"), QStringLiteral("#F0FDFA"), QStringLiteral("#FFF7ED"),
    QStringLiteral("#ECFEFF"), QStringLiteral("#F0FDF4"), QStringLiteral("#F5F3FF"),
    QStringLiteral("#FEF2F2"), QStringLiteral("#FEFCE8"), QStringLiteral("#EEF2FF"),
    QStringLiteral("#F7FEE7"), QStringLiteral("#F0F9FF"), QStringLiteral("#FFF1F2")};

const requirement::RequirementSceneNode* nodeById(const requirement::RequirementScene* sc,
                                                   const QString& id) {
  for (const auto& n : sc->nodes)
    if (n.id == id) return &n;
  return nullptr;
}

// Render a requirementDiagram body under `theme` via the production cache path
// (frontmatter-declared theme). The returned entry keeps the scene alive.
editor::MermaidRenderEntry renderThemeEntry(editor::MermaidRenderCache& cache,
                                            const QString& theme, const QString& body) {
  const QString src =
      QStringLiteral("%%{init: {\"theme\": \"%1\"}}%%\nrequirementDiagram\n%2").arg(theme, body);
  auto entry = cache.getSync(cache.makeKey(src), src);
  require(entry.status == editor::MermaidRenderStatus::Ready,
          QStringLiteral("requirement under '%1' did not render: %2").arg(theme, entry.errorMessage));
  return entry;
}

const requirement::RequirementScene* sceneOf(const editor::MermaidRenderEntry& e) {
  const auto* sc = dynamic_cast<const requirement::RequirementScene*>(e.scene.get());
  require(sc != nullptr, QStringLiteral("entry is not a Requirement scene"));
  return sc;
}

// 3 requirements (A,B,C) + 2 elements (D,E). colorIndex order: A=0 B=1 C=2 D=3 E=4.
QString abcdeBody() {
  return QStringLiteral(
      "requirement A {\n  id: 1\n}\n"
      "requirement B {\n  id: 2\n}\n"
      "requirement C {\n  id: 3\n}\n"
      "element D {\n  type: t\n}\n"
      "element E {\n  type: t\n}\n");
}

// Assert node `id` (at colorIndex `idx`) cycled to palette[k], k = idx % 12.
void assertCycled(const requirement::RequirementScene* sc, const QString& id, int idx,
                  bool bkgEmpty, const QString& mainBkg) {
  const int k = idx % 12;
  const auto* n = nodeById(sc, id);
  require(n != nullptr, QStringLiteral("node '%1' missing").arg(id));
  const QString expFill = bkgEmpty ? mainBkg : kBkgPalette.at(k);
  require(n->fill == expFill,
          QStringLiteral("redux %1 fill idx=%2: got %3 exp %4").arg(id).arg(idx).arg(n->fill, expFill));
  require(n->outlineStroke == kBorderPalette.at(k),
          QStringLiteral("redux %1 outline idx=%2: got %3 exp %4")
              .arg(id).arg(idx).arg(n->outlineStroke, kBorderPalette.at(k)));
  require(n->dividerStroke == kBorderPalette.at(k),
          QStringLiteral("redux %1 divider idx=%2: got %3 exp %4")
              .arg(id).arg(idx).arg(n->dividerStroke, kBorderPalette.at(k)));
  require(n->outlineVisible && n->dividerVisible,
          QStringLiteral("redux %1 outline+divider both visible").arg(id));
}
}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  MermaidFontRegistry::ensureLoaded();
  editor::MermaidRenderCache cache;

  // ===== 1. FlowTheme palette population (the source of truth for the arrays) =====
  {
    const auto rc = flowtheme::resolveFlowTheme(flowtheme::FlowThemeId::ReduxColor);
    require(rc.borderColorArray == kBorderPalette,
            QStringLiteral("redux-color borderColorArray = 12 Tailwind *-400"));
    require(rc.bkgColorArray == kBkgPalette,
            QStringLiteral("redux-color bkgColorArray = 12 Tailwind *-50"));
    const auto rdc = flowtheme::resolveFlowTheme(flowtheme::FlowThemeId::ReduxDarkColor);
    require(rdc.borderColorArray == kBorderPalette,
            QStringLiteral("redux-dark-color borderColorArray = same 12 *-400"));
    require(rdc.bkgColorArray.isEmpty(),
            QStringLiteral("redux-dark-color bkgColorArray is EMPTY (fill falls back to mainBkg)"));
    // The 9 other themes carry no palette -> colorIndex is inert for them.
    for (flowtheme::FlowThemeId id : {flowtheme::FlowThemeId::Default, flowtheme::FlowThemeId::Dark,
                                      flowtheme::FlowThemeId::Base, flowtheme::FlowThemeId::Forest,
                                      flowtheme::FlowThemeId::Neutral, flowtheme::FlowThemeId::Neo,
                                      flowtheme::FlowThemeId::NeoDark, flowtheme::FlowThemeId::Redux,
                                      flowtheme::FlowThemeId::ReduxDark}) {
      const auto t = flowtheme::resolveFlowTheme(id);
      require(t.borderColorArray.isEmpty() && t.bkgColorArray.isEmpty(),
              QStringLiteral("theme %1 has no colorIndex palette (inert)").arg(static_cast<int>(id)));
    }
  }

  // ===== 2. redux-color: per-node fill/outline/divider cycle by insertion order =====
  {
    const auto e = renderThemeEntry(cache, QStringLiteral("redux-color"), abcdeBody());
    const auto* sc = sceneOf(e);
    // Requirements first (idx 0,1,2), then elements continuing (idx 3,4) — the
    // shared counter is the key behavior (elements do NOT restart at 0).
    assertCycled(sc, QStringLiteral("A"), 0, false, QString());
    assertCycled(sc, QStringLiteral("B"), 1, false, QString());
    assertCycled(sc, QStringLiteral("C"), 2, false, QString());
    assertCycled(sc, QStringLiteral("D"), 3, false, QString());
    assertCycled(sc, QStringLiteral("E"), 4, false, QString());
  }

  // ===== 3. redux-dark-color: empty bkgColorArray -> fill stays mainBkg, stroke cycles =====
  {
    const auto e = renderThemeEntry(cache, QStringLiteral("redux-dark-color"), abcdeBody());
    const auto* sc = sceneOf(e);
    const QString mainBkg = sc->style.boxFill;  // adapter sets boxFill = themeVars.mainBkg
    require(!mainBkg.isEmpty(), QStringLiteral("redux-dark-color mainBkg should be set"));
    assertCycled(sc, QStringLiteral("A"), 0, true, mainBkg);
    assertCycled(sc, QStringLiteral("B"), 1, true, mainBkg);
    assertCycled(sc, QStringLiteral("C"), 2, true, mainBkg);
    assertCycled(sc, QStringLiteral("D"), 3, true, mainBkg);
    assertCycled(sc, QStringLiteral("E"), 4, true, mainBkg);
  }

  // ===== 4. default theme (no palette): every node identical, no cycling =====
  {
    const auto e = renderThemeEntry(cache, QStringLiteral("default"), abcdeBody());
    const auto* sc = sceneOf(e);
    for (const auto& id : {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"),
                           QStringLiteral("D"), QStringLiteral("E")}) {
      const auto* n = nodeById(sc, id);
      require(n != nullptr, QStringLiteral("node %1 missing").arg(id));
      require(n->fill == sc->style.boxFill,
              QStringLiteral("default %1 fill = theme boxFill (no cycle); got %2 exp %3")
                  .arg(id, n->fill, sc->style.boxFill));
      require(n->outlineStroke == sc->style.boxStroke,
              QStringLiteral("default %1 outline = theme boxStroke").arg(id));
      require(n->dividerStroke == sc->style.dividerColor,
              QStringLiteral("default %1 divider = theme dividerColor").arg(id));
    }
    // Distinct fills across nodes == 1 (proves no cycling).
    QSet<QString> fills;
    for (const auto& n : sc->nodes) fills.insert(n.fill);
    require(fills.size() == 1, QStringLiteral("default theme: 1 distinct fill (no cycle); got %1").arg(fills.size()));
  }

  // ===== 5. User `style` wins over the palette (fill+outline+divider) =====
  {
    const QString body = abcdeBody() + QStringLiteral("style A stroke:#000000,fill:#123456");
    const auto e = renderThemeEntry(cache, QStringLiteral("redux-color"), body);
    const auto* sc = sceneOf(e);
    const auto* a = nodeById(sc, QStringLiteral("A"));
    require(a != nullptr, QStringLiteral("A missing"));
    require(a->fill == QStringLiteral("#123456"),
            QStringLiteral("user fill wins over palette; got %1").arg(a->fill));
    require(a->outlineStroke == QStringLiteral("#000000"),
            QStringLiteral("user stroke wins on outline; got %1").arg(a->outlineStroke));
    require(a->dividerStroke == QStringLiteral("#000000"),
            QStringLiteral("user stroke wins on divider; got %1").arg(a->dividerStroke));
    // Untouched nodes still cycle the palette.
    assertCycled(sc, QStringLiteral("B"), 1, false, QString());
    assertCycled(sc, QStringLiteral("E"), 4, false, QString());
  }

  // ===== 6. Wrap: colorIndex % borderColorArray.size() (>12 nodes reuse palette[0]) =====
  {
    // 13 requirements A..M -> node M is idx 12 -> k = 0 -> palette[0].
    QString body;
    for (char c = 'A'; c <= 'M'; ++c)
      body += QStringLiteral("requirement %1 {\n  id: %1\n}\n").arg(QChar(c));
    const auto e = renderThemeEntry(cache, QStringLiteral("redux-color"), body);
    const auto* sc = sceneOf(e);
    assertCycled(sc, QStringLiteral("A"), 0, false, QString());   // idx 0
    assertCycled(sc, QStringLiteral("M"), 12, false, QString());  // idx 12 -> k 0
    // And a mid-palette node (idx 5) for good measure.
    assertCycled(sc, QStringLiteral("F"), 5, false, QString());
  }

  // ===== 7. Text color never cycles (palette affects box/divider only) =====
  {
    const auto e = renderThemeEntry(cache, QStringLiteral("redux-color"), abcdeBody());
    const auto* sc = sceneOf(e);
    for (const auto& n : sc->nodes) {
      for (const auto& row : n.rows) {
        // No node declares `color:` -> every row color is invalid (falls back to
        // theme textColor at paint). If colorIndex leaked into text, rows would
        // carry per-node colors here.
        require(!row.color.isValid(),
                QStringLiteral("text color not cycled by palette (node %1 row invalid/default)")
                    .arg(n.id));
      }
    }
  }

  qDebug() << "MermaidRequirementColorIndexTest: passed";
  return 0;
}
