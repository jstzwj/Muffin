# Muffin Markdown Example

中文 | English

This document is a Markdown syntax sample for exercising Muffin's parser, renderer, and editing paths.

## Inline Features

Plain text can mix **bold**, *italic*, ~~strikethrough~~, `inline code`, inline HTML <kbd>Ctrl</kbd> + <kbd>S</kbd>, links such as [Muffin](https://example.com), autolinks like https://example.com, and inline math $E = mc^2$.

Escaped punctuation stays literal: \*not emphasized\*, \`not code\`, \[not a link\].

Entities are decoded by renderers: &amp; &lt; &gt; &copy;.

## Headings

# Heading 1 sample
## Heading 2 sample
### Heading 3 sample
#### Heading 4 sample
##### Heading 5 sample
###### Heading 6 sample

## Paragraphs and Line Breaks

Markdown paragraphs are separated by blank lines. Soft line breaks normally stay inside the same paragraph.
This line follows a soft break.

Two trailing spaces create a hard line break.  
This line starts after a hard break.

## Block Quotes

> A block quote can contain paragraphs.
>
> It can also contain **formatting**, `code`, and nested quotes.
>
> > Nested quote.

## Lists

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
- [x] Render Markdown from the document AST
- [x] Edit tables, code fences, math blocks, and HTML blocks
- [ ] Complete export workflows

## Links and Images

Inline link: [GitHub cmark-gfm](https://github.com/github/cmark-gfm)

Reference link: [Qt][qt-link]

Autolink: https://www.qt.io

Email autolink: <hello@example.com>

Image syntax:

![Muffin placeholder](docs/muffin-placeholder.png "Muffin")

[qt-link]: https://www.qt.io

## Code

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
.\build\dist\Muffin.exe example.md
```

## Table

| Feature | Status | Notes |
| :--- | :---: | ---: |
| Paragraphs | Implemented | CommonMark |
| Tables | Editable | GFM table model operations |
| Code fences | Editable | Language and literal updates |
| HTML blocks | Editable | Sanitized preview support |
| Math | In progress | Parser, block edits, native renderer work |
| Exports | Planned | HTML/PDF/Pandoc workflows |

## Thematic Breaks

---

***

___

## Math

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

## HTML

<details>
<summary>HTML block sample</summary>

Muffin preserves raw HTML as Markdown source and routes preview-oriented behavior through an HTML sanitizer.

</details>

## HTML Sanitizer Fixture

The raw HTML below is intentionally unsafe. Muffin should preserve it as source text, but a sanitized preview must remove scripts, event handlers, and `javascript:` URLs.

<div onclick="alert('unsafe')">
  <a href="javascript:alert('xss')">unsafe link</a>
  <script>alert('blocked');</script>
</div>

## Unsupported or Planned Markdown-Adjacent Features

The following syntax is useful for compatibility tests, but full editing/rendering support is not complete yet:

- Footnotes: `A note reference[^sample-note]`
- Definition lists
- Mermaid diagrams
- Front matter editor
- Embedded rich media

[^sample-note]: Footnote syntax is included as a compatibility sample.
