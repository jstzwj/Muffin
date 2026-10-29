# Native document visual acceptance

These fixtures stay fixed when the repository README changes. `readme.zh.md`
and `logo.svg` were copied from Muffin commit
`66a8568405703149b19b3ee53e67bf7cf2194344` (the repository's MIT license applies).
The other Markdown fixtures are derived from that Chinese text and comparison
table; the code fixture contains 4,000 lines in one fenced block. The table
also exercises native HTML row/column spans and formatted cell text.

The six small badge PNGs are authored test assets, mapped to the original image
URLs by `images.json`. Tests seed the normal image cache so screenshots do not
depend on a network request or changes at shields.io. The original filtered
SVG logo remains an SVG and follows the application's normal decoder.

Run `ctest --preset conan-release -R MuffinDocumentVisualAcceptance` after a
Release build. The three registered tests use display scales 100%, 125%, 150%;
each renders all four documents at editor zoom 100%, 125%, 150% with the bundled
GitHub theme. The theme viewport is 1920×1080 logical pixels and the editor has
1600 pixels available. Platform font fallback follows the application's normal
font resolver; the selected family list is recorded rather than assuming the
same installed CJK font on every operating system.
On Windows the offscreen plugin does not enumerate desktop fonts, so these tests
register installed Segoe emoji/symbol and Consolas faces before rendering. No
platform font files are copied into the fixtures or application bundle.

`build/visual-acceptance/` contains 36 PNGs and three JSON reports. Reports record
the font, page width, document height and source offsets at visual line starts.
Tests also round-trip text/cell caret geometry and long code-block clicks through
document hit testing. CI uploads these files as `document-visuals-*` artifacts.

Review screenshots for container alignment, SVG shadow bounds and transparency,
CJK wrapping, heading/list spacing, sub/superscript size and table borders.
Compare reports only with matching OS, fonts, theme and scale. Pixel differences
between different platform font rasterizers are not a failure by themselves.
Screenshots currently cover the first 1,000 logical pixels; geometry checks cover
all native paragraphs/cells plus the beginning, middle and end of long code.
