# Spell-check dictionaries — sources & licenses

These files are Hunspell-format dictionaries (`<locale>.aff` + `<locale>.dic`) bundled
for Muffin's built-in spell checker (powered by nuspell). The spell-checker loads them
read-only at runtime; they are not modified.

## English (`en_US`)

Source: [LibreOffice dictionaries](https://github.com/LibreOffice/dictionaries) → `en/`.
Derived from [SCOWL](http://wordlist.aspell.net/) (Spell Checker Oriented Word Lists,
© Kevin Atkinson and contributors). Licensed under MPL-2.0 / GPL / LGPL (the LibreOffice
distribution terms). SCOWL itself aggregates public-domain and BSD-licensed word lists.

## Other locales (`de`, `es`, `fr`, `it`, `nl`, `pl`, `pt_BR`, `ru`, `tr`, `vi`)

Source: [wooorm/dictionaries](https://github.com/wooorm/dictionaries), which repackages
upstream Mozilla / LibreOffice / RLA dictionaries with a consistent layout. Each
dictionary retains its upstream license — typically MPL-2.0, GPL, or LGPL. See the
upstream repository for the per-dictionary license text.

`pt_BR` was fetched from the `pt` (Brazilian Portuguese) folder of wooorm/dictionaries.

## CJK locales (`ja`, `zh_CN`, `zh_TW`, `ko`)

No traditional Hunspell spelling dictionary is bundled for CJK scripts — classic
word-based spell checking does not apply. When a CJK UI language is selected, the spell
checker falls back to `en_US`.

## Notes

- All dictionaries remain the property of their respective contributors and are
  distributed under their original licenses. Full upstream license texts are available at
  the linked repositories.
- The spell-check engine, nuspell (vendored under `third_party/nuspell/`), is licensed
  under LGPL-3.0-or-later; see `third_party/nuspell/COPYING.LESSER`.
