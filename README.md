<div align="center">

<img src="logo.svg" alt="Muffin" width="220">

# Muffin

**A native, lightweight, WYSIWYG Markdown editor.**

Muffin is a block-level WYSIWYG Markdown editor built from the ground up in C++ and Qt 6. Your Markdown file stays the single source of truth, rendered as a page you edit directly: no split panes, no preview pane, no typing lag.

[Download](#download) · [Features](#features) · [Build from source](#development) · [Architecture](#architecture)

![C++](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=c%2B%2B&logoColor=white)
![Qt](https://img.shields.io/badge/Qt-6-41CD52?logo=qt&logoColor=white)
![Platforms](https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20%7C%20Linux-2ea44f)
![UI Languages](https://img.shields.io/badge/UI_languages-15-blueviolet)
[![Releases](https://img.shields.io/badge/releases-GitHub-181717?logo=github)](https://github.com/jstzwj/Muffin/releases)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

<sup>Translations:</sup>
<a href="README.zh.md">中文</a>

</div>

## Why Muffin

- **Native, not a web app** — A real C++/Qt desktop application. No bundled Chromium, no Node runtime, no web stack — just a fast cold start and a small memory footprint.
- **Opens huge files instantly** — A lazy, viewport-aware layout renders only the blocks on screen, so multi-megabyte documents open without freezing. Incremental parsing and text-delta editing keep typing responsive at any size.
- **True WYSIWYG editing** — Write and edit directly on the rendered page, with the Markdown kept in sync underneath. No side-by-side preview, no render delay.
- **Markdown as the source of truth** — Your `.md` file round-trips cleanly, and a synchronized source mode lets you drop into raw Markdown anytime with full cursor round-tripping between the two views.
- **Themeable end to end** — One theme definition drives the rendered page, the source editor, and every piece of chrome (menus, sidebar, dialogs, status bar). The built-in themes are authored as plain CSS, and you can drop in your own `.css` (or `.json`) theme.

### How it compares

Muffin is the only fully native, fully open-source WYSIWYG editor in this group — and the only one that stays responsive on very large files.

| | Muffin | Typora | MarkText | Obsidian |
|:--|:--:|:--:|:--:|:--:|
| Core technology | C++ / Qt 6 | Electron | Electron | Electron |
| Open source | ✅ | ❌ | ✅ | ❌ |
| WYSIWYG | ✅ | ✅ | ✅ | ❌ |
| Native UI | ✅ | ❌ | ❌ | ❌ |
| Local-first | ✅ | ✅ | ✅ | ✅ |
| Free | ✅ | Paid | ✅ | Free for personal use |
| Large files | ✅ | ⚠️ | ⚠️ | ⚠️ |

<sub>Obsidian's Live Preview blends rendered Markdown with raw syntax rather than offering true inline WYSIWYG. ⚠️ marks editors that can open very large documents but may lag while scrolling or editing.</sub>

<br />

## Features

### ✍️ Editing

- **Live WYSIWYG editing** — Write and edit directly in the rendered view. No split panes, no preview delay.
- **Source mode** — Toggle to a syntax-highlighted raw Markdown editor with full cursor synchronization between views.
- **Focus mode** (`F8`) — Dim all blocks except the active one, so you can concentrate on what you're writing.
- **Typewriter mode** (`F9`) — Keep the cursor vertically centered with smooth animated scrolling that feels like paper.
- **Smart punctuation** — Curl straight quotes, turn `--`/`---` into en/em dashes, and `...` into an ellipsis as you type. An optional render-only mode beautifies punctuation without touching your Markdown source.
- **Extended Markdown** — GitHub-style alerts (`[!NOTE]`, `[!TIP]`, `[!WARNING]`, …), `==highlight==`, `~subscript~`, `^superscript^`, Setext headings, and `\[ ... \]` LaTeX math blocks.
- **Spell checking** — Nuspell-powered spell checking with misspelling underlines in both render and source modes, a right-click suggestion menu, ignore-word support, and bundled dictionaries for 11 languages.
- **Emoji autocomplete** — Type `:` followed by a shortcode to open a popup emoji picker backed by a bundled dataset, then accept with `Tab`; enabled by default while typing.
- **Editable tables** — Add, resize, align, and delete rows and columns inline. Insert tables via a configurable dialog.
- **Editable code blocks** — Inline editing with syntax highlighting for 20+ languages via tree-sitter. Set the language from an autocomplete dropdown. Code Tools add line-by-line indent/dedent and a copy-block-content action.
- **Editable math blocks** — Write LaTeX expressions rendered live by a full KaTeX-compatible engine written in C++, with a dual-pane edit/preview layout. Supports user-defined macros, braket notation, commutative diagrams, and a one-click "Refresh All" to re-render every formula.
- **Native Mermaid diagrams** — Render flowchart, sequence, class, state, ER, requirement, pie, quadrant, journey, radar, XY, timeline, packet, Kanban, mindmap, TreeView, Event Modeling, Gantt, and Info diagrams live from ```` ```mermaid ```` fences through a pure C++/Qt engine, with no JavaScript or browser runtime dependency. Invalid source stays editable with exact line/column diagnostics, an in-source error marker, and click-to-jump navigation. Shared title/accessibility metadata, rounded flow routes, safe links and tooltips, live animated edges, Sequence link menus, deterministic native SVG/HTML export, and a generated 276-row configuration-effect matrix keep editor and export behavior explicit.
- **Editable HTML blocks** — Edit raw HTML blocks inline with Lexbor-based parsing and Yoga-based flexbox layout, themed to match the active document.
- **Footnotes & link definitions** — Full support for footnote (`[^id]: text`) and link reference definitions with rendering, editing, and insertion commands.
- **Front matter** — Full YAML front matter support.
- **Rich paragraph commands** — Toggle headings, code fences, math blocks, and more from the paragraph menu.
- **Block movement** — Move paragraphs up and down with keyboard shortcuts.
- **Configurable indent** — Choose the default indent size (2/4/8 spaces), with an optional "Align Indent" command to match surrounding indentation.
- **Find & replace** — Built-in search bar with regex support, wrap-around, and replace/replace-all.
- **Multi-format copy** — Copy selected content as Markdown, HTML, or plain text.
- **Copy as Markdown** — Optional preference to copy the underlying Markdown source when copying as plain text.
- **Whole-line copy & cut** — With no selection, copy and cut operate on the entire current line.
- **Link interaction** — Hover cursor changes on links; Ctrl+Click to open in the system browser.
- **Line break rendering** — Render a single newline as a hard line break, or join soft-wrapped lines into one paragraph per CommonMark, controlled by a Markdown preference. Hard `<br>` line breaks render and edit in all three forms (`<br>`, `<br/>`, `<br />`).
- **Line break preferences** — Choose Windows (CRLF) or Unix (LF) line endings, with an optional trailing newline on save.

### 🧭 Navigation & Organization

- **Document outline** — Jump to any heading from the sidebar outline, with collapsible subtrees for long documents.
- **Heading badges** — Visual level badges (H3–H6) painted alongside headings for quick hierarchy identification.
- **File tree sidebar** — Browse and open files from a folder tree.
- **Quick Open** — Fuzzy-jump to any file in the open workspace, with recently used and recently modified files surfaced first.
- **File operations** — Move, delete, reveal in file manager, reopen-with-encoding, and save-all-open-files from the file menu.
- **Draft recovery** — Recover unsaved work after a crash or unexpected exit.
- **Autosave** — Optional periodic saving and save-on-exit to protect unsaved work.
- **Custom status bar** — A self-painted, theme-aware status bar showing parse time, cursor position, and word count, with a click-to-open statistics popup (words, characters, lines, reading time, and selection count) and an inline spell-check language quick-switcher.
- **In-app help** — Quick Start, Markdown Reference, and Acknowledgements available from the Help menu in a built-in viewer rendered by the native Markdown engine, with back/forward navigation.
- **Automatic update checks** — Notified of new releases on startup (once per 24 hours), with a manual check from the Help menu.

### 📤 Export & Import

- **Multi-format export** — Export to PDF (native renderer), HTML, and plain HTML natively; Word (DOCX), ODT, RTF, ePub, LaTeX, MediaWiki, RST, Textile, and OPML via an external [Pandoc](https://pandoc.org) process.
- **Import via Pandoc** — Convert documents from other formats into Markdown through File → Import.
- **Document printing** — Print the current document via File → Print (Ctrl+P).

### 🖼️ Images

- **Image editing** — Insert local or network images, drag-and-drop upload, right-click context menu, preview rendering, and batch processing. WebP and AVIF formats supported, plus bundled PNG/JPEG decoders so images load reliably regardless of Qt's plugin availability.
- **Image insertion policy** — A centralized system governing image insertion across paste, dialog, and drag-and-drop with six configurable actions (no action, copy to `./`, `./assets`, `./<filename>.assets`, upload, or a custom folder), honoring front-matter upload overrides and configurable relative-path, leading-slash, and URL-escaping formatting.
- **Custom-command upload** — Upload images through a configurable external command, parsing its stdout lines as image URLs.

### 🎨 Appearance

- **5 built-in themes** — GitHub, Newsprint, Night, Pixyll (now with a serif body font), and Whitey.
- **Custom themes** — The built-in themes are authored as plain CSS, and you can author your own the same way: drop a `.css` (or `.json`) file into the themes folder and it shows up in the live Theme menu instantly. Import themes directly from the menu or the Appearance preferences page.
- **Appearance preferences** — Font size, zoom level, focus mode, typewriter mode, and status bar visibility — all persisted across sessions.
- **Always-on-top** — Keep the window above all others (Ctrl+Shift+F).
- **15 UI languages** — English, 简体中文, 繁體中文, 日本語, 한국어, Tiếng Việt, Français, Español, Deutsch, Português (Brasil), Русский, Italiano, Türkçe, Polski, and Nederlands.

### ⚡ Performance

- **Native C++/Qt** — No Electron. Fast startup, low memory, and smooth scrolling.
- **Lazy viewport layout** — Opening a document computes only cheap height estimates for the whole file; full text shaping, syntax highlighting, math, and HTML rendering are deferred to blocks as they scroll into view. Anchor-corrected scrolling keeps the page steady while offscreen blocks are promoted, so large files open and scroll without lag.
- **Incremental parsing** — Only changed blocks are re-parsed and re-rendered.
- **Incremental layout** — Top-level block range diffing avoids full layout rebuilds on edits.
- **Text delta editing** — Sends incremental text updates instead of full document replacement.

## Download

|         | Windows | macOS | Linux |
|:--------|:-------:|:-----:|:-----:|
| Install | [MSI](https://github.com/jstzwj/Muffin/releases) | [DMG](https://github.com/jstzwj/Muffin/releases) | [Build from source](#development) |

## Development

Muffin uses [Conan](https://conan.io/) for dependency management and CMake for building. You need a C++20 compiler (MSVC 2022+, GCC 12+, or Clang 15+), Qt 6 (installed via Conan), Conan 2.x, and CMake 3.24+.

### Build

```bash
# Detect your Conan profile
conan profile detect --force

# Install dependencies
conan install . -s build_type=Release -s compiler.cppstd=20 --build=missing

# Configure and build
cmake --preset conan-default
cmake --build --preset conan-release
```

### Test

```bash
ctest --preset conan-release --output-on-failure
```

### Run

```bash
# Build the distributable bundle
cmake --build --preset conan-release --target dist

# Launch
./build/dist/Muffin          # Linux / macOS
.\build\dist\Muffin.exe      # Windows
```

See [CLAUDE.md](CLAUDE.md) for additional build details and common pitfalls.

### Translations

```bash
cmake --build --preset conan-release --target update_translations   # Extract strings
cmake --build --preset conan-release --target release_translations   # Compile .qm files
```

## Architecture

Muffin uses a native block tree as its runtime model. On import, Markdown is parsed into structured, editable blocks; on save, the block tree is re-serialized into normalized Markdown. Bidirectional inline projection keeps the rendered view and the raw source mapped at all times.

| Layer | Responsibility |
| --- | --- |
| `app` | MainWindow, preferences, sidebar, quick open, and UI language management. |
| `editor` | The rendered editing surface, source editor, find bar, and input handling. |
| `render` | Layout engine and block/inline painting, driven by themes. |
| `document` | Markdown document model, outline, and source round-trip mapping. |
| `parser` | Markdown parsing via cmark-gfm, fed by incremental text deltas. |
| `blocks` | Per-block runtimes: code, table, math, HTML, front matter, link refs, literal. |
| `html` | HTML block layout engine — Lexbor parsing and Yoga flexbox used by HTML blocks. |
| `edit` | Text editing operations: insert, delete, replace, and block movement. |
| `projection` | Bidirectional offset mapping between the rendered view and raw Markdown. |
| `export` | Native PDF/HTML export and the Pandoc runner for other formats. |
| `math` | KaTeX-compatible math rendering. |
| `unicode` | Word-boundary segmentation for cursor movement and selection. |
| `theme` | Unified theme definitions, chrome style-sheet generation, and runtime theme management. |
| `image` | Image insertion policy and custom-command upload. |
| `io` | File I/O, encoding, and image file operations. |
| `spellcheck` | Nuspell spell checking and bundled dictionaries. |
| `commands` | Command registry decoupling menu actions from their implementations. |

### Third-party dependencies

| Library | Purpose | License |
|---------|---------|---------|
| Qt 6 | GUI framework | LGPL-3.0 |
| cmark-gfm | GitHub-Flavored Markdown parsing | BSD-2-Clause |
| tree-sitter | Code syntax highlighting | MIT |
| KaTeX | Math formula rendering (fonts bundled) | MIT |
| Yoga | Flexbox layout for HTML blocks | MIT |
| Lexbor | HTML parsing | Apache-2.0 |
| Nuspell | Spell checking | LGPL-3.0-or-later |
| ICU | Unicode text processing | ICU License |
| libwebp / libavif / dav1d | WebP and AVIF image decoding (libavif built from vendored source, with dav1d as its AV1 decoder) | BSD-3-Clause / BSD-2-Clause |
| libpng / libjpeg | PNG and JPEG image decoding | libpng / libjpeg |

Third-party sources live in `third_party/` and are built as part of the CMake project.

## Roadmap

Muffin already covers nearly all of core and extended Markdown — headings, paragraphs, lists, task lists, blockquotes, tables, code blocks, inline formatting, links, reference-style links and images, footnotes, front matter, math, and HTML — plus multi-format export and import. Ongoing work includes:

- [ ] Polish the rendered editor surface — selection, cursor, IME, and local refresh edge cases.
- [x] Add native Mermaid flowchart, sequence, class, state, ER, requirement, pie, quadrant, journey, radar, XY, timeline, packet, Kanban, mindmap, TreeView, Event Modeling, Gantt, and Info diagrams.
- [x] Add safe Mermaid links/tooltips, live Flowchart edge animation, and Sequence participant menus.
- [x] Add deterministic native Mermaid SVG export, including inline HTML output and single-diagram context-menu saving.
- [ ] Finish absolute SVG marker URL parity and continue adding Mermaid families.
- [ ] Continue extending GFM coverage.
- [ ] Harden performance — structured 1–100 MB parser phases scale approximately linearly; AST node bodies are now 96 B smaller each and the 100 MiB roundtrip uses about 834 MiB less resident memory. Next, reduce cmark's parse-tree peak and very-large-document layout cost.
- [ ] Accessibility — keyboard navigation improvements and screen reader support.

## Contributing

Contributions are welcome! Please read the [Contributing Guide](CONTRIBUTING.md) to get started.

If you have a bug report or feature request, feel free to [open an issue](https://github.com/jstzwj/Muffin/issues). When doing so, please include your OS, steps to reproduce, and any relevant screenshots.

## License

[**MIT**](LICENSE)

## Star History

[![Star History Chart](https://api.star-history.com/chart?repos=jstzwj/Muffin&type=date)](https://star-history.com/#jstzwj/Muffin&type=date)
