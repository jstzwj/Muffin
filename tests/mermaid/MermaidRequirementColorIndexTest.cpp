// requirementDiagram colorIndex — per-node color cycling by insertion order.
//
// Commit 4 + review-fix. The requirement DB stamps each node a `colorIndex`
// (a single counter shared across requirements THEN elements, in declaration
// order). genColor emits a CSS rule per color-id k in 0..11 (THEME_COLOR_LIMIT)
// only; the shape sets data-color-id = color-(idx % borderLen). So:
//   k = idx % borderColorArray.size()
//   k < 12        -> outline+divider = borderColorArray[k], fill = bkgColorArray[k]
//                   (each gated on its own entry existing+parsing; bkg shorter/
//                   empty -> fill mainBkg). Verified vs mermaid 11.16.0
//                   (G:/github/req-probe/step2-colorindex-full-report.json).
//   k >= 12       -> no rule -> base (mainBkg/nodeBorder).
//   user `style`  -> wins; an INVALID value the browser drops falls back to the
//                   PALETTE color when a rule exists (fillFromPalette/strokeFromPalette),
//                   else to foreground fill / hidden outline.
// A user themeVariables.borderColorArray/bkgColorArray REPLACES the built-in
// (or, empty, clears it); a custom array activates colorIndex under any theme.
// Text color never cycles.
//
// Two layers: (1) FlowTheme palette population (resolveFlowTheme ReduxColor /
// ReduxDarkColor array contents + the 9 inert themes), (2) the full production
// render path (MermaidRenderCache -> RequirementDiagramAdapter -> RequirementScene)
// under frontmatter-declared themes, asserting resolved node fill/outline/divider.
// Sections 8-18 cover the review-fix: structured array pathway + palette-aware
// per-property invalid fallback.
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
// {"borderColorArray":[...]}). Empty tvJson -> no themeVariables block. The
// production path (frontmatter init) is what the preprocessor sanitizes, so this
// exercises the whitelist + arrayOverride pathway end to end.
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

  // ===== 8. Custom array activates under `default` (cycles %3, wraps) =====
  {
    const QString tv = QStringLiteral(
        "{\"borderColorArray\":[\"#ff0000\",\"#00ff00\",\"#0000ff\"],"
        "\"bkgColorArray\":[\"#100000\",\"#001000\",\"#000010\"]}");
    const auto e = renderThemeTvEntry(cache, QStringLiteral("default"), tv, reqsBody(5));
    const auto* sc = sceneOf(e);
    requireNodeColors(sc, QStringLiteral("A"), QStringLiteral("#100000"), QStringLiteral("#ff0000"), QStringLiteral("#ff0000"), true, true);  // k0
    requireNodeColors(sc, QStringLiteral("B"), QStringLiteral("#001000"), QStringLiteral("#00ff00"), QStringLiteral("#00ff00"), true, true);  // k1
    requireNodeColors(sc, QStringLiteral("C"), QStringLiteral("#000010"), QStringLiteral("#0000ff"), QStringLiteral("#0000ff"), true, true);  // k2
    requireNodeColors(sc, QStringLiteral("D"), QStringLiteral("#100000"), QStringLiteral("#ff0000"), QStringLiteral("#ff0000"), true, true);  // k3%3=0
    requireNodeColors(sc, QStringLiteral("E"), QStringLiteral("#001000"), QStringLiteral("#00ff00"), QStringLiteral("#00ff00"), true, true);  // k1
  }

  // ===== 9. Empty borderColorArray CLEARS the redux built-in (inert) =====
  {
    const QString tv = QStringLiteral("{\"borderColorArray\":[]}");
    const auto e = renderThemeTvEntry(cache, QStringLiteral("redux-color"), tv, reqsBody(3));
    const auto* sc = sceneOf(e);
    for (const auto& id : {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")})
      requireNodeColors(sc, id, sc->style.boxFill, sc->style.boxStroke, sc->style.dividerColor, true, true);
    QSet<QString> fills;
    for (const auto& n : sc->nodes) fills.insert(n.fill);
    require(fills.size() == 1, QStringLiteral("empty border clears palette -> 1 fill; got %1").arg(fills.size()));
  }

  // ===== 10. bkgColorArray only (no border) is INERT =====
  {
    const QString tv = QStringLiteral("{\"bkgColorArray\":[\"#100000\",\"001000\",\"#000010\"]}");
    const auto e = renderThemeTvEntry(cache, QStringLiteral("default"), tv, reqsBody(3));
    const auto* sc = sceneOf(e);
    for (const auto& id : {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")})
      requireNodeColors(sc, id, sc->style.boxFill, sc->style.boxStroke, sc->style.dividerColor, true, true);
  }

  // ===== 11. User array REPLACES the redux built-in (cycles %2, not 12) =====
  {
    const QString tv = QStringLiteral(
        "{\"borderColorArray\":[\"#aa0000\",\"#00aa00\"],"
        "\"bkgColorArray\":[\"#100000\",\"#001000\"]}");
    const auto e = renderThemeTvEntry(cache, QStringLiteral("redux-color"), tv, reqsBody(4));
    const auto* sc = sceneOf(e);
    requireNodeColors(sc, QStringLiteral("A"), QStringLiteral("#100000"), QStringLiteral("#aa0000"), QStringLiteral("#aa0000"), true, true);  // k0
    requireNodeColors(sc, QStringLiteral("B"), QStringLiteral("#001000"), QStringLiteral("#00aa00"), QStringLiteral("#00aa00"), true, true);  // k1
    requireNodeColors(sc, QStringLiteral("C"), QStringLiteral("#100000"), QStringLiteral("#aa0000"), QStringLiteral("#aa0000"), true, true);  // k2%2=0
    requireNodeColors(sc, QStringLiteral("D"), QStringLiteral("#001000"), QStringLiteral("#00aa00"), QStringLiteral("#00aa00"), true, true);  // k1
  }

  // ===== 12. border/bkg length mismatch (border 5 / bkg 3): fill per-property =====
  {
    const QString tv = QStringLiteral(
        "{\"borderColorArray\":[\"#ff0000\",\"#00ff00\",\"#0000ff\",\"#ffff00\",\"#00ffff\"],"
        "\"bkgColorArray\":[\"#100000\",\"#001000\",\"#000010\"]}");
    const auto e = renderThemeTvEntry(cache, QStringLiteral("default"), tv, reqsBody(7));
    const auto* sc = sceneOf(e);
    const QString mainBkg = sc->style.boxFill;
    requireNodeColors(sc, QStringLiteral("A"), QStringLiteral("#100000"), QStringLiteral("#ff0000"), QStringLiteral("#ff0000"), true, true);  // k0 fill
    requireNodeColors(sc, QStringLiteral("B"), QStringLiteral("#001000"), QStringLiteral("#00ff00"), QStringLiteral("#00ff00"), true, true);  // k1 fill
    requireNodeColors(sc, QStringLiteral("C"), QStringLiteral("#000010"), QStringLiteral("#0000ff"), QStringLiteral("#0000ff"), true, true);  // k2 fill
    // k3,k4: border valid (outline+divider cycle), bkg out of range -> fill mainBkg.
    requireNodeColors(sc, QStringLiteral("D"), mainBkg, QStringLiteral("#ffff00"), QStringLiteral("#ffff00"), true, true);
    requireNodeColors(sc, QStringLiteral("E"), mainBkg, QStringLiteral("#00ffff"), QStringLiteral("#00ffff"), true, true);
    requireNodeColors(sc, QStringLiteral("F"), QStringLiteral("#100000"), QStringLiteral("#ff0000"), QStringLiteral("#ff0000"), true, true);  // k5%5=0
    requireNodeColors(sc, QStringLiteral("G"), QStringLiteral("#001000"), QStringLiteral("#00ff00"), QStringLiteral("#00ff00"), true, true);  // k1
  }

  // ===== 13. borderLen > 12: idx 12,13,14 fall to BASE; idx 15 -> color-0 =====
  {
    const QString tv = QStringLiteral(
        "{\"borderColorArray\":["
        "\"#010101\",\"#020202\",\"#030303\",\"#040404\",\"#050505\","
        "\"#060606\",\"#070707\",\"#080808\",\"#090909\",\"#0a0a0a\","
        "\"#0b0b0b\",\"#0c0c0c\",\"#0d0d0d\",\"#0e0e0e\",\"#0f0f0f\"],"
        "\"bkgColorArray\":["
        "\"#101010\",\"#202020\",\"#303030\",\"#404040\",\"#505050\","
        "\"#606060\",\"#707070\",\"#808080\",\"#909090\",\"#a0a0a0\","
        "\"#b0b0b0\",\"#c0c0c0\",\"#d0d0d0\",\"#e0e0e0\",\"#f0f0f0\"]}");
    const auto e = renderThemeTvEntry(cache, QStringLiteral("default"), tv, reqsBody(16));
    const auto* sc = sceneOf(e);
    const QString mainBkg = sc->style.boxFill;
    const QString nodeBorder = sc->style.boxStroke;
    const QString dividerColor = sc->style.dividerColor;
    requireNodeColors(sc, QStringLiteral("A"), QStringLiteral("#101010"), QStringLiteral("#010101"), QStringLiteral("#010101"), true, true);  // k0
    requireNodeColors(sc, QStringLiteral("F"), QStringLiteral("#606060"), QStringLiteral("#060606"), QStringLiteral("#060606"), true, true);  // k5
    requireNodeColors(sc, QStringLiteral("L"), QStringLiteral("#c0c0c0"), QStringLiteral("#0c0c0c"), QStringLiteral("#0c0c0c"), true, true);  // k11 (last rule)
    // k12,13,14 -> no genColor rule -> base, both visible.
    requireNodeColors(sc, QStringLiteral("M"), mainBkg, nodeBorder, dividerColor, true, true);
    requireNodeColors(sc, QStringLiteral("N"), mainBkg, nodeBorder, dividerColor, true, true);
    requireNodeColors(sc, QStringLiteral("O"), mainBkg, nodeBorder, dividerColor, true, true);
    // idx 15 -> 15%15 = 0 -> color-0.
    requireNodeColors(sc, QStringLiteral("P"), QStringLiteral("#101010"), QStringLiteral("#010101"), QStringLiteral("#010101"), true, true);
  }

  // ===== 14. Invalid array entry drops just that property (node B -> base) =====
  {
    const QString tv = QStringLiteral(
        "{\"borderColorArray\":[\"#ff0000\",\"notacolor\",\"#0000ff\"],"
        "\"bkgColorArray\":[\"#100000\",\"alsonotacolor\",\"#000010\"]}");
    const auto e = renderThemeTvEntry(cache, QStringLiteral("default"), tv, reqsBody(4));
    const auto* sc = sceneOf(e);
    requireNodeColors(sc, QStringLiteral("A"), QStringLiteral("#100000"), QStringLiteral("#ff0000"), QStringLiteral("#ff0000"), true, true);  // k0 valid
    // k1: both entries unparseable -> stroke+fill unprotected -> base for both.
    requireNodeColors(sc, QStringLiteral("B"), sc->style.boxFill, sc->style.boxStroke, sc->style.dividerColor, true, true);
    requireNodeColors(sc, QStringLiteral("C"), QStringLiteral("#000010"), QStringLiteral("#0000ff"), QStringLiteral("#0000ff"), true, true);  // k2 valid
    requireNodeColors(sc, QStringLiteral("D"), QStringLiteral("#100000"), QStringLiteral("#ff0000"), QStringLiteral("#ff0000"), true, true);  // k3%3=0
  }

  // ===== 15. Array color formats: hex / rgb / rgba / hsl / hsla all valid =====
  {
    const QString tv = QStringLiteral(
        "{\"borderColorArray\":[\"#ff0000\",\"rgb(0,255,0)\",\"rgba(0,0,255,0.5)\","
        "\"hsl(60,100%,50%)\",\"hsla(0,100%,50%,0.3)\"],"
        "\"bkgColorArray\":[\"#100000\",\"#001000\",\"#000010\",\"#100100\",\"#010010\"]}");
    const auto e = renderThemeTvEntry(cache, QStringLiteral("default"), tv, reqsBody(6));
    const auto* sc = sceneOf(e);
    requireNodeColors(sc, QStringLiteral("A"), QStringLiteral("#100000"), QStringLiteral("#ff0000"), QStringLiteral("#ff0000"), true, true);
    requireNodeColors(sc, QStringLiteral("B"), QStringLiteral("#001000"), QStringLiteral("rgb(0,255,0)"), QStringLiteral("rgb(0,255,0)"), true, true);
    requireNodeColors(sc, QStringLiteral("C"), QStringLiteral("#000010"), QStringLiteral("rgba(0,0,255,0.5)"), QStringLiteral("rgba(0,0,255,0.5)"), true, true);
    requireNodeColors(sc, QStringLiteral("D"), QStringLiteral("#100100"), QStringLiteral("hsl(60,100%,50%)"), QStringLiteral("hsl(60,100%,50%)"), true, true);
    requireNodeColors(sc, QStringLiteral("E"), QStringLiteral("#010010"), QStringLiteral("hsla(0,100%,50%,0.3)"), QStringLiteral("hsla(0,100%,50%,0.3)"), true, true);
    requireNodeColors(sc, QStringLiteral("F"), QStringLiteral("#100000"), QStringLiteral("#ff0000"), QStringLiteral("#ff0000"), true, true);  // k5%5=0
  }

  // ===== 16. P1#2: invalid user `style` UNDER a palette -> palette wins =====
  {
    // 16a fill:notacolor -> palette fill #FDF4FF (NOT foreground).
    {
      const auto e = renderThemeTvEntry(cache, QStringLiteral("redux-color"), QString(),
                                        reqsBody(1) + QStringLiteral("style A fill:notacolor"));
      requireNodeColors(sceneOf(e), QStringLiteral("A"), kBkgPalette.at(0), kBorderPalette.at(0), kBorderPalette.at(0), true, true);
    }
    // 16b stroke:notacolor -> palette stroke, outline+divider BOTH visible.
    {
      const auto e = renderThemeTvEntry(cache, QStringLiteral("redux-color"), QString(),
                                        reqsBody(1) + QStringLiteral("style A stroke:notacolor"));
      requireNodeColors(sceneOf(e), QStringLiteral("A"), kBkgPalette.at(0), kBorderPalette.at(0), kBorderPalette.at(0), true, true);
    }
    // 16c both invalid on A; B untouched still cycles.
    {
      const auto e = renderThemeTvEntry(cache, QStringLiteral("redux-color"), QString(),
                                        reqsBody(2) + QStringLiteral("style A fill:notacolor,stroke:notacolor"));
      const auto* sc = sceneOf(e);
      requireNodeColors(sc, QStringLiteral("A"), kBkgPalette.at(0), kBorderPalette.at(0), kBorderPalette.at(0), true, true);
      assertCycled(sc, QStringLiteral("B"), 1, false, QString());
    }
  }

  // ===== 17. P1#2 per-property split: redux-dark-color (stroke protected, fill not) =====
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

  // ===== 18. P1#2 no-palette contrast: invalid inline -> foreground / hidden outline =====
  {
    // 18a fill:notacolor, no palette -> foreground (#333).
    {
      const auto e = renderThemeTvEntry(cache, QStringLiteral("default"), QString(),
                                        reqsBody(1) + QStringLiteral("style A fill:notacolor"));
      const auto* sc = sceneOf(e);
      requireNodeColors(sc, QStringLiteral("A"), sc->style.foregroundFallback, sc->style.boxStroke, sc->style.dividerColor, true, true);
    }
    // 18b stroke:notacolor, no palette -> outline HIDDEN, divider keeps theme.
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

  qDebug() << "MermaidRequirementColorIndexTest: passed";
  return 0;
}
