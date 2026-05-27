# Muffin

A fast, lightweight native Markdown editor built with C++ and Qt 6 Widgets.

**[中文文档](README_zh.md)**

---

## Why Muffin?

Many modern Markdown editors are built on web runtimes, which can bring larger package sizes and more runtime overhead. Muffin explores a native Qt implementation that keeps the editing experience focused and lightweight:

- **Fast startup** — native C++, no Chromium runtime
- **Small footprint** — designed around Qt Widgets and native rendering
- **Low memory usage** — no embedded browser engine
- **Cross-platform** — Windows, macOS, and Linux from one codebase

---

## Features

| Status | Feature |
|:------:|---------|
| Done | Qt Widgets desktop shell with a native Chinese menu bar |
| Done | File open/save/save-as/new document workflow |
| Done | Command-line file opening and screenshot capture for visual checks |
| Done | GitHub Flavored Markdown parsing via cmark-gfm |
| Done | Read-only rendered Markdown view backed by `QTextDocument` |
| Done | Centered document column with status bar word count |
| Done | Improved rendering for headings, horizontal rules, tables, code blocks, inline code, links, lists, and blockquotes |
| Done | Theme menu with Github, Newsprint, Night, Pixyll, and Whitey presets |
| Done | Practical menu actions: new window, recent files, file properties, open file location, print, fullscreen, stay-on-top, zoom, word count dialog, and source wrap |
| WIP | Source mode / rendered mode switching |
| Planned | True WYSIWYG editing with inline Markdown syntax reveal/hide |
| Planned | Sidebar, outline, file tree, search, settings, export, images, and math rendering |

---

## Tech Stack

| Component | Technology |
|-----------|------------|
| Language | C++17 |
| UI Framework | Qt 6 Widgets |
| Rendering Model | `QTextDocument`, `QTextBrowser`, `QTextCursor` |
| Markdown Parser | [cmark-gfm](https://github.com/github/cmark-gfm) |
| Build System | CMake 3.24+ |
| Package Manager | Conan 2.x |

### Architecture Overview

```text
Markdown source
      │
      ▼
cmark-gfm AST
      │
      ▼
DocumentRenderer + ThemeStylesheet
      │
      ▼
QTextDocument rendered view
```

Muffin keeps raw Markdown as the source of truth. The parsed AST and rendered `QTextDocument` are derived from that source, and theme changes re-render the document without changing the Markdown text.

---

## Building

### Prerequisites

| Requirement | Version |
|-------------|---------|
| CMake | >= 3.24 |
| C++ Compiler | MSVC 2022, GCC 11+, or Clang 14+ |
| Qt | 6.5+ with Widgets, Gui, and PrintSupport |
| Conan | 2.x recommended |

### Option A: Build with system Qt

Make sure `Qt6_DIR` is set, for example:

```text
C:\Qt\6.8.3\msvc2022_64\lib\cmake\Qt6
```

Then build:

```bash
cmake --preset dev
cmake --build build/dev --config Debug
```

### Option B: Build with Conan

```bash
conan install . --build=missing -s compiler.cppstd=17 -of build/conan
cmake --preset dev-conan
cmake --build build/dev --config Debug
```

### Run

```bash
./build/dev/muffin
```

On Windows/MSVC multi-config builds:

```bash
./build/dev/Debug/muffin
./build/dev/Muffin-dist/muffin.exe
```

Open a file from the command line:

```bash
./build/dev/Muffin-dist/muffin.exe README.md
```

Capture a visual-check screenshot:

```bash
./build/dev/Muffin-dist/muffin.exe README.md --screenshot screenshot.png
```

### Run Tests

```bash
ctest --test-dir build/dev --output-on-failure
```

On Windows/MSVC multi-config builds:

```bash
ctest --test-dir build/dev -C Debug --output-on-failure
```

---

## Development Roadmap

| Phase | Scope | Status |
|:-----:|-------|:------:|
| 1 | Project skeleton, build system, basic window, open/save | Done |
| 2 | Markdown parsing, rendered preview, themes, native editor chrome, menu actions | WIP |
| 3 | True WYSIWYG editing and inline Markdown syntax reveal/hide | Planned |
| 4 | Sidebar, outline, file tree, search, settings, export, images, math | Planned |
| 5 | Polish, packaging, shortcuts, i18n, release builds | Planned |

---

## Contributing

Muffin is in early development. Contributions, bug reports, and ideas are welcome.

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/my-feature`)
3. Commit your changes
4. Open a Pull Request

---

## License

MIT License. See [LICENSE](LICENSE) for details.
