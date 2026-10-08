# Native layout conformance cases

`upstream.json` pins the WPT revision and SHA-256 of each unmodified source file.
The upstream license is retained in `upstream/LICENSE.md`. These files document
provenance; Muffin does not execute the WPT JavaScript harness natively.

`cases.json` contains adaptations for Muffin's horizontal, in-flow CSS subset.
Each case records its source and changes. Support stylesheets are expanded into
inline declarations, stable IDs expose geometry, and unsupported positioned or
orthogonal variants are excluded explicitly. This is selected WPT coverage, not
a claim of passing the complete upstream test.

The Flex/Grid browser probes append these cases to their existing fixtures.
Explicit upstream size assertions are checked before the probe writes output.
The native tests compare all identified boxes at 100% and 200% zoom. Committed
browser fixtures let the ordinary CTest suite run without Chromium or networking.

Both sides load the same bundled Open Sans regular, italic, bold and bold-italic
files. The browser probes wait for all four faces before measuring and record
the source WOFF2 SHA-256 hashes in each reference. Native tests verify those
hashes, load the build-generated TTF versions and assert 96 logical DPI. Missing
fonts fail explicitly instead of silently measuring a system fallback. Linux
CTest uses the runner's `/etc/fonts/fonts.conf` when it exists, since Conan's
Fontconfig build-time configuration path need not exist on the test machine.

Additional native cases cover inline components and container baseline export,
including occupied rows, shared baseline groups and row-major fallback order from
[CSS Grid 2 section 11.6](https://www.w3.org/TR/css-grid-2/#grid-baselines).

Regenerate with a local Chromium and Playwright:

```powershell
$env:CHROME_EXECUTABLE='C:/Program Files/Google/Chrome/Application/chrome.exe'
node scripts/probe_flex_css.mjs
node scripts/probe_grid_css.mjs
```
