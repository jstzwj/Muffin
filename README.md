# Muffin

A fast, lightweight Markdown WYSIWYG editor — a native clone of Typora built with C++ and Qt 6.

**[中文文档](README_zh.md)**

---

## Why Muffin?

Typora is a great editor, but it's built on Electron (~150 MB, slow cold start). Muffin aims for 1:1 Typora feature parity using native Qt, delivering:

- **Instant startup** — native C++, no Chromium overhead
- **Tiny footprint** — target binary < 30 MB
- **Low memory** — no embedded browser engine
- **Cross-platform** — Windows, macOS, Linux from a single codebase

---

## Features

| Status | Feature |
|:------:|---------|
| WIP | Typora-like WYSIWYG editing — seamless toggle between rendered and source |
| WIP | GitHub Flavored Markdown (tables, strikethrough, task lists, autolinks) |
| WIP | Live inline toggle — `**bold**` renders as **bold**, click to reveal syntax |
| WIP | Theme support — light, dark, custom CSS |
| Planned | Image paste / drag-drop / resize |
| Planned | Math rendering (LaTeX) |
| Planned | Export to HTML / PDF |
| Planned | Outline sidebar, file tree |
| Planned | i18n (English, Chinese) |

---

## Tech Stack

| Component | Technology |
|-----------|-----------|
| Language | C++17 |
| UI Framework | Qt 6 (Widgets) |
| Markdown Parser | [cmark-gfm](https://github.com/github/cmark-gfm) |
| Build System | CMake 3.24+ |
| Package Manager | Conan 2.x |

### Architecture Overview

```
 ┌─────────────┐     parse      ┌────────────┐    render     ┌────────────────┐
 │  Markdown    │──────────────►│  cmark-gfm │─────────────►│  QTextDocument │
 │  Source      │               │    AST      │              │  (rendered)    │
 └─────────────┘               └────────────┘              └────────────────┘
       ▲                                                           │
       │                    SyncEngine                             │
       │              (Position Mapper +                          │
       │               Inline Toggle)                             │
       └─────────────────────────────────────────────────────────┘
```

The editor maintains two parallel representations — raw Markdown source and a rendered `QTextDocument`. The **InlineToggleManager** hides/reveals Markdown syntax (e.g. `**bold**` ↔ **bold**) as the cursor moves, delivering a Typora-like experience. The raw source string is always the ground truth; the rendered view is derived from it.

---

## Project Structure

```
Muffin/
├── CMakeLists.txt            # Top-level CMake
├── CMakePresets.json         # Dev / Release presets
├── conanfile.py              # Conan 2 dependency spec
├── src/
│   ├── main.cpp
│   ├── app/                  # Application, MainWindow, tabs
│   ├── core/                 # Document model, FileManager, UndoManager
│   ├── parser/               # cmark-gfm C++ wrapper (AstNode, AstTree)
│   ├── renderer/             # AST → QTextDocument (block/inline/table)
│   ├── editor/               # MarkdownEditor, InlineToggleManager, InputHandler
│   ├── sync/                 # SyncEngine, PositionMapper
│   ├── theme/                # ThemeManager, CSS loading
│   └── settings/             # QSettings wrapper, shortcut config
├── resources/
│   ├── themes/               # default_light.css, default_dark.css
│   └── resources.qrc
└── tests/                    # Qt Test unit tests
```

---

## Building

### Prerequisites

| Requirement | Version |
|-------------|---------|
| CMake | >= 3.24 |
| C++ Compiler | MSVC 2022, GCC 11+, or Clang 14+ |
| Qt | 6.5+ (Widgets, Gui, Svg) |
| Conan | 2.x (optional) |

### Option A: Build with system Qt

Make sure `Qt6_DIR` is set (e.g. `C:\Qt\6.8.3\msvc2022_64\lib\cmake\Qt6`).

```bash
cmake --preset dev
cmake --build build/dev --config Debug
```

### Option B: Build with Conan (auto-installs Qt)

```bash
conan install . --build=missing -s compiler.cppstd=17 -of build/conan
cmake --preset dev-conan
cmake --build build/dev --config Debug
```

> **Note:** The first Conan build compiles Qt from source (~15–30 min). Subsequent builds use the cache.

### Run

```bash
./build/dev/muffin        # Linux / macOS
./build/dev/Debug/muffin  # Windows (MSVC)
```

### Run Tests

```bash
ctest --test-dir build/dev --output-on-failure
```

---

## Development Roadmap

| Phase | Scope | Status |
|:-----:|-------|:------:|
| 1 | Project skeleton, build system, basic window with open/save | Done |
| 2 | Markdown parsing & read-only rendered preview | Next |
| 3 | WYSIWYG inline toggle (the core innovation) | |
| 4 | Tables, images, math, themes, export | |
| 5 | Polish — shortcuts, settings, i18n, packaging | |

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
