These test-only WOFF2 specimens make Chinese/Latin fallback reproducible. They
are converted to TTF in `build/text-fixture-fonts` and are never bundled with the
application. The OFL / GUST Font licenses are included beside them.

* `han.woff2`: Noto Sans SC 2.04, weight 400 instantiated from the variable font.
  Source SHA-256: `763146584cf0710223441356b4395e279021b0806c196614377a7a0174ae074a`.
* `abyss.woff2`: LXGW WenKai Regular, the font used by the Abyss comparison.
  Source SHA-256: `b64b7add297672bf04c54ce229678ddf09b4f9671cb1ece1f24c868f4226edd0`.

Both subsets are renamed to `MuffinFixtureHan` / `MuffinFixtureAbyss`. They retain
only the characters required by the specimen. To reproduce them from the source
fonts, run `python scripts/subset_text_fixture_fonts.py --han <font.ttf> --abyss
<font.ttf>`. The script prints the original hashes for verification.
The Han subset also includes the Japanese characters used by the source/IME
editing tests, making their raster comparisons independent of system fonts.

`latex-{regular,bold,italic,bolditalic}.woff2` are renamed ASCII subsets of Latin
Modern Roman 10 v2.004, the imported LaTeX theme's first-choice Latin family.
They use the GUST Font License / LPPL 1.3c. Their source URLs and exact original
hashes are listed in `MANIFEST-MuffinFixtureLatex.txt`. Pass `--latex <directory>`
to the subset script, with the four original OTFs named `regular.otf`, `bold.otf`,
`italic.otf` and `bolditalic.otf`. Native SFNTs are generated only under build/.

`scripts/generate_text_backend_reference.mjs` embeds these exact WOFF2 bytes and
the existing Open Sans / PT Serif WOFF2s in Chromium. It records source hashes,
actual platform font choices, wrapping and caret positions in
`text-backend-browser.json`. The 96 cases include wide italic lines to isolate
shaping and narrow italic lines explicitly marked as diagnostics. Known italic
wrapping differences are reported separately from the enforced acceptance
checks; they are not a cross-platform production acceptance claim.
The LaTeX specimens include its 9.5pt body and 1.5/1.9em heading sizes. They
isolate font metrics; they do not claim full-theme screenshot equivalence.
