# Muffin

[中文](README.zh.md)

Muffin is a fast, lightweight native Markdown editor built with C++20 and Qt 6 Widgets.

The project is exploring a Typora-like single-pane editing model. Markdown is parsed into a document AST, the UI renders an editable projection of that AST, and saving/exporting should eventually serialize the document model back to Markdown. Muffin is not intended to become a left/right split previewer.

## Status

Muffin is in early development.

- M0/M2 application shell is available: native Qt menu bar, source mode, file open/save/save-as, recent files, document properties, status bar, command-line file opening, and distribution packaging.
- M3 has started: a read-only AST render view is available and source mode can be toggled from `View -> Source Mode`.
- `third_party/cmark-gfm` is vendored and trimmed for embedded use.
- A Typora-style math extension has been added on top of cmark-gfm, including inline math and block math AST nodes.
- True WYSIWYG editing, block selection, AST transaction undo/redo, table editing widgets, image handling, exports, and native math rendering are still planned.

## Technology

| Area | Choice |
| --- | --- |
| UI | Qt 6 Widgets |
| Language | C++20 |
| Dependency manager | Conan |
| Markdown parser | Vendored `cmark-gfm` |
| Markdown extensions | GFM tables, strikethrough, autolinks, task lists, tag filtering, and Muffin math |
| Packaging | CMake `dist` target with Qt DLL/plugin copying |

Qt Widgets was selected over QML because Muffin needs native menus, complex text input, desktop dialogs, printing, and predictable high-performance document editing.

## Build

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

The current Conan configuration uses dynamic Qt:

```ini
qt/*:shared=True
qt/*:widgets=True
qt/*:gui=True
qt/*:qtdeclarative=False
qt/*:with_odbc=False
qt/*:with_pq=False
```

## Repository Layout

```text
src/app/        Main window, document session, command registry
src/document/   Markdown document model and AST node types
src/editor/     Temporary source editor based on QPlainTextEdit
src/io/         File open/save controllers
src/parser/     cmark-gfm adapter and Markdown serializer
src/render/     Read-only AST render view
tests/parser/   Parser smoke tests
docs/           Design notes and menu reference
third_party/    Trimmed embedded cmark-gfm source
```

## Markdown Feature Gallery

This section intentionally contains broad Markdown syntax coverage so Muffin can be opened against its own README as a rendering and parsing sample.

### Headings

# Heading 1 sample
## Heading 2 sample
### Heading 3 sample
#### Heading 4 sample
##### Heading 5 sample
###### Heading 6 sample

### Paragraphs and Line Breaks

Markdown paragraphs are separated by blank lines. Soft line breaks normally stay inside the same paragraph.
This line follows a soft break.

Two trailing spaces create a hard line break.  
This line starts after a hard break.

### Emphasis

Plain text can contain *emphasis*, _alternative emphasis_, **strong text**, __alternative strong text__, ***strong emphasis***, ~~strikethrough~~, `inline code`, and inline HTML such as <kbd>Ctrl</kbd> + <kbd>S</kbd>.

### Escapes and Entities

Escaped punctuation stays literal: \*not emphasized\*, \`not code\`, \[not a link\].

Entities are decoded by renderers: &amp; &lt; &gt; &copy;.

### Block Quotes

> A block quote can contain paragraphs.
>
> It can also contain **formatting**, `code`, and nested quotes.
>
> > Nested quote.

### Lists

Unordered list:

- First item
- Second item with **bold** text
- Third item
  - Nested item
  - Another nested item

Ordered list:

1. First step
2. Second step
3. Third step
   1. Nested ordered step
   2. Another nested ordered step

Task list:

- [x] Parse CommonMark blocks
- [x] Parse GFM tables and task markers
- [x] Parse inline and block math
- [ ] Implement editable AST projection
- [ ] Implement export workflows

### Links and Images

Inline link: [GitHub cmark-gfm](https://github.com/github/cmark-gfm)

Reference link: [Qt][qt-link]

Autolink: https://www.qt.io

Email autolink: <hello@example.com>

Image syntax:

![Muffin placeholder](docs/muffin-placeholder.png "Muffin")

[qt-link]: https://www.qt.io

### Code

Inline code: `cmark_node_get_type(node)`.

Indented code:

    conan install . -s build_type=Release -s compiler.cppstd=17
    cmake --build --preset conan-release

Fenced code:

```cpp
#include <QApplication>

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  return app.exec();
}
```

```powershell
cmake --build --preset conan-release --target dist
.\build\dist\Muffin.exe README.md
```

### Tables

| Feature | Status | Notes |
| :--- | :---: | ---: |
| Paragraphs | Done | CommonMark |
| Tables | Done | GFM |
| Math | In progress | Muffin extension |
| WYSIWYG editing | Planned | AST projection |

### Thematic Breaks

---

***

___

### Math

Inline math uses single dollar delimiters: $E = mc^2$ and $a_1 + b_1 = c_1$.

Block math uses double dollar fences:

$$
\int_0^1 x^2\,dx = \frac{1}{3}
$$

Another block:

$$
\begin{aligned}
a^2 + b^2 &= c^2 \\
e^{i\pi} + 1 &= 0
\end{aligned}
$$

### HTML

<details>
<summary>HTML block sample</summary>

Markdown renderers may allow raw HTML blocks. Muffin currently treats this as parsed Markdown input and will refine HTML behavior during renderer work.

</details>

### Unsupported or Planned Markdown-Adjacent Features

The following syntax is useful for compatibility tests, but full editing/rendering support is not complete yet:

- Footnotes: `A note reference[^sample-note]`
- Definition lists
- Mermaid diagrams
- Front matter editors
- Embedded rich media

[^sample-note]: Footnote syntax is included as a compatibility sample.

## Roadmap

1. Improve read-only AST rendering quality.
2. Add block hit testing and source range mapping.
3. Switch selected rendered blocks into local editable source widgets.
4. Add AST transactions, undo/redo grouping, and serializer-preserving edits.
5. Implement table, math, image, export, and theme workflows.

