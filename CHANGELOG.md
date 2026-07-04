# Changelog

All notable changes to Muffin will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Drag a file from the sidebar into the editor to insert a link** - Dragging a file from the file tree onto the rendered page or the source editor inserts a markdown link at the drop position, e.g. `[拔牙日记1.md](日记\拔牙日记1.md)`. Image files insert as inline images (`![alt](...)`); folders keep their existing "open as sidebar root" behavior. The target path is resolved relative to the current document's directory when the file lives inside it (portable across folder moves), otherwise absolute, with native separators. Only drags that originate in Muffin's file tree insert a link; external `file://` drops keep the prior open-as-document behavior. As part of this change, dropped images now also land at the drop position instead of the stale caret

> Note: link targets use native path separators (backslash on Windows). A path segment that starts with ASCII punctuation right after a separator (for example `dir\.gitignore`) is shortened by CommonMark's backslash-escape rule; rename or use forward slashes for such paths if a renderer mis-parses the link.

## [0.5.1] - 2026-07-04

An internal-architecture release. No user-visible behavior changes — every edit is behavior-preserving and gated by the full test suite — but the largest source files have been decomposed into focused modules for navigability and future maintenance.

### Changed
- **CSS theme mapper decomposed** - The 2461-line theme mapper is split into focused modules: `CssValueParser` (colour/length/gradient value parsing — the shared chokepoint), `CssSelectorAnalysis` (selector parsing), and `CssDecorationExtractor` (pseudo-element / hover / animation extraction). It is now 1346 lines, and every colour and length resolution has a single source of truth. The HTML inline-style resolver now shares the same parsing path
- **Editor view decomposed** - The 2012-line editor view is split into a paint module (`EditorViewPaint`) and a viewport / scroll / coordinate module (`EditorViewViewport`), leaving the core file to event handling and state
- **Input controller decomposed** - The 1982-line input controller is split into auto-pair, smart-punctuation, emoji-autocomplete, and block-removal modules, isolating each editing feature behind its own translation unit
- **Math parser and builder decomposed** - The LaTeX function-command switches (`parseFunction`'s 594-line switch and `buildNode`'s inline cases) are now per-command handler methods behind a thin dispatch, so each `\command`'s parse/build logic is independently navigable
- **Per-type block paint** - `BlockLayout::paintSelf` is now a thin dispatch over `paintInlineBlock` / `paintBlockQuote` / `paintMathBlock` / `paintHtmlBlock` / `paintThematicBreak`
- **Unified block-removal path** - The `tryRemove*` block-deletion handlers share a single parameterized skeleton (`removeTopLevelBlock`), removing roughly 500 lines of duplicated structure
- **Single performance-probe header** - Five duplicated `PerfTimer` RAII probes across the codebase are consolidated into one `diagnostics/ScopedPerfProbe` header
- **Drag-selection state enum** - The drag-selection boolean pair is replaced with an explicit `Idle` / `Pending` / `Dragging` state enum
- **Named render metrics** - The root-em (16px) and line-height factor (1.16) magic numbers are now named constants (`kRootEmPx`, `kLineHeightFactor`) shared across the render layer
- **Iterative CSS descendant traversal** - The `:has(...)` descendant tag collection now uses an iterative depth-first walk, extending the earlier sibling-chain stack-overflow fix to deeply nested documents

## [0.5.0] - 2026-06-30

### Added
- **CSS computed-style engine** - A real computed-style cascade now resolves CSS values for rendered elements, adding `calc()` expressions, `conic-gradient()` backgrounds, `text-shadow` (offscreen blur composite), a `filter` suite (blur and color filters), per-code-point `text-transform`, and text-decoration color/style - so imported CSS themes that rely on these properties now render at full fidelity
- **Focus animation and structural selectors** - Themes can now use `:focus` (with a smooth blend against hover), sibling and general-sibling combinators (`+`, `~`), and structural pseudo-classes (`:first-child`, `:nth-child(an+b)`, `:has(tag|.class)`) via a real-tree matcher; themes without structural rules keep the fast load-time prototype path
- **Heading auto-numbering** - CSS `counter()` on headings (e.g. phycat's `counter(h1)`) now renders as real numbers instead of literal text; a counter-reset/increment state machine runs on full and range rebuilds and is reused on single-block edits so the heading outline never shifts
- **Thematic-break editing experience** - Editing around a `---` thematic break now stays smooth and seamless, with stable selection and layout through insert and delete gestures, including next to nested containers
- **Animated loading overlay** - Asynchronous document loads now show an animated overlay instead of a bare "Loading…" paint
- **data: URI images** - Inline `data:` URI images are now decoded and rendered
- **HTML export theme embedding** - Export to HTML (styled) now embeds the active theme's CSS, recursively inlining local `@import` sheets so multi-file themes export with their full styling
- **Pandoc auto-detect** - The export and import pipeline now auto-detects a system Pandoc installation and resolves its path more robustly
- **Disabled-state theme colors** - Themes can declare disabled-state color tokens, and chrome color derivation now fills them

### Changed
- **PieceTable text storage** - The document text buffer is now a zero-copy PieceTable, eliminating full-buffer copies on every edit
- **Large-file performance and stability** - Opening and editing very large files (tens to hundreds of MB) is now dramatically faster and no longer crashes. Fixes include a stack overflow in the CSS sibling-chain traversal, an O(n²) structural-cascade rebuild on flat block lists, O(document) marker and cursor-resolution scans on every keystroke, per-block registry reads in the layout-estimate loop, and major memory reductions during parse (freeing the UTF-8 and cmark trees as soon as they are consumed). Undo on a huge document dropped from roughly 20 seconds to milliseconds
- **Theme and render refactor** - Consolidated theme parsing and rendering, with element box geometry (margin, padding, border, radius, fit-content) now single-sourced from element styles instead of duplicated across spacing fields

### Fixed
- **Inline image offset under CSS line-height** - Inline images no longer draw offset from their line under themes that set a custom `line-height`
- **`content: none` rendered as literal text** - Pseudo-elements declaring `content: none` or `normal` no longer paint the word "none"
- **`rem` unit resolution** - `rem` now resolves against the root font size rather than the body font size, fixing oversized text in themes such as pixyll
- **Lazy-promote layout gap** - Promoting an off-screen block into view no longer snaps its spacing gap once on the first click
- **Undeclared table background rendered black** - Tables in themes without an explicit background color (e.g. Whitey) no longer render solid-black header and stripe rows; undeclared backgrounds fall back to transparent over the page
- **Nested-blockquote trailing empty paragraph** - Pressing Enter inside a nested blockquote now produces a visible, editable empty line instead of the caret vanishing
- **Editor-only CSS hacks leaking** - Editor-only CSS hacks carried by imported themes (e.g. `pre.md-meta-block`) no longer leak into code fences and inflate their height
- **CSS cascade leaking non-inherited properties** - Parent padding, margin, and border no longer leak into children; only inherited and custom properties propagate down the tree
- **CSS hex-alpha color byte order** - `#RRGGBBAA` colors are now read in the correct byte order (previously read as `#AARRGGBB`, turning pale low-alpha colors bright)
- **Async open parse/edit race** - Keystrokes typed while an asynchronous parse is in flight no longer clobber the document being loaded; the input pipeline now gates on parse-in-progress
- **Local-edit top-level block handling** - Editing near non-editable top-level blocks no longer corrupts the live tree or triggers a full re-parse
- **macOS build** - Fixed a default-argument compile error in `BlockLayout::paint`

## [0.4.1] - 2026-06-24

### Fixed
- **Image links render as clickable images** - An image wrapped in a link (`[![alt](image)](url)`) now displays the image inline and opens the link on click, instead of collapsing to the alt text styled as a plain link. Link formatting is now applied as a separate attribute that composes with any nested inline content: inside a link, images load, inline code keeps its background, and inline math keeps its styling

## [0.4.0] - 2026-06-24

### Added
- **CSS-based built-in theme system** - The five built-in themes (GitHub, Newsprint, Night, Pixyll, Whitey) are now authored as CSS files and loaded through a `CssThemeMapper` that converts selectors to theme tokens; CSS-format themes can be imported alongside JSON user themes, with custom line-height, heading colors, margins, and document-card rendering with shadows
- **Break on single newline** - A new Markdown rendering preference renders a single line break as an actual line break rather than joining lines, applied consistently across the rendered view, HTML export, and Pandoc export, while leaving the saved source as soft breaks
- **`<br>` editing** - All three `<br>` variants (`<br>`, `<br/>`, `<br />`) are now handled uniformly in length calculation, table-cell editing, plain-text generation, and inline projection, so hard line breaks render and edit correctly
- **HTML block empty-content fallback** - HTML blocks that render no visible content (only scaffold tags or whitespace) now fall back to showing the syntax-highlighted source instead of a blank block
- **Typography alignment and font styles** - Themes can now control text alignment, font weight, and italic styling; the Night theme gained its own cursor resources, fonts, and styles, and heading decoration drawing was refined
- **Windows ARM64 installer** - Cross-architecture MSI packaging driven by a new `MUFFIN_MSI_ARCH` cache variable, with a Windows ARM64 build added to CI and the release workflow (the Conan cache key is now architecture-aware to prevent cross-arch corruption)

### Changed
- **Block-relative offset model** - MarkdownNode descendant offsets are now stored relative to their top-level block and resolved to absolute on read; local edits shift only the top-level block's range instead of recursively walking descendants, fixing inline-projection offset drift and cutting per-edit cost
- **Typing performance** - Added a typing performance benchmark and instrumentation timers across the edit pipeline (visible under the `muffin.perf` log category); local-edit lazy-marker processing no longer builds a full document copy, and the markdown text buffer pre-reserves growth capacity to reduce per-edit allocation and copying
- **Incremental sidebar and index refresh** - Outline refresh is now debounced for local edits, and both the line-start offset cache and the document node index update incrementally instead of rebuilding on every keystroke
- **Inline math source retrieval** - Refactored to read the TeX source directly from the projection span instead of recomputing, and table column-width calculation now accounts for a span's maximum visual line advance
- **UTF-8 string construction** - All hardcoded UTF-8 byte sequences are now built via `QString::fromUtf8` instead of `QStringLiteral`, avoiding cross-compiler portability pitfalls (correct on MSVC, garbage on GCC/Linux)
- **libavif build** - Replaced the Conan `libavif` package with the AV1 decoder `dav1d` and a vendored libavif built from source, avoiding the unreachable libyuv dependency; the Conan cache workflow was split into restore/clean/save phases to reduce cache size and avoid cache-over-limit failures

### Fixed
- **Thematic break deletion next to nested containers** - Backspace/Delete can now remove a thematic break (`---`) even when the adjacent editable block is nested inside a blockquote or list; rule handlers now climb to the next block in document order instead of stopping at the container boundary
- **Spurious empty paragraphs and spacing** - Rewrote virtual-empty-paragraph insertion to count actual blank lines in the gap rather than relying on line arithmetic (cmark-gfm under-reports end lines for math and HTML blocks), eliminating false empty paragraphs after display-math and HTML blocks and the extra blank space between paragraphs
- **Rem unit and paragraph spacing** - Added root-relative-px (`rem`) computation in the CSS theme mapper and adjusted paragraph spacing calculation to remove inter-paragraph gaps; non-tail blocks are no longer misjudged as a block-tail area on click
- **Code border color** - Themes without an explicit code-border color now derive a soft border instead of falling back to pure black
- **avif/dav1d static linking** - MuffinUi now explicitly links `dav1d::dav1d`, resolving undefined-symbol errors when libavif was built statically without merging Conan's dav1d dependency

## [0.3.0] - 2026-06-20

### Added
- **Unified theme system** - A single `ThemeDefinition` data structure now drives every styling surface, replacing four parallel hardcoded systems. The document theme and the chrome (window) theme are described by one definition, and a `ChromeStyleSheet` generator produces the application QSS from theme tokens instead of string literals
- **Custom theme import** - Load user-authored themes from `AppDataLocation/themes/*.json` on startup; `ThemeManager` scans the folder and lists built-in and imported definitions together
- **Dynamic Theme menu** - The Theme menu is now enumerated live from the available definitions, so imported themes appear immediately alongside the built-ins, with an Import-Theme action reachable from both the menu and the Appearance preferences page
- **Open theme folder** - Quickly jump to the user themes directory to add or edit custom theme files
- **Serif body typography** - Themes can opt into a serif body font via a `serifBody` flag; the built-in Pixyll theme is now a serif theme, and Whitey's palette was softened for a gentler look
- **HTML render theming** - HTML blocks now render against a full `HtmlColorPalette` derived from the active theme, so embedded HTML adopts the document's colors instead of hardcoded values
- **Smart punctuation** - Input-side conversion of straight quotes, dashes, and ellipses into their typographic forms, plus a display-only render-time conversion that leaves the Markdown source untouched
- **Smart punctuation controls** - A Markdown preferences section and Math Tools menu commands to toggle smart punctuation behavior
- **Math Refresh All** - A global command (under the Math Tools submenu) that re-renders every visible math formula at once

### Changed
- **Full theming refactor** - The sidebar, preferences dialog, and all chrome widgets were rewired to follow the active theme colors, removing the last hardcoded color values and the old static theme-switching code paths
- **Unicode punctuation remapping** - Punctuation is remapped during parsing so that smart quotes and dashes no longer break Markdown syntax recognition

### Added
- **Quick Open** - Fuzzy file opener for the workspace folder, surfacing recently used and recently modified files for fast switching
- **Multi-format export** - File → Export to PDF (native renderer), HTML and plain HTML (native serializer), plus Word, ODT, RTF, ePub, LaTeX, MediaWiki, RST, Textile, and OPML via an external Pandoc process
- **Import via Pandoc** - Convert documents from other formats into Markdown through File → Import
- **File operations** - Move, delete, reveal in file manager, reopen-with-encoding, save-all-open-files, and show-in-sidebar commands for the current file
- **Image insertion policy** - A centralized system unifying image insertion across paste, dialog, and drag-and-drop with six configurable actions (no action, copy to `./`, copy to `./assets`, copy to `./$(filename).assets`, upload, copy to a custom folder), honoring front-matter upload overrides and configurable relative-path, leading-slash, and URL-escaping formatting
- **Custom-command image upload** - Upload images via a configurable external command (a second QProcess subsystem), parsing the returned stdout lines as image URLs
- **Bundled PNG and JPEG decoders** - PNG and JPEG are now decoded by bundled static libpng and libjpeg instead of Qt's image plugins, so images load reliably even in Qt builds shipped without `qjpeg`/`qpng`
- **Custom-drawn status bar** - A fully self-painted status bar themed from the active render theme, with a click-to-open document statistics popup (words, characters, lines, reading time, and selection count) and an inline spell-check language quick-switcher
- **Code block indent, dedent, and copy-content commands** - Code Tools actions to indent or dedent a selection or the whole block by line, and copy the raw block content
- **Refreshed About description** - Updated the Help → About blurb to accurately reflect the current feature set

### Changed
- **Chrome font hinting** - UI text (menus, status bar, dialogs) now uses vertical-stroke hinting for smoother anti-aliasing, bringing the rendered chrome closer to native application text
- **Code block indent logic** - Refactored code-block indent handling and removed the Shift+Tab indent configuration
- **Image copy command IDs** - Unified the image copy command id naming

### Fixed
- **HTML block source view wrapping** - Clicking an HTML block to edit its source once again soft-wraps the source text; a regression had tied it to the "Code Blocks Auto Wrap" setting, causing the reserved layout height (wrapped) to disagree with the painted text (clipped, non-scrolling overflow)
- **Local JPEG and PNG images** - Markdown and HTML images referencing local `.jpg`/`.png` files now render reliably regardless of Qt's image-plugin availability
- **HTML `<img>` with Windows drive-letter paths** - `<img src="C:/...">` is no longer rejected as an unsafe URL scheme by the sanitizer
- **Paste Plain command** - The Paste Plain action is no longer permanently disabled
- **Image menu** - Removed a duplicate "Copy Image" menu mount and enabled the image global-settings command

## [0.2.7] - 2026-06-19

### Added
- **Render-mode context menu** - Right-click in the rendered view opens a context-aware menu for text, links, images, and tables, with dedicated "Open Link" and "Copy Link Address" commands
- **Draft recovery** - A new draft recovery system restores unsaved documents after a crash or an unsaved exit
- **Outline folding** - The sidebar outline can now collapse and expand heading subtrees, toggleable from the preferences
- **List and alert operations** - Insert GitHub-style alert blocks, toggle task list item status, and indent/outdent list items via the paragraph commands
- **File drag-and-drop** - Drag-and-drop now accepts folders, Markdown files, and other importable files in addition to images
- **Autosave** - Automatic periodic saving and save-on-exit options to protect unsaved work

### Changed
- **Table toolbar orientation** - The table toolbar now switches between horizontal and vertical layout automatically, preferring a position above the table and falling back to the left sidebar when space is constrained

### Removed
- **Advanced debug settings card** - Removed the advanced debug settings card from the General preferences page

### Fixed
- **Table insertion position** - Inserting a table now places it after the cursor's current block instead of always appending it to the end of the document
- **Cursor disappearing on empty blockquote** - Typing a blockquote into an empty block no longer loses the cursor; empty blockquotes are demoted to paragraphs during parsing so the cursor always has a place to land

## [0.2.6] - 2026-06-19

### Added
- **Subscript and superscript** - `~text~` subscript and `^text^` superscript syntax with parsing, serialization, and rendering, plus toggleable parse options in the Markdown preferences page
- **Highlight inline format** - `==highlight==` inline highlighting with full parse, render, and serialize support
- **GitHub-style alerts** - Blockquote alerts marked with `[!NOTE]`, `[!TIP]`, `[!WARNING]`, etc. now render as themed cards
- **Code fence horizontal scroll** - Non-wrapping code fences support horizontal scrolling via scrollbar drag and Shift+wheel, keeping the cursor within view
- **Code block line numbers** - Code blocks now display line numbers in the gutter
- **Code block Shift+Tab dedent** - Shift+Tab now dedents the selected lines across a code block
- **Image zoom and conversion** - Resize images via `style="zoom:N%"` and convert between Markdown image syntax and HTML `<img>` tags
- **Markdown preferences system** - A unified Markdown parse-options panel controlling smart punctuation, list style, code block indent, strict mode, and the new subscript, superscript, and highlight extensions
- **Restore last file on startup** - When launched without arguments, the app reopens the most recently used file instead of a blank document

### Changed
- **Source editor theming** - The source editor's colors are now derived from the active render theme and update live on theme switch; hardcoded color values were removed and the gutter/current-line highlighting was unified with the theme
- **Viewport anchor stability** - Replaced slot-index anchoring with a NodeId-based anchor behind a unified `ScopedViewportPin` RAII primitive that pins every layout-mutation path, keeping the view steady across block refreshes, range refreshes, full re-layouts, and gutter realignment
- **Document layout lazy loading** - `DocumentLayout` now builds lazily and renders only visible blocks, with visible-block refresh APIs that avoid full rebuilds on large documents
- **Parse and document-build performance** - Definition-block tree traversal reduced from O(M*N) to O(M+N) with caching for missing-definition inserts, plus independent performance timers for parse and `setMarkdownText`

### Fixed
- **Scroll jumping on inline marker reveal** - Revealing inline markers and promoting blocks no longer causes the viewport to jump
- **Math parse toggle** - The math formula parse switch now correctly takes effect when changed in preferences
- **Inline cursor hit range** - Corrected the cursor hit-test range logic for inline elements

## [0.2.5] - 2026-06-17

### Added
- **Spell checking** - Nuspell-powered spell checking with a global spell checker, an editor preferences panel to enable it and switch the dictionary language, misspelling underlines in both render and source modes, a right-click suggestion menu and ignore-word support, and bundled dictionaries for 11 languages (English, German, Spanish, French, Italian, Dutch, Polish, Portuguese (Brazil), Russian, Turkish, and Vietnamese)
- **Emoji autocomplete** - Emoji autocomplete system with a bundled emoji provider and a popup completer widget, enabled by default while typing
- **Configurable indent** - Default indent size selector (2/4/8 spaces) with an "Align Indent" option; the indent system was refactored to support configurable units and aligned indentation
- **Whole-line copy and cut** - The clipboard now copies or cuts the entire line when no text is selected
- **Copy as Markdown** - Option to copy the Markdown source when copying plain text
- **Block source preview** - The status bar can show the Markdown source of the current block element

### Fixed
- **Language switching heap corruption** - Menu translation no longer rebuilds menus on the `LanguageChange` event; it now updates menu text in place via `retranslateMenuTexts()`, avoiding heap corruption from freeing menu resources while Qt still has pending events
- **Inline projection offset mapping** - `InlineProjection` decoding now covers backslash escapes in addition to HTML entities, and splits text nodes so each plain-text segment keeps a 1:1 offset correspondence, fixing incorrect cursor and source position mapping for escaped punctuation and HTML entities

## [0.2.4] - 2026-06-16

### Added
- **In-app help viewer** - Quick Start, Markdown Reference, and Acknowledgements are now available from the Help menu in a built-in viewer that renders the documentation with the native Markdown engine, shows the localized version matching the current interface language, and supports back/forward navigation and cross-document links

## [0.2.3] - 2026-06-15

### Added
- **Table of contents insertion** - Insert a `[TOC]` table-of-contents marker via the insert menu
- **Editor multi-selection and navigation** - Paragraph/block selection, word selection, jump to document start/end and line start/end, and select-next-match, available in both render and source editors with menu shortcuts
- **Delete Range submenu** - Unified `deleteRange` deletion with delete-by-word, delete-by-format-block, delete-by-line, and delete-whole-block commands under a new Edit submenu
- **Thematic break editing gestures** - Backspace after a paragraph removes an adjacent trailing thematic break, delete before removes a leading one, with list-adjacency handling and full undo/redo support

### Changed
- **Ordered list marker alignment** - Ordered lists now indent content based on the widest item marker and right-align markers to that boundary, so multi-digit markers no longer overlap content and the cursor stays out of the marker area

### Removed
- **Emoji and symbols menu entry** - Removed the emoji and symbols menu item and its translations

### Fixed
- **Math block editing** - Unified math block source-range annotation, correct literal parsing (trimming stray leading/trailing newlines), and preservation of original newlines on edit
- **Backspace at trailing empty paragraph** - Backspace from the virtual trailing paragraph now collapses to the end of the last real block instead of doing nothing
- **Empty list item backspace** - Empty list items now merge into the preceding sibling on backspace instead of always outdenting (only outdenting when there is no preceding item), and no longer lose the cursor

## [0.2.2] - 2026-06-14

### Added
- **Automatic update checking** - The app now checks for new releases on startup (once per 24 hours) and notifies the user when an update is available; users can also check manually from the Help menu
- **Application icons** - Generated app icons (PNG/ICO/ICNS) embedded into the Windows `.exe`, macOS `.app` bundle, and runtime window icon across all platforms
- **Setext heading support** - Full Setext-style heading (`====`/`----`) parsing, serialization, and editing
- **LaTeX display math syntax** - `\[ ... \]` LaTeX-style math blocks are now parsed and rendered alongside the existing `$$` syntax
- **HTML underline rendering** - `<u>` inline HTML and HTML-block underline styling now render correctly
- **Virtual trailing paragraph cursor** - A virtual trailing paragraph at the end of the document can be focused and edited for smoother end-of-document cursor handling
- **Paragraph insertion around literal blocks** - Insert empty paragraphs before and after code and math blocks
- **Blockquote dedent on Enter** - Pressing Enter in an empty blockquote paragraph outdents the block instead of nesting deeper

### Changed
- **Heading rendering** - Simplified heading block rendering logic, removed heading-prefix rendering, and improved empty heading handling
- **Preferences UI** - Refactored the preferences dialog layout with unified styling and shared layout constants
- **Literal block cursor handling** - Reworked cursor handling for code, math, and other literal blocks, including merging literal blocks with following paragraphs
- **Pending block marker logic** - Unified the pending-block marker processing flow in the parser
- **List item editing** - Improved list item editing with cursor positioning and merging support for task list items
- **Blockquote line breaks** - Corrected line-break behavior and prefix handling inside blockquotes
- **Generalized naming** - Renamed editor-specific identifiers to a more generic editor style

### Fixed
- **Setext underline false positive** - Fixed standalone `-` being misdetected as a Setext heading underline in cmark-gfm

## [0.2.1] - 2026-06-11

### Added
- **Underline formatting** - Toggle underline format via toolbar button or keyboard shortcut
- **Strikethrough formatting** - Toggle strikethrough format via toolbar button or keyboard shortcut
- **Inline math formatting** - Toggle inline math formatting command from the format toolbar
- **HTML comment support** - HTML comments (`<!-- ... -->`) are now parsed and rendered in the editor
- **Format toolbar optimization** - Reformatted toolbar with improved grouping of text formatting actions

### Changed
- **Cursor format state query** - Refactored cursor format state detection logic for more reliable real-time formatting feedback
- **Chinese word segmentation** - Improved Chinese text word boundary detection for better cursor movement and selection
- **Heading block editing** - Optimized heading block editing and rendering logic for smoother editing experience
- **Page layout margins** - Adjusted page layout margins and content width for better readability

### Fixed
- **Qt TLS and network plugins** - Added Qt TLS and network information plugins to the distributable bundle for reliable remote image loading

## [0.2.0] - 2026-06-10

### Added
- **HTML block rendering** - Live HTML preview rendering within HTML blocks, with inline editing toggle; powered by lexbor (HTML parser) and yoga (flexbox layout engine)
- **Inline HTML rendering** - Common inline HTML tags (`<b>`, `<i>`, `<a>`, `<img>`, `<kbd>`, etc.) rendered inline in Markdown content via `InlineHtmlRenderer`
- **HTML table layout** - Full HTML `<table>` layout with caption, row/cell layout, column alignment, and cell spanning
- **`<pre>` tag support** - `<pre>` and `<pre-wrap>` white-space modes with dedicated text measurement and layout
- **HTML `<img>` as Markdown image** - HTML inline `<img>` tags (standalone or wrapped in `<a>`) are parsed and rendered as Markdown image spans
- **`<kbd>` tag rendering** - Keyboard shortcut styling with rounded background and monospace font
- **SVG image support** - SVG image rendering via Qt6Svg, with size detection and fallback decoding
- **Image placeholder icons** - Placeholder and broken-state SVG icons displayed during image loading and on load failure
- **Qt standard dialog translations** - `qtbase_*.qm` translation files bundled for localized standard dialog buttons (Save, Discard, Cancel, etc.) across 13 languages

### Changed
- **C++ standard upgraded to C++20** - All platforms and CI configurations now use C++20 (`gnu20` on Linux, `20` on Windows/macOS)
- **HTML style system** - Refactored style parsing with font family inheritance, percentage width/margin, border/radius, line-height, ordered list styles, and `<details>` collapse support
- **Image loading pipeline** - Local images fall back to custom `ImageDecoder` on Qt failure; remote HTTP/HTTPS images loaded asynchronously with caching
- **Icon resources consolidated** - Merged `image_icons.qrc` and `statusbar_icons.qrc` into unified `icons.qrc`
- **Qt translation auto-discovery** - CMake now searches multiple paths (Qt prefix, `QT_HOST_PATH`, `Qt6_DIR`, PySide6) for `qtbase_*.qm` files and auto-copies them
- **Inline projection selection refresh** - Pre-drag selection state preserved to fix selection display after drag operations

### Fixed
- **Text background color in HTML** - Fixed background color not being applied during inline text rendering
- **SVG image sizing** - Fixed local SVG images returning incorrect dimensions

## [0.1.6] - 2026-06-09

### Added
- **Math macros and symbols** - braket package macros (`\bra`, `\ket`, `\braket`, `\set`), stmaryrd symbols, and user-defined macro support
- **Wide character mapping** - Mathematical Alphanumeric Symbols (U+1D400–U+1D7FF) rendering with surrogate pair token merging and precomposed character NFD decomposition
- **CD environment** - `cd` commutative diagram environment parsing and rendering
- **equation environment** - `equation` environment support with optimized parser context breakpoint handling
- **\middle command** - Delimiter rendering with `\middle` command support
- **\imath/\jmath** - Dotless i and j math symbols
- **Context-sensitive \dots** - `\dots` macro now selects `ldots` or `cdots` based on context

### Changed
- **Math error nodes** - Error node generation refactored to match KaTeX behavior
- **Auto-link detection** - Improved inline node autolink attribute recognition
- **Text/math mode transitions** - Enhanced text mode and math mode conversion with more font commands
- **Unicode and ligature handling** - New Unicode character decomposition and ligature processing logic
- **Image menu translations** - Complete i18n for image context menu and new UI strings across all 14 languages

### Fixed
- **Arrow rendering** - Fixed renderer arrow drawing and empty node handling
- **Test ink border tolerance** - Adjusted ink border width test tolerance to 1.25 for cross-platform stability

## [0.1.5] - 2026-06-09

### Added
- **Image editing** - Insert local/network images, drag-and-drop upload, right-click image context menu, batch image processing, preview rendering, open image location, copy/move image files, and update image reference paths
- **Remote image loading** - Asynchronous remote image loading with caching via `ImageLoader`, powered by Qt Network; mixed local/remote image rendering in documents
- **WebP and AVIF image support** - Fallback decoding for WebP and AVIF formats via libwebp and libavif when Qt's native image loader doesn't support them
- **Image block preview** - Active image spans show a large preview below the inline text; inactive images render inline
- **Always-on-top view** - View → Always on Top option (Ctrl+Shift+F) with state persistence
- **macdeployqt auto-discovery** - CI workflow step to locate `macdeployqt` from Conan cache and pass it via `MUFFIN_MACDEPLOYQT` cache variable

### Changed
- **Project structure** - Migrated headers from `app/` to `document/` and `projection/` directories; added `EditorContextHolder`, `MainWindowActionBinder`, `MainWindowSignalBinder`, and `RenderCommandFacade` to decouple responsibilities
- **NodeIndex** - Rewritten to manage block nodes in document order
- **MathLayoutResult overflow clipping** - Improved clipping logic for overflow cases
- **Inline layout traversal** - Fixed traversal logic for mixed content blocks

### Fixed
- **Square root and array math rendering** - Adjusted radical yOffset and SVG drawing area to match KaTeX output; refactored array rendering with VList wrapping for correct border/ink box alignment
- **Null pointer checks** - Unified null checks using new `EditorContext` helper methods across multiple call sites
- **macOS DMG build** - Fixed `macdeployqt` not found by adding Conan cache search with `MUFFIN_MACDEPLOYQT` cache variable fallback

## [0.1.4] - 2026-06-08

### Changed
- **EditorContext** - Introduced unified `EditorContext` struct to encapsulate all core editor context objects, replacing scattered `setDocumentSession` and other individual setters with a single `setContext` interface across all controllers
- **Inline source range model** - New `InlineRange` and `InlineSourceRanges` structs for managing inline node source positions; rewrote `InlineProjection` and `CmarkNodeAdapter` to use precise inline source range annotations with markdown caching
- **Action update consolidation** - Merged multiple per-action update functions into a single `updateContextActions` method
- **Input controller literal editor management** - Migrated literal editor pointers from `InputController` members to `EditorContext` hash table, removing redundant attach/setter interfaces
- **Edit transaction merging** - Added text delta merging, cursor update, and mutable access interfaces to `EditTransaction`; implemented undo stack transaction coalescing with max depth limit
- **LiteralBlockUtil** - New utility class for preview generation and front matter insertion
- **SourceRangeUtil** - Extracted common utility functions (line-to-offset, list marker parsing, main paragraph lookup) from scattered controllers into a shared utility
- **MathDimension** - Extracted size conversion logic from `MathParser`/`MathParserEnvironment` into a standalone module with unified unit handling
- **Paragraph and stylize controller refactoring** - Rewrote cursor offset conversion logic using the new source range model

### Fixed
- **CI release workflow PATH** - Fixed `cmake: command not found` on Linux and macOS by replacing `env: PATH:` override (which resolved to empty) with shell-level conditional PATH modification
- **Inline projection text matching** - Improved tolerance for non-standard or repeated inline node patterns in projection matching
- **Editor refresh dirty block loss** - Fixed editor refresh logic to preserve dirty blocks and avoid update loss
- **Word count duplicate definition** - Removed duplicate `countWords` function definition

## [0.1.3] - 2026-06-08

### Added
- **Footnote and link definition blocks** - Full support for footnote definitions (`[^id]: text`) and link reference definitions (`[id]: url "title"`) with rendering, editing, insertion commands, and deletion
- **Heading badges** - Visual H3–H6 level badges painted on the left side of headings for quick hierarchy identification
- **Document printing** - Print the current document via File → Print (Ctrl+P), powered by Qt PrintSupport
- **Table copy and format** - `copyCurrentTable` and `formatCurrentTableSource` commands for copying and reformatting table Markdown source
- **Insert Table dialog** - Dialog for inserting a new table with configurable row and column count
- **Table submenu** - Dedicated Table submenu in the Edit menu with keyboard shortcuts for table operations

### Changed
- **Definition block parsing and serialization** - Rewrote definition block (link references, footnotes) token model with syntax/editable slot separation, multi-line footnote support, and format-preserving serialization
- **Hit testing for definition blocks** - Token-based hit testing for precise cursor positioning and interaction within definition blocks
- **Table header defaults** - Default table headers are now empty strings instead of "Header"
- **Table theme colors** - Adjusted table header background colors across all built-in themes (GitHub, Newsprint, Night, Pixyll, Whitey)
- **Heading badge vertical centering** - Fixed badge vertical alignment calculation

### Fixed
- **macOS macdeployqt lookup** - Fixed Qt tool discovery on macOS by traversing upward from the framework library to find the Qt installation prefix
- **Placeholder text translation context** - Fixed placeholder text using incorrect translation context
- **File menu print action** - Corrected print action parameters and removed extraneous boolean argument
- **Removed stale menu binding** - Removed obsolete `document_list` menu item binding, unified to `file_tree`

## [0.1.2] - 2026-06-07

### Added
- **Replace submenu** - Edit → Find and Replace submenu with separate Find (Ctrl+F), Replace (Ctrl+H), Find Next (F3), and Find Previous (Shift+F3) actions
- **Render-mode find improvements** - Find now searches the entire Markdown document with wrap-around support and cursor position tracking across consecutive searches
- **Replace bar UI** - Replace row in the find bar with Replace and Replace All buttons, toggled by the Replace menu action
- **Dynamic language switching for find bar** - FindBarWidget immediately updates all button texts and placeholders when the application language changes

### Changed
- **FindBarWidget i18n architecture** - Extracted all `tr()` calls into a dedicated `retranslateUi()` method with `changeEvent()` override for `QEvent::LanguageChange`
- **lupdate namespace context fix** - Fixed `lupdate` context name generation across 17 `.cpp` files by replacing `namespace muffin { }` wrapping with fully qualified `muffin::ClassName::method()` definitions, ensuring runtime translation context matches the generated `.ts` context
- **Translation files updated** - All 14 language `.ts` files updated with correct `muffin::` prefixed contexts and complete Find/Replace string translations

## [0.1.1] - 2026-06-07

### Added
- **Link hover cursor** - Mouse cursor changes to pointing hand when hovering over inline links, reference links, and autolinks in the rendered view
- **Ctrl+Click link navigation** - Ctrl+Left-click on any link (inline `[text](url)`, reference `[text][ref]`, autolink `<url>`) opens it in the system browser via `QDesktopServices::openUrl()`
- **Line break menu** - Edit → Line Breaks submenu with Windows (CRLF) / Unix (LF) radio selection, controlling the line ending style used when saving files
- **Trailing newline on save** - Optional "Ensure Trailing Newline on Save" checkbox in the Line Breaks submenu, ensuring saved files always end with a newline character
- **Changelog menu action** - Help → Changelog now opens the GitHub changelog page in the browser

### Changed
- **File save pipeline** - Removed `QIODevice::Text` flag from file I/O; line endings are now explicitly controlled by the user's Line Breaks preference instead of platform-dependent Qt translation
- **File read pipeline** - All line endings are normalized to LF on load, regardless of the file's original line ending style
- **Typewriter mode scrolling** - Extended scroll bar range when typewriter mode is enabled, allowing the cursor to be vertically centered even in blank or short documents
- **Preferences dialog** - Restructured into multi-page layout with Files, Editor, Image, Markdown, Export, Appearance, and General pages

### Removed
- **"What's New..." menu item** - Removed from the Help menu
- **"Include beta updates" checkbox** - Removed from the General preferences page

### Fixed
- **Typewriter mode in short documents** - Fixed cursor not centering vertically when the document is shorter than the viewport

[0.1.0]: https://github.com/jstzwj/Muffin/releases/tag/v0.1.0

### Added
- **WYSIWYG Markdown editor** - Single-pane editing with live rendered output, keeping Markdown as the source of truth
- **Source mode** - Toggle between rendered and source code editing views with cursor position synchronization
- **Focus mode (F8)** - Dim all blocks except the cursor's active block to 35% opacity for distraction-free writing
- **Typewriter mode (F9)** - Keep the cursor vertically centered with smooth animated scrolling using `QPropertyAnimation` with `OutCubic` easing
- **Syntax highlighting** - Code block syntax highlighting powered by tree-sitter with support for 20+ languages (C, C++, Python, JavaScript, TypeScript, Go, Rust, Java, Ruby, C#, Bash, HTML, CSS, JSON, YAML, TOML, Lua, PHP, XML, Objective-C, QML, PowerShell, INI, Markdown)
- **Math rendering** - LaTeX math formula rendering via KaTeX with dual-pane edit/preview layout
- **Editable tables** - Inline table editing with resize, column alignment, row/column insertion, and delete operations
- **Editable code blocks** - Inline code block editing with language selection via tree-sitter-powered autocomplete
- **Editable HTML blocks** - Inline HTML block editing
- **Front Matter support** - YAML front matter parsing and rendering
- **Document outline sidebar** - Navigate document headings via an outline panel
- **File tree sidebar** - Browse and open files from a folder tree panel
- **Multi-format copy** - Copy content as Markdown, HTML, or plain text
- **Find and replace** - Search and replace bar with regex support
- **Block movement** - Move paragraphs up and down with keyboard shortcuts
- **Paragraph commands** - Toggle block types (heading levels, code, formula, etc.) via paragraph menu
- **Appearance settings** - Preferences panel for theme, font size, zoom, status bar visibility, focus mode, and typewriter mode
- **Built-in themes** - GitHub, Newsprint, Night, Pixyll, and Whitey color themes
- **Internationalization** - Multi-language UI support with English, Simplified Chinese, Japanese, Vietnamese, French, Spanish, and Russian translations
- **Status bar** - Parse time, cursor position, and word count display with sidebar and source mode toggle buttons
- **Installer packages** - Official installers for Windows (MSIX) and macOS (DMG)
- **CI/CD pipeline** - Cross-platform GitHub Actions workflow for building and testing on Windows, macOS, and Linux

### Changed
- **Incremental layout refresh** - Optimized document re-rendering to only refresh changed top-level blocks instead of rebuilding the entire layout
- **Viewport-aware re-layout** - Adaptive layout rebuilding based on viewport position for smooth scrolling performance
- **Text delta editing** - Replaced full-text replacement with incremental text delta updates for efficient editing
- **Node snapshot cursor remapping** - Cursor position preservation across undo/redo operations using snapshot-based remapping
- **Inline projection** - Replaced `InlineSourceMap` with `InlineProjection` for accurate source offset mapping between rendered and source views
- **Source cursor synchronization** - Bidirectional cursor position sync when switching between rendered and source views
- **Table editing refactoring** - Rewrote table cell editing to preserve rich text formatting during edits
- **List marker rendering** - Restructured list marker rendering with dedicated marker type management
- **Source editor replacement** - Replaced plain text source editor with a custom syntax-highlighted component
- **Font handling** - Refactored cross-platform font selection with adaptive fallback chains
- **Code block and table toolbar** - Refactored language editor and table toolbar into standalone components
- **Editor controller decomposition** - Split monolithic editor controller into focused sub-controllers (block, inline, table, math, code, HTML, stylize)
- **Input controller and math parser** - Restructured input handling and math parsing for better separation of concerns

### Fixed
- **Table cell deletion** - Fixed deletion inside table cells incorrectly removing Markdown syntax markers
- **Table cell selection** - Fixed text selection and serialization logic within table cells
- **Heading block boundaries** - Fixed backspace and delete key handling at heading block edges
- **Empty block creation** - Fixed empty paragraph creation at document boundaries
- **Code block indentation** - Fixed Tab key behavior at non-starting positions in list items
- **Backtab in blocks** - Properly handle Shift+Tab in code, math, and HTML blocks
- **List indentation** - Fixed list item indent/outdent logic
- **Cross-platform build** - Added `libxcb-util-dev` dependency for Linux CI and offscreen rendering environment for macOS tests

[0.5.0]: https://github.com/jstzwj/Muffin/releases/tag/v0.5.0
[0.4.1]: https://github.com/jstzwj/Muffin/releases/tag/v0.4.1
[0.3.0]: https://github.com/jstzwj/Muffin/releases/tag/v0.3.0
[0.2.8]: https://github.com/jstzwj/Muffin/releases/tag/v0.2.8
[0.2.7]: https://github.com/jstzwj/Muffin/releases/tag/v0.2.7
[0.2.6]: https://github.com/jstzwj/Muffin/releases/tag/v0.2.6
[0.2.5]: https://github.com/jstzwj/Muffin/releases/tag/v0.2.5
[0.2.4]: https://github.com/jstzwj/Muffin/releases/tag/v0.2.4
[0.2.3]: https://github.com/jstzwj/Muffin/releases/tag/v0.2.3
[0.2.2]: https://github.com/jstzwj/Muffin/releases/tag/v0.2.2
[0.2.1]: https://github.com/jstzwj/Muffin/releases/tag/v0.2.1
[0.2.0]: https://github.com/jstzwj/Muffin/releases/tag/v0.2.0
[0.1.6]: https://github.com/jstzwj/Muffin/releases/tag/v0.1.6
[0.1.5]: https://github.com/jstzwj/Muffin/releases/tag/v0.1.5
[0.1.4]: https://github.com/jstzwj/Muffin/releases/tag/v0.1.4
[0.1.3]: https://github.com/jstzwj/Muffin/releases/tag/v0.1.3
[0.1.2]: https://github.com/jstzwj/Muffin/releases/tag/v0.1.2
[0.1.1]: https://github.com/jstzwj/Muffin/releases/tag/v0.1.1
[0.1.0]: https://github.com/jstzwj/Muffin/releases/tag/v0.1.0
