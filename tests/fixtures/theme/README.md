# Document theme regressions

`MuffinTypographyConformanceTest` compares native used geometry and selected gradient pixels with the checked-in Chromium reference in `typography-browser.json`. Its pinned font isolates CSS and layout behavior. Regenerate that reference with `node scripts/probe_typography_css.mjs`.

`MuffinRealThemeRegressionTest` keeps the original Newsprint/Night font stacks. It uses the opening of the repository's `example.md`, 16/18px text sizes, and 720/960/1440px viewports. It checks fresh/incremental/lazy geometry, painting, caret/selection, typing followed by deletion, and resize paths. Formula interiors intentionally snap to atomic edges.

For a local browser audit, build with the Conan Release preset, then run:

```powershell
$env:MUFFIN_THEME_AUDIT_DIR = "$PWD/build/theme-audit/real"
ctest --preset conan-release -R '^MuffinRealThemeRegressionTest$' --output-on-failure
node scripts/audit_document_themes.mjs build/theme-audit/real
```

To include imported themes, set `MUFFIN_THEME_AUDIT_MANIFEST` to the absolute path of a local JSON array before running the test:

```json
[
  {"id": "newsprint", "css": "G:/github/Muffin/resources/themes/newsprint.css"},
  {"id": "night", "css": "G:/github/Muffin/resources/themes/night.css"},
  {"id": "latex", "css": "C:/path/to/latex.css"},
  {"id": "abyss", "css": "C:/path/to/phycat-abyss.css"}
]
```

Original relative imports and font files must remain alongside these stylesheets. Imported theme files are not copied into the repository. The browser script needs Playwright/Chromium; `MUFFIN_PLAYWRIGHT_MODULE` and `CHROME_EXECUTABLE` can select existing installations.

The audit exports the native parser's AST as HTML, loads original CSS/fonts, and supplies the same application canvas/ink palette to the browser host. Its text-size adjustment scales fonts and line height while keeping absolute box dimensions unchanged, matching native `textScale`. Soft-break mode is disabled in both renderers. PNG pairs include normal/focus variants; `comparison.json` records geometry, font families and pseudo-element computed values. `report.md` summarizes differences and excludes the editor's trailing empty caret paragraph from its final-block comparison.

This original-font audit is diagnostic, not a pixel-equality claim across Qt and Chromium. `native.json` records the actual Qt platform plugin; preserve the application's native plugin when investigating screen font metrics, since `offscreen` can select a different font backend. The checked-in conformance fixtures remain the deterministic browser assertions in CI. Remove the audit environment variables before ordinary test runs.

`MuffinRenderLinkBeforeFlowTest` consumes `live-pseudos-browser.json`. It covers adjacent Markdown links, links with nested emphasis, inline HTML links, structural/attribute/functional selectors and both generated-content boundaries at 100/125/200% zoom. Generated content retains link targets without becoming editable source. Heading tests consume the same fixture's positioned structural decorations, including percentage geometry, hover/focus inheritance and structural invalidation. Regenerate with `node scripts/probe_live_pseudos.mjs`.

`MuffinRealThemeBrowserGeometryTest` compares the original bundled Newsprint/Night CSS at 720/960/1440px with `real-theme-browser.json`. Only font families are overridden with the same SHA-verified Open Sans files in both engines. It checks page widths, block positions/sizes, first/last baselines, horizontal caret positions, code/keyboard boxes and fresh/lazy/incremental editing consistency. DOM character ink boxes and full-line editor carets have different vertical extents, so vertical comparison uses their shared baseline. CSS hashes require explicitly regenerating the reference after theme edits. Run `node scripts/probe_real_theme_geometry.mjs` to regenerate; CI reads the committed fixture without a browser download.

For fractional-size backend experiments, see [the font backend investigation](../../../docs/font-backend-investigation.zh.md). The opt-in `MuffinFontBackendProbe` target writes native glyph runs, wrapping/caret data and PNGs; `scripts/probe_font_backend.mjs` compares the same font bytes in Chromium. These experiments do not select a new production font backend.
