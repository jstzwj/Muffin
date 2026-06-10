# Changelog

All notable changes to Muffin will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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

[0.2.1]: https://github.com/jstzwj/Muffin/releases/tag/v0.2.1
[0.2.0]: https://github.com/jstzwj/Muffin/releases/tag/v0.2.0
[0.1.6]: https://github.com/jstzwj/Muffin/releases/tag/v0.1.6
[0.1.5]: https://github.com/jstzwj/Muffin/releases/tag/v0.1.5
[0.1.4]: https://github.com/jstzwj/Muffin/releases/tag/v0.1.4
[0.1.3]: https://github.com/jstzwj/Muffin/releases/tag/v0.1.3
[0.1.2]: https://github.com/jstzwj/Muffin/releases/tag/v0.1.2
[0.1.1]: https://github.com/jstzwj/Muffin/releases/tag/v0.1.1
[0.1.0]: https://github.com/jstzwj/Muffin/releases/tag/v0.1.0
