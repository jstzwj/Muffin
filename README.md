<div align="center">

# Muffin

**A fast, lightweight Markdown editor with a live rendered writing surface.**

Muffin keeps your Markdown file as the source of truth while presenting the document as an editable page — no split panes, no preview lag. Edit the rendered output directly and let the editor handle the Markdown underneath.

<sup>Available for Linux, macOS, and Windows.</sup>

<br />

<!-- License -->
<a href="LICENSE">
  <img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="License: MIT">
</a>
<!-- Platform -->
<a href="#download">
  <img src="https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20%7C%20Linux-lightgrey.svg" alt="Platform">
</a>
<!-- Language -->
<a href="#appearance">
  <img src="https://img.shields.io/badge/UI_languages-14-blueviolet.svg" alt="14 UI Languages">
</a>

<br />

<sup>Translations:</sup>
<a href="README.zh.md">中文</a>

</div>

<br />

## Features

### Editing

- **Live WYSIWYG editing** — Write and edit directly in the rendered view. No split panes, no preview delay.
- **Source code mode** — Toggle to a syntax-highlighted raw Markdown editor with full cursor synchronization between views.
- **Focus mode** (`F8`) — Dim all blocks except the active one, so you can concentrate on what you're writing.
- **Typewriter mode** (`F9`) — Keep the cursor vertically centered with smooth animated scrolling that feels like paper.
- **Editable tables** — Add, resize, align, and delete rows and columns inline.
- **Editable code blocks** — Inline editing with syntax highlighting for 20+ languages via tree-sitter. Set the language from an autocomplete dropdown.
- **Editable math blocks** — Write LaTeX expressions rendered live by KaTeX, with a dual-pane edit/preview layout.
- **Editable HTML blocks** — Edit raw HTML blocks inline.
- **Front matter** — Full YAML front matter support.
- **Rich paragraph commands** — Toggle headings, code fences, math blocks, and more from the paragraph menu.
- **Block movement** — Move paragraphs up and down with keyboard shortcuts.
- **Find and replace** — Built-in search bar with regex support.
- **Multi-format copy** — Copy selected content as Markdown, HTML, or plain text.

### Navigation & Organization

- **Document outline** — Jump to any heading from the sidebar outline panel.
- **File tree sidebar** — Browse and open files from a folder tree.
- **Status bar** — Parse time, cursor position, word count, and quick toggles for sidebar and source mode.

### Appearance

- **5 built-in themes** — GitHub, Newsprint, Night, Pixyll, and Whitey.
- **Appearance preferences** — Font size, zoom level, focus mode, typewriter mode, and status bar visibility — all persisted across sessions.
- **14 UI languages** — English, 简体中文, 繁體中文, 日本語, 한국어, Tiếng Việt, Français, Español, Deutsch, Português (Brasil), Русский, Italiano, Türkçe, Polski, and Nederlands.

### Performance

- **Native C++/Qt** — No Electron. Fast startup, low memory, and smooth scrolling.
- **Incremental parsing** — Only changed blocks are re-parsed and re-rendered.
- **Incremental layout** — Top-level block range diffing avoids full layout rebuilds.
- **Text delta editing** — Sends incremental text updates instead of full document replacement.

## Download

|         | Windows | macOS | Linux |
|:--------|:-------:|:-----:|:-----:|
| Install | [MSIX (coming soon)](https://github.com/jstzwj/Muffin/releases) | [DMG (coming soon)](https://github.com/jstzwj/Muffin/releases) | [Build from source](#development) |

> Pre-built packages are on the way. In the meantime, you can build from source following the instructions below.

## Development

Muffin uses [Conan](https://conan.io/) for dependency management and CMake for building. You need a C++17 compiler, Qt 6 (installed via Conan), Conan 2.x, and CMake 3.24+.

### Build

```bash
# Detect your Conan profile
conan profile detect --force

# Install dependencies
conan install . -s build_type=Release -s compiler.cppstd=17 --build=missing

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

See [CLAUDE.md](CLAUDE.md) for additional build details.

### Translations

```bash
cmake --build --preset conan-release --target update_translations   # Extract strings
cmake --build --preset conan-release --target release_translations   # Compile .qm files
```

## Contributing

Contributions are welcome! Please read the [Contributing Guide](CONTRIBUTING.md) to get started.

If you have a bug report or feature request, feel free to [open an issue](https://github.com/jstzwj/Muffin/issues). When doing so, please include your OS, steps to reproduce, and any relevant screenshots.

## Roadmap

1. Polish the rendered editor surface — selection, cursor, IME, and local refresh edge cases.
2. Expand block transforms — headings, paragraphs, lists, blockquotes, front matter, and task list commands.
3. Finish complex block UX — tables, code fences, math blocks, HTML blocks, and images.
4. Add search/replace, richer clipboard behavior, export flows, printing, and diagnostics.
5. Harden performance with large-document stress tests and roundtrip verification.

## License

[**MIT**](LICENSE)
