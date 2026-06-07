# Changelog

All notable changes to Muffin will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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

[0.1.2]: https://github.com/jstzwj/Muffin/releases/tag/v0.1.2
[0.1.1]: https://github.com/jstzwj/Muffin/releases/tag/v0.1.1
[0.1.0]: https://github.com/jstzwj/Muffin/releases/tag/v0.1.0
