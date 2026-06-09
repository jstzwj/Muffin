# Contributing to Muffin

Thank you for your interest in contributing to Muffin! This document provides guidelines and information for contributors.

## How to Contribute

### Reporting Issues

- Use the [GitHub issue tracker](https://github.com/jstzwj/Muffin/issues)
- Describe the issue clearly with steps to reproduce
- Include your environment (OS, Qt version, compiler, etc.)
- Attach screenshots or screen recordings when relevant

### Submitting Pull Requests

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Make your changes
4. Build and run the test suite (see below)
5. Commit your changes (`git commit -m 'Add amazing feature'`)
6. Push to your branch (`git push origin feature/amazing-feature`)
7. Open a Pull Request

## Development Setup

Muffin uses [Conan](https://conan.io/) for dependency management and CMake for building. You need:

- **C++20** compiler (MSVC 2022+, GCC 12+, or Clang 15+)
- **Qt 6** (installed via Conan)
- **Conan 2.x** package manager
- **CMake 3.24+**

### Quick Setup

```powershell
# Detect your Conan profile
conan profile detect --force

# Install dependencies (Release mode)
conan install . -s build_type=Release -s compiler.cppstd=20 --build=missing

# Configure and build
cmake --preset conan-default
cmake --build --preset conan-release
```

### Run Tests

```powershell
ctest --preset conan-release --output-on-failure
```

### Package and Run

```powershell
cmake --build --preset conan-release --target dist
.\build\dist\Muffin.exe
```

See [CLAUDE.md](CLAUDE.md) for additional build details and common pitfalls.

## Code Style

- Follow the existing code style in the repository
- Use `camelCase` for variables and functions, `PascalCase` for classes
- Add comments for non-obvious logic
- Keep functions focused and concise

### Naming Conventions

- **Namespaces**: `muffin::` for all application code
- **Files**: `PascalCase.h` / `PascalCase.cpp` for class files
- **Members**: Trailing underscore (e.g., `document_`, `renderView_`)
- **Qt signals/slots**: `camelCase`
- **CMake targets**: `muffin::` prefix for internal libraries

### Commit Messages

Use conventional commit format:

```
type(scope): short description

feat(editor): add animated scroll to cursor in typewriter mode
fix(table): fix cell deletion removing syntax markers
refactor(render): restructure inline projection offset mapping
build(cmake): add macOS offscreen rendering environment variable
ci(workflow): add libxcb-util-dev dependency for Linux
```

Common types: `feat`, `fix`, `refactor`, `build`, `ci`, `test`, `docs`, `style`, `i18n`

## Testing

All new features and bug fixes should include tests when applicable.

```powershell
# Run all tests
ctest --preset conan-release --output-on-failure

# Run a specific test
ctest --preset conan-release -R MuffinDocumentTest --output-on-failure
```

Test executables are located in `build/Release/` after building. Tests use Qt Test framework (`QTest`).

### Writing Tests

- Place test files in the `tests/` directory
- Use `QVERIFY`, `QCOMPARE`, and other QTest macros
- Follow the naming convention `Muffin<Component>Test.cpp`
- Register new tests in `tests/CMakeLists.txt`

## Project Architecture

### Directory Structure

```
src/
  app/        MainWindow, preferences, sidebar, language manager
  blocks/     Block model and layout types
  commands/   Command registry for menu actions
  diagnostics/ Debug and profiling utilities
  document/   Markdown document model, parser, outline
  edit/       Text editing operations (insert, delete, replace)
  editor/     EditorView, source editor, find bar, input handling
  export/     File export functionality
  icons/      SVG icon resources
  io/         File I/O and encoding
  math/       Math rendering integration (KaTeX)
  parser/     Markdown parsing (cmark-gfm)
  render/     Layout engine, block/inline rendering, themes
  table/      Table model and cell editing
  theme/      Theme definitions and management
```

### Key Concepts

- **Document Model** (`MarkdownDocument`) — Holds the parsed Markdown tree and provides node access
- **Layout Engine** (`DocumentLayout`, `BlockLayout`, `InlineLayout`) — Computes positions and sizes for rendered content
- **Editor View** (`EditorView`) — The main rendered editing surface using `QAbstractScrollArea` with custom painting
- **Source Editor** (`SourceEditorWidget`) — Source code editing with syntax highlighting
- **Cursor Model** (`HitTestResult`, `CursorPosition`, `SelectionRange`) — Maps between screen coordinates, document nodes, and source offsets
- **Command Registry** (`CommandRegistry`) — Decouples menu actions from their implementations
- **Text Deltas** — Incremental text updates sent to the parser instead of full document replacement
- **Inline Projection** — Bidirectional source offset mapping between rendered view and raw Markdown

### Third-Party Dependencies

| Library | Purpose | License |
|---------|---------|---------|
| Qt 6 | GUI framework | LGPL-3.0 |
| cmark-gfm | Markdown parsing | BSD-2-Clause |
| tree-sitter | Syntax highlighting | MIT |
| KaTeX | Math formula rendering | MIT |

Third-party sources live in `third_party/` and are built as part of the CMake project.

## Adding New Features

### Adding a New Block Type

1. Add parsing support in `src/parser/` if needed
2. Create a layout class in `src/render/` extending `BlockLayout`
3. Add rendering logic in the block's `paint()` method
4. Add editing support in `src/edit/` and the relevant sub-controller
5. Add cursor hit-test handling in `EditorView`
6. Add tests in `tests/`

### Adding a New Theme

1. Add a `RenderTheme` definition in `src/theme/`
2. Register the theme in the theme manager
3. Add a menu action in `MainWindowMenus.cpp`
4. Update the stylesheet for chrome elements (menu bar, status bar, sidebar)
5. Update `updateThemeActions()` in `MainWindow.cpp`

### Adding New Translations

```powershell
# Update .ts files with new translatable strings
cmake --build --preset conan-release --target update_translations

# Edit translations in Qt Linguist or directly in the .ts files
# Release compiled .qm files
cmake --build --preset conan-release --target release_translations
```

If Qt Linguist tools are not on `PATH`, set `MUFFIN_QT_LINGUIST_BIN` to the directory containing `lupdate` and `lrelease`.

## Documentation

- Update `README.md` for user-facing changes
- Update `CHANGELOG.md` with your changes under the appropriate section (`Added`, `Changed`, `Fixed`, `Removed`)
- Add inline comments for complex algorithms or non-obvious design decisions
- Keep CLAUDE.md in sync with any build system or testing changes

## Questions?

Feel free to:

- Open an issue for questions or discussions
- Check existing issues and pull requests for context
- Refer to [CLAUDE.md](CLAUDE.md) for build and test guidance

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
