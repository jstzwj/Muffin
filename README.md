<div align="center">

<img src="logo.svg" alt="Muffin" width="220">

# Muffin

**A native, lightweight Markdown editor with a live rendered writing surface.**

Muffin is a block-level WYSIWYG Markdown editor built in C++ and Qt 6. It keeps your Markdown file as the single source of truth while rendering the document as a directly editable page — no split panes, no preview lag, no Electron. Edit the rendered output and let the editor maintain the Markdown underneath.

Muffin also ships a synchronized source mode, so you can fall back to raw Markdown whenever you want, with full cursor round-tripping between the two views.

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

<br />

## Features

### ✍️ Editing

- **Live WYSIWYG editing** — Write and edit directly in the rendered view. No split panes, no preview delay.
- **Source mode** — Toggle to a syntax-highlighted raw Markdown editor with full cursor synchronization between views.
- **Focus mode** (`F8`) — Dim all blocks except the active one, so you can concentrate on what you're writing.
- **Typewriter mode** (`F9`) — Keep the cursor vertically centered with smooth animated scrolling that feels like paper.
- **Editable tables** — Add, resize, align, and delete rows and columns inline. Insert tables via a configurable dialog.
- **Editable code blocks** — Inline editing with syntax highlighting for 20+ languages via tree-sitter. Set the language from an autocomplete dropdown.
- **Editable math blocks** — Write LaTeX expressions rendered live by a full KaTeX-compatible engine written in C++, with a dual-pane edit/preview layout. Supports user-defined macros, braket notation, commutative diagrams, and more.
- **Editable HTML blocks** — Edit raw HTML blocks inline with Lexbor-based parsing and Yoga-based flexbox layout.
- **Image editing** — Insert local or network images, drag-and-drop upload, right-click context menu, preview rendering, and batch processing. WebP and AVIF formats supported.
- **Footnotes & link definitions** — Full support for footnote (`[^id]: text`) and link reference definitions with rendering, editing, and insertion commands.
- **Front matter** — Full YAML front matter support.
- **Rich paragraph commands** — Toggle headings, code fences, math blocks, and more from the paragraph menu.
- **Block movement** — Move paragraphs up and down with keyboard shortcuts.
- **Find & replace** — Built-in search bar with regex support, wrap-around, and replace/replace-all.
- **Multi-format copy** — Copy selected content as Markdown, HTML, or plain text.
- **Link interaction** — Hover cursor changes on links; Ctrl+Click to open in the system browser.
- **Document printing** — Print the current document via File → Print (Ctrl+P).
- **Line break preferences** — Choose Windows (CRLF) or Unix (LF) line endings, with an optional trailing newline on save.

### 🧭 Navigation & Organization

- **Document outline** — Jump to any heading from the sidebar outline panel.
- **Heading badges** — Visual level badges (H3–H6) painted alongside headings for quick hierarchy identification.
- **File tree sidebar** — Browse and open files from a folder tree.
- **Status bar** — Parse time, cursor position, word count, and quick toggles for sidebar and source mode.

### 🎨 Appearance

- **5 built-in themes** — GitHub, Newsprint, Night, Pixyll, and Whitey.
- **Appearance preferences** — Font size, zoom level, focus mode, typewriter mode, and status bar visibility — all persisted across sessions.
- **Always-on-top** — Keep the window above all others (Ctrl+Shift+F).
- **15 UI languages** — English, 简体中文, 繁體中文, 日本語, 한국어, Tiếng Việt, Français, Español, Deutsch, Português (Brasil), Русский, Italiano, Türkçe, Polski, and Nederlands.

### ⚡ Performance

- **Native C++/Qt** — No Electron. Fast startup, low memory, and smooth scrolling.
- **Incremental parsing** — Only changed blocks are re-parsed and re-rendered.
- **Incremental layout** — Top-level block range diffing avoids full layout rebuilds.
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
| `app` | MainWindow, preferences, sidebar, and UI language management. |
| `editor` | The rendered editing surface, source editor, find bar, and input handling. |
| `render` | Layout engine and block/inline painting, driven by themes. |
| `document` | Markdown document model, outline, and source round-trip mapping. |
| `parser` | Markdown parsing via cmark-gfm, fed by incremental text deltas. |
| `blocks` | Per-block runtimes: code, table, math, HTML, front matter, link refs, literal. |
| `edit` | Text editing operations: insert, delete, replace, and block movement. |
| `projection` | Bidirectional offset mapping between the rendered view and raw Markdown. |
| `export` | File export pipeline. |
| `math` | KaTeX-compatible math rendering. |
| `theme` | Theme definitions and runtime theme management. |
| `commands` | Command registry decoupling menu actions from their implementations. |
| `io` | File I/O and encoding. |
| `diagnostics` | Debug and profiling utilities. |
| `unicode` | Unicode text utilities. |

### Third-party dependencies

| Library | Purpose | License |
|---------|---------|---------|
| Qt 6 | GUI framework | LGPL-3.0 |
| cmark-gfm | Markdown parsing | BSD-2-Clause |
| tree-sitter | Syntax highlighting for code blocks | MIT |
| KaTeX | Math formula rendering | MIT |

Third-party sources live in `third_party/` and are built as part of the CMake project.

## Roadmap

Muffin already covers nearly all of core and extended Markdown — headings, paragraphs, lists, task lists, blockquotes, tables, code blocks, inline formatting, links, reference-style links and images, footnotes, front matter, math, and HTML. Ongoing work includes:

- [ ] Polish the rendered editor surface — selection, cursor, IME, and local refresh edge cases.
- [ ] Expand export flows — PDF, HTML, and DOCX export with template support.
- [ ] Harden performance — large-document stress tests, roundtrip verification, and profiling diagnostics.
- [ ] Accessibility — keyboard navigation improvements and screen reader support.

## Contributing

Contributions are welcome! Please read the [Contributing Guide](CONTRIBUTING.md) to get started.

If you have a bug report or feature request, feel free to [open an issue](https://github.com/jstzwj/Muffin/issues). When doing so, please include your OS, steps to reproduce, and any relevant screenshots.

## License

[**MIT**](LICENSE)

## Star History

[![Star History Chart](https://api.star-history.com/chart?repos=jstzwj/Muffin&type=date)](https://star-history.com/#jstzwj/Muffin&type=date)
