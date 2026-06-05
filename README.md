# Muffin

[中文](README.zh.md)

Muffin is a fast, lightweight Markdown editor focused on a clean writing surface and responsive editing.

The editor keeps Markdown as the source of truth while presenting the document as an editable page. It is designed for people who want the readability of a rendered document without giving up plain Markdown files.

## Highlights

- Clean single-pane editing experience.
- Source mode and rendered editing mode.
- Fast local Markdown parsing and document refresh.
- File workflows for new, open, save, save as, recent files, document properties, and folder browsing.
- Editable tables, code fences, math blocks, and HTML blocks.
- Document outline and file tree sidebar.
- Theme support and English/Simplified Chinese UI resources.

For a Markdown syntax stress sample, see [example.md](example.md).

## Build and Test

This repository uses Conan-generated CMake presets. The local verification flow is:

```powershell
conan profile detect --force
conan install . -s build_type=Release -s compiler.cppstd=17 --build=missing
cmake --preset conan-default
cmake --build --preset conan-release
ctest --preset conan-release --output-on-failure
cmake --build --preset conan-release --target dist
```

The packaged application is written to:

```text
build/dist/Muffin.exe
```

`cmake --build --preset conan-release` updates `build/Release/Muffin.exe`; run the `dist` target when you need to refresh the distributable bundle.

## Translation Workflow

Muffin currently includes English and Simplified Chinese UI resources.

```powershell
cmake --build --preset conan-release --target update_translations
cmake --build --preset conan-release --target release_translations
```

If Qt Linguist tools are not on `PATH`, configure `MUFFIN_QT_LINGUIST_BIN` to the directory containing `lupdate` and `lrelease`.

## Roadmap

1. Polish the rendered editor surface and close gaps in selection, cursor, IME, and local refresh behavior.
2. Expand block transforms for headings, paragraphs, lists, quotes, front matter, and task list commands.
3. Finish complex block UX for tables, code fences, math blocks, HTML blocks, and images.
4. Add search/replace, richer clipboard behavior, export flows, printing, and diagnostics.
5. Harden performance and roundtrip tests for large Markdown documents.
