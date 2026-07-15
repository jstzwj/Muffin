# Bundled Noto fonts

The Mermaid renderer embeds static Noto fonts so text measurement and raster
goldens do not depend on the host operating system's font fallback set.

Sources:

- `NotoSans-Regular.ttf`, `NotoSansArabic-Regular.ttf`, and
  `NotoSansHebrew-Regular.ttf`: `notofonts/noto-fonts`, branch `main`, hinted TTF.
- `NotoSansCJKsc-Regular.otf`: `notofonts/noto-cjk`, branch `main`, Sans OTF,
  Simplified Chinese.

All files are distributed under the SIL Open Font License 1.1 in `LICENSE`.
Static TTF/OTF files are used deliberately because the Noto CJK project warns
that CFF2 variable fonts are not supported by Windows.

SHA-256:

- `NotoSans-Regular.ttf`: `b85c38ecea8a7cfb39c24e395a4007474fa5a4fc864f6ee33309eb4948d232d5`
- `NotoSansArabic-Regular.ttf`: `ceea25b464a656dc3b26849bab9356740401af62aedf1bfa8b7f0d9b75925b1b`
- `NotoSansCJKsc-Regular.otf`: `2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`
- `NotoSansHebrew-Regular.ttf`: `a7fa16fffb27bedb060a0866267c29e9859aeb9c21cc33f5b3aaf6eb062eca85`
