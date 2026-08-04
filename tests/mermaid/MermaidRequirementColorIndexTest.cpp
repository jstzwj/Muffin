// requirementDiagram colorIndex — per-node color cycling by insertion order.
//
// Commit 4 + review-fixes. The requirement DB stamps each node a `colorIndex`
// (a single counter shared across requirements THEN elements, in declaration
// order). genColor emits a CSS rule per color-id k in 0..(THEME_COLOR_LIMIT-1)
// only (default 12, configurable via themeVariables.THEME_COLOR_LIMIT); the shape
// sets data-color-id = color-(idx % borderColorArray.size()). So with
// k = idx % borderColorArray.size():
//   k < limit     -> outline+divider = borderColorArray[k], fill = bkgColorArray[k]
//                    (the fill rule only when k<bkgLen; bkg shorter/empty -> mainBkg).
//   k >= limit    -> no rule -> base (mainBkg/nodeBorder).
// Fill/outline/divider paint is resolved through a 3-layer CSS cascade
// (RequirementScene.cpp resolvePaint): user inline `style` -> genColor palette ->
// theme base, with CSS-wide keywords (inherit/initial/unset/revert/revert-layer),
// currentColor, none, and garbage each having distinct, probe-verified outcomes
// (G:/github/req-probe/step3-cascade-report.json). Text color never cycles.
//
// SOURCE-ENTRY SCOPE (review-fix 4, G:/github/req-probe/step4-source-entry-report.json):
// only the BUILT-IN redux-color/redux-dark-color palette is honored via %%{init}%%.
// User-supplied borderColorArray/bkgColorArray are IGNORED by upstream mermaid
// 11.16.0 through %%{init}%% (only the external mermaid.initialize() API honors
// them), so Muffin does NOT whitelist/consume them — that would be an extension,
// not parity. THEME_COLOR_LIMIT IS a legit %%{init}%% source config and is honored
// with full JS Number()+ceil semantics (null/absent -> default 12).
//
// Tests run the full production path (MermaidRenderCache -> RequirementDiagramAdapter
// -> RequirementScene) under frontmatter-declared themes. §1-7 built-in palette
// cycling; §8-11 the 3-layer paint cascade (invalid-under-palette, per-property
// split, no-palette contrast, full keyword matrix); §12-13 THEME_COLOR_LIMIT
// (=2 rule count, and JS Number()+ceil semantics).
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

// N requirements A..(A+N-1). DOM order == colorIndex order.
QString reqsBody(int n) {
  QString b;
  for (int i = 0; i < n; ++i)
    b += QStringLiteral("requirement %1 {\n  id: %2\n}\n").arg(QChar::fromLatin1('A' + i)).arg(i + 1);
  return b;
}

// Render under `theme` with optional themeVariables (a JSON *object* string, e.g.
// {"THEME_COLOR_LIMIT":2}). Empty tvJson -> no themeVariables block. The production
// path (frontmatter %%{init}%%) is what the preprocessor sanitizes; only legit
// source keys (e.g. THEME_COLOR_LIMIT) have effect — borderColorArray/bkgColorArray
// are upstream-ignored via %%{init}%% and are not whitelisted.
editor::MermaidRenderEntry renderThemeTvEntry(editor::MermaidRenderCache& cache,
                                              const QString& theme, const QString& tvJson,
                                              const QString& body) {
  const QString src = tvJson.isEmpty()
      ? QStringLiteral("%%{init: {\"theme\": \"%1\"}}%%\nrequirementDiagram\n%2").arg(theme, body)
      : QStringLiteral("%%{init: {\"theme\": \"%1\", \"themeVariables\": %2}}%%\nrequirementDiagram\n%3")
            .arg(theme, tvJson, body);
  auto entry = cache.getSync(cache.makeKey(src), src);
  require(entry.status == editor::MermaidRenderStatus::Ready,
          QStringLiteral("requirement under '%1' (tv=%2) did not render: %3")
              .arg(theme, tvJson, entry.errorMessage));
  return entry;
}

// Generic per-node color assertion (fill / outline / divider strokes + visibility).
void requireNodeColors(const requirement::RequirementScene* sc, const QString& id,
                       const QString& expFill, const QString& expOutline,
                       const QString& expDivider, bool outlineVisible, bool dividerVisible) {
  const auto* n = nodeById(sc, id);
  require(n != nullptr, QStringLiteral("node %1 missing").arg(id));
  require(n->fill == expFill,
          QStringLiteral("%1 fill: got %2 exp %3").arg(id, n->fill, expFill));
  require(n->outlineStroke == expOutline,
          QStringLiteral("%1 outline: got %2 exp %3").arg(id, n->outlineStroke, expOutline));
  require(n->dividerStroke == expDivider,
          QStringLiteral("%1 divider: got %2 exp %3").arg(id, n->dividerStroke, expDivider));
  require(n->outlineVisible == outlineVisible,
          QStringLiteral("%1 outlineVisible: got %2 exp %3").arg(id).arg(static_cast<int>(n->outlineVisible)).arg(static_cast<int>(outlineVisible)));
  require(n->dividerVisible == dividerVisible,
          QStringLiteral("%1 dividerVisible: got %2 exp %3").arg(id).arg(static_cast<int>(n->dividerVisible)).arg(static_cast<int>(dividerVisible)));
}

// Reference paint resolution for §11, articulating the probe contract
// (G:/github/req-probe/step3-cascade-report.json Matrix A) directly — independent
// of the implementation's layered resolver. `pal`=palette active for this node/prop
// (k=0 under redux-color); pFill/pStroke = palette[k=0]; fg/mainBkg/nodeBorder/
// divCol read from sc->style. Returns the expected resolved paint; colors that are
// don't-care (hidden outline/divider, NoBrush fill) are left empty.
struct RefPaint { bool fillNone; QString fill; QString outline; bool outVis; QString divider; bool divVis; };

// `style A fill:V` (no stroke decl): outline/divider come from L1(palette) if pal
// else L0a; fill per V (revert-layer/garbage -> palette fill if pal else foreground).
RefPaint refFill(const QString& v, bool pal, const QString& fg, const QString& mainBkg,
                 const QString& nodeBorder, const QString& divCol,
                 const QString& pFill, const QString& pStroke) {
  RefPaint r{false, {}, pal ? pStroke : nodeBorder, true, pal ? pStroke : divCol, true};
  const QString l = v.toLower();
  if (l == QStringLiteral("none")) r.fillNone = true;
  else if (l == QStringLiteral("currentcolor") || l == QStringLiteral("initial")) r.fill = QStringLiteral("#000000");
  else if (l == QStringLiteral("inherit") || l == QStringLiteral("unset") || l == QStringLiteral("revert")) r.fill = fg;
  else if (l == QStringLiteral("revert-layer") || l == QStringLiteral("notacolor")) r.fill = pal ? pFill : fg;
  else r.fill = v;  // valid color
  return r;
}
// `style A stroke:V` (no fill decl): fill = L1(palette) if pal else L0a(mainBkg);
// outline per V (inherited=none); divider per V (inherited=divCol).
RefPaint refStroke(const QString& v, bool pal, const QString& fg, const QString& mainBkg,
                   const QString& nodeBorder, const QString& divCol,
                   const QString& pFill, const QString& pStroke) {
  RefPaint r{false, pal ? pFill : mainBkg, {}, true, {}, true};
  const QString l = v.toLower();
  // outline:
  if (l == QStringLiteral("none") || l == QStringLiteral("initial") ||
      l == QStringLiteral("inherit") || l == QStringLiteral("unset") || l == QStringLiteral("revert"))
    r.outVis = false;
  else if (l == QStringLiteral("revert-layer") || l == QStringLiteral("notacolor")) { r.outline = pal ? pStroke : QString(); r.outVis = pal; }
  else if (l == QStringLiteral("currentcolor")) r.outline = QStringLiteral("#000000");
  else { r.outline = v; }  // valid color
  // divider:
  if (l == QStringLiteral("none") || l == QStringLiteral("initial")) r.divVis = false;
  else if (l == QStringLiteral("inherit") || l == QStringLiteral("unset") || l == QStringLiteral("revert")) { r.divider = divCol; }
  else if (l == QStringLiteral("revert-layer") || l == QStringLiteral("notacolor")) { r.divider = pal ? pStroke : divCol; }
  else if (l == QStringLiteral("currentcolor")) r.divider = QStringLiteral("#000000");
  else { r.divider = v; }
  return r;
}
void chkRef(const requirement::RequirementScene* sc, const QString& id, const QString& label,
            const RefPaint& e) {
  const auto* n = nodeById(sc, id);
  require(n != nullptr, QStringLiteral("%1: node missing").arg(label));
  require(n->fillNone == e.fillNone,
          QStringLiteral("%1 fillNone got %2 exp %3").arg(label).arg(n->fillNone).arg(e.fillNone));
  if (!e.fillNone) require(n->fill == e.fill, QStringLiteral("%1 fill got %2 exp %3").arg(label, n->fill, e.fill));
  require(n->outlineVisible == e.outVis,
          QStringLiteral("%1 outlineVis got %2 exp %3").arg(label).arg(n->outlineVisible).arg(e.outVis));
  if (e.outVis) require(n->outlineStroke == e.outline, QStringLiteral("%1 outline got %2 exp %3").arg(label, n->outlineStroke, e.outline));
  require(n->dividerVisible == e.divVis,
          QStringLiteral("%1 dividerVis got %2 exp %3").arg(label).arg(n->dividerVisible).arg(e.divVis));
  if (e.divVis) require(n->dividerStroke == e.divider, QStringLiteral("%1 divider got %2 exp %3").arg(label, n->dividerStroke, e.divider));
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

  // ===== 8. P1#2: invalid user `style` UNDER a palette -> palette wins =====
  {
    // 8a fill:notacolor -> palette fill #FDF4FF (NOT foreground).
    {
      const auto e = renderThemeTvEntry(cache, QStringLiteral("redux-color"), QString(),
                                        reqsBody(1) + QStringLiteral("style A fill:notacolor"));
      requireNodeColors(sceneOf(e), QStringLiteral("A"), kBkgPalette.at(0), kBorderPalette.at(0), kBorderPalette.at(0), true, true);
    }
    // 8b stroke:notacolor -> palette stroke, outline+divider BOTH visible.
    {
      const auto e = renderThemeTvEntry(cache, QStringLiteral("redux-color"), QString(),
                                        reqsBody(1) + QStringLiteral("style A stroke:notacolor"));
      requireNodeColors(sceneOf(e), QStringLiteral("A"), kBkgPalette.at(0), kBorderPalette.at(0), kBorderPalette.at(0), true, true);
    }
    // 8c both invalid on A; B untouched still cycles.
    {
      const auto e = renderThemeTvEntry(cache, QStringLiteral("redux-color"), QString(),
                                        reqsBody(2) + QStringLiteral("style A fill:notacolor,stroke:notacolor"));
      const auto* sc = sceneOf(e);
      requireNodeColors(sc, QStringLiteral("A"), kBkgPalette.at(0), kBorderPalette.at(0), kBorderPalette.at(0), true, true);
      assertCycled(sc, QStringLiteral("B"), 1, false, QString());
    }
  }

  // ===== 9. P1#2 per-property split: redux-dark-color (stroke protected, fill not) =====
  {
    const auto e = renderThemeTvEntry(cache, QStringLiteral("redux-dark-color"), QString(),
                                      reqsBody(2) + QStringLiteral("style A fill:notacolor,stroke:notacolor"));
    const auto* sc = sceneOf(e);
    // bkg empty -> fill NOT protected -> invalid fill falls to foreground; stroke
    // protected -> palette border[0], outline+divider both visible.
    requireNodeColors(sc, QStringLiteral("A"), sc->style.foregroundFallback, kBorderPalette.at(0), kBorderPalette.at(0), true, true);
    // B untouched: redux-dark-color stroke cycles, fill stays mainBkg.
    assertCycled(sc, QStringLiteral("B"), 1, true, sc->style.boxFill);
  }

  // ===== 10. P1#2 no-palette contrast: invalid inline -> foreground / hidden outline =====
  {
    // 10a fill:notacolor, no palette -> foreground (#333).
    {
      const auto e = renderThemeTvEntry(cache, QStringLiteral("default"), QString(),
                                        reqsBody(1) + QStringLiteral("style A fill:notacolor"));
      const auto* sc = sceneOf(e);
      requireNodeColors(sc, QStringLiteral("A"), sc->style.foregroundFallback, sc->style.boxStroke, sc->style.dividerColor, true, true);
    }
    // 10b stroke:notacolor, no palette -> outline HIDDEN, divider keeps theme.
    {
      const auto e = renderThemeTvEntry(cache, QStringLiteral("default"), QString(),
                                        reqsBody(1) + QStringLiteral("style A stroke:notacolor"));
      const auto* sc = sceneOf(e);
      const auto* n = nodeById(sc, QStringLiteral("A"));
      require(n != nullptr, QStringLiteral("A missing"));
      require(n->fill == sc->style.boxFill,
              QStringLiteral("no-palette invalid stroke keeps base fill; got %1 exp %2").arg(n->fill, sc->style.boxFill));
      require(!n->outlineVisible, QStringLiteral("no-palette invalid stroke hides the outline"));
      require(n->dividerVisible && n->dividerStroke == sc->style.dividerColor,
              QStringLiteral("no-palette invalid stroke: divider keeps theme color"));
    }
  }

  // ===== 11. User paint-keyword cascade: 9 values x {fill,stroke} x {default, redux-color} =====
  // The full CSS-wide-keyword + palette contract (step3-cascade-report.json Matrix A),
  // asserted against refFill/refStroke (an independent articulation of the probe).
  {
    const QStringList vals = {QStringLiteral("none"),   QStringLiteral("currentColor"),
                              QStringLiteral("inherit"), QStringLiteral("initial"),
                              QStringLiteral("unset"),   QStringLiteral("revert"),
                              QStringLiteral("revert-layer"), QStringLiteral("#00ff00"),
                              QStringLiteral("notacolor")};
    struct ThemeCfg { QString theme; bool pal; };
    for (const ThemeCfg tc : {ThemeCfg{QStringLiteral("redux-color"), true},
                              ThemeCfg{QStringLiteral("default"), false}}) {
      const auto baseE = renderThemeTvEntry(cache, tc.theme, QString(), reqsBody(1));
      const auto* base = sceneOf(baseE);
      const QString fg = base->style.foregroundFallback, mainBkg = base->style.boxFill,
                    nodeBorder = base->style.boxStroke, divCol = base->style.dividerColor;
      const QString pFill = kBkgPalette.at(0), pStroke = kBorderPalette.at(0);
      for (const QString& v : vals) {
        {  // fill
          const auto e = renderThemeTvEntry(cache, tc.theme, QString(),
                                            reqsBody(1) + QStringLiteral("style A fill:") + v);
          chkRef(sceneOf(e), QStringLiteral("A"), QStringLiteral("%1 fill:%2").arg(tc.theme, v),
                 refFill(v, tc.pal, fg, mainBkg, nodeBorder, divCol, pFill, pStroke));
        }
        {  // stroke
          const auto e = renderThemeTvEntry(cache, tc.theme, QString(),
                                            reqsBody(1) + QStringLiteral("style A stroke:") + v);
          chkRef(sceneOf(e), QStringLiteral("A"), QStringLiteral("%1 stroke:%2").arg(tc.theme, v),
                 refStroke(v, tc.pal, fg, mainBkg, nodeBorder, divCol, pFill, pStroke));
        }
      }
    }
  }

  // ===== 12. THEME_COLOR_LIMIT=2 (themeVariables path): only color-0/1 cycle =====
  // TCL is a legit %%{init}%% source config (unlike borderColorArray). genColor
  // emits min(TCL,..) rules over the BUILT-IN redux palette: with TCL=2 only k0/k1.
  {
    const QString tv = QStringLiteral("{\"THEME_COLOR_LIMIT\":2}");
    const auto e = renderThemeTvEntry(cache, QStringLiteral("redux-color"), tv, reqsBody(16));
    const auto* sc = sceneOf(e);
    const QString mainBkg = sc->style.boxFill, nodeBorder = sc->style.boxStroke, divCol = sc->style.dividerColor;
    requireNodeColors(sc, QStringLiteral("A"), kBkgPalette.at(0), kBorderPalette.at(0), kBorderPalette.at(0), true, true);  // idx0 k0<2
    requireNodeColors(sc, QStringLiteral("B"), kBkgPalette.at(1), kBorderPalette.at(1), kBorderPalette.at(1), true, true);  // idx1 k1<2
    requireNodeColors(sc, QStringLiteral("C"), mainBkg, nodeBorder, divCol, true, true);  // idx2 k2>=2 -> base
    requireNodeColors(sc, QStringLiteral("M"), kBkgPalette.at(0), kBorderPalette.at(0), kBorderPalette.at(0), true, true);  // idx12 k12%12=0<2
    requireNodeColors(sc, QStringLiteral("N"), kBkgPalette.at(1), kBorderPalette.at(1), kBorderPalette.at(1), true, true);  // idx13 k1<2
  }

  // ===== 13. THEME_COLOR_LIMIT JS Number()+ceil semantics (themeVariables path) =====
  // genColor iterates i<Number(TCL) after config merge (null/absent keep default).
  // Probe step4 rule counts: 2.5/"2.5"->3, true->1, false/"abc"/[]/[1,2]/{}->0,
  // "0x2"/"0b10"/"0o2"/[2]->2, ["2.5"]->3, null/absent->12. Built-in redux palette.
  {
    struct Case { QString lit; int rules; };
    const Case cases[] = {
        {QStringLiteral("null"), 12},   {QStringLiteral("2.5"), 3},    {QStringLiteral("\"2.5\""), 3},
        {QStringLiteral("\"0x2\""), 2}, {QStringLiteral("\"0b10\""), 2}, {QStringLiteral("\"0o2\""), 2},
        {QStringLiteral("[2]"), 2},     {QStringLiteral("[\"2.5\"]"), 3},
        {QStringLiteral("true"), 1},    {QStringLiteral("false"), 0},
        {QStringLiteral("[]"), 0},      {QStringLiteral("[1,2]"), 0},   {QStringLiteral("{}"), 0},
        {QStringLiteral("\"abc\""), 0}, {QStringLiteral("2"), 2},
    };
    for (const Case& c : cases) {
      const QString tv = QStringLiteral("{\"THEME_COLOR_LIMIT\":%1}").arg(c.lit);
      const auto e = renderThemeTvEntry(cache, QStringLiteral("redux-color"), tv, reqsBody(4));
      const auto* sc = sceneOf(e);
      const QString mb = sc->style.boxFill, nb = sc->style.boxStroke, dc = sc->style.dividerColor;
      auto chk = [&](const QString& id, int idx) {
        // k = idx % 12 (built-in 12-palette); rules <= 12 -> palette iff idx < rules.
        if (idx < c.rules)
          requireNodeColors(sc, id, kBkgPalette.at(idx), kBorderPalette.at(idx), kBorderPalette.at(idx), true, true);
        else
          requireNodeColors(sc, id, mb, nb, dc, true, true);
      };
      chk(QStringLiteral("A"), 0);  // TCL label in the require messages identifies the case
      chk(QStringLiteral("B"), 1);
      chk(QStringLiteral("C"), 2);
      chk(QStringLiteral("D"), 3);
    }
    // absent TCL -> default 12 (full cycle).
    {
      const auto e = renderThemeTvEntry(cache, QStringLiteral("redux-color"), QString(), reqsBody(1));
      requireNodeColors(sceneOf(e), QStringLiteral("A"), kBkgPalette.at(0), kBorderPalette.at(0), kBorderPalette.at(0), true, true);
    }
  }

  qDebug() << "MermaidRequirementColorIndexTest: passed";
  return 0;
}
