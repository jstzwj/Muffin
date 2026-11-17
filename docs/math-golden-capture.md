# KaTeX browser ink goldens

`bbox.json` records browser layout boxes and rasterized ink separately. Ink can
extend beyond the layout box: `\mathllap` has zero layout width, and scripts and
accents can extend vertically. A screenshot of the layout box with fixed padding
can silently crop valid glyphs.

`scripts/browser_ink_capture.mjs` starts with the target, descendant and text-range
bounds, intersecting them with ancestor overflow clips. This matters for KaTeX's
oversized stretchy SVGs. It moves the stage by whole pixels when ink extends past
the page origin, then expands the screenshot until the ink has a clear border.
If the border still contains ink after the bounded retries, generation fails.
The measured layout dimensions and font size stay independent of the screenshot.

Install the locked browser tools and run the capture regression tests:

```sh
npm ci
npx playwright install --with-deps chromium
npm run test:golden-capture
```

Regenerate both browser bbox sets without replacing the independent builder
metrics and glyph-position goldens:

```sh
node scripts/generate_katex_golden.mjs tests/fixtures/math/core.json tests/golden/katex --bbox-only
node scripts/generate_katex_golden.mjs tests/fixtures/math/katex-official-screenshotter.json tests/golden/katex-official --bbox-only
```

Omit `--bbox-only` to regenerate all three outputs. The generator uses the vendored
KaTeX distribution and embeds its fonts; it refuses missing required fonts.
Chromium is launched with `--font-render-hinting=none` so Linux's default hinting
does not snap glyph advances and change the reference layout widths.
Raster sizes can vary slightly between platforms, so review changes and keep the
native audit's existing tolerances. Do not trim native ink or enlarge tolerances
to match an incomplete browser screenshot.

On an Ubuntu version newer than Playwright supports, the compatible Ubuntu 24.04
browser can be selected with `PLAYWRIGHT_HOST_PLATFORM_OVERRIDE=ubuntu24.04-x64`.
CI runs the regression tests on Ubuntu 24.04.
