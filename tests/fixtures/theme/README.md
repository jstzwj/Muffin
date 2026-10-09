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
