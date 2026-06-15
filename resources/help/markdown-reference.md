# Markdown Reference

Muffin renders **GitHub‑Flavored Markdown (GFM)**. This is a concise reference for what the editor supports.

## Headings

```
# Heading 1
## Heading 2
### Heading 3
```

Up to six levels are supported.

## Emphasis

```
**bold**   *italic*   ***both***   ~~strikethrough~~
```

**bold** *italic* ***both*** ~~strikethrough~~

## Lists

Unordered lists use `-`, `*` or `+`:

```
- Apples
- Oranges
  - Tangerines
```

Ordered lists use numbers:

```
1. First
2. Second
```

Task lists turn into check boxes:

```
- [x] Done
- [ ] To do
```

- [x] Done
- [ ] To do

## Links and images

```
[text](https://example.com)
![alt text](image.png)
```

Reference‑style links are supported too: `[label][id]` with `[id]: https://example.com` elsewhere.

## Code

Inline code uses backticks: `` `inline` ``.

Fenced code blocks — add a language for syntax highlighting:

`````
```cpp
int main() { return 0; }
```
`````

## Blockquotes

```
> A quoted passage.
> Second line.
```

> A quoted passage.
> Second line.

## Tables

```
| Name  | Role |
| ----- | ---- |
| Muffin | Editor |
```

| Name | Role |
| --- | --- |
| Muffin | Editor |

## Math

Inline math with single dollars, display math with double dollars:

```
Inline: $a^2 + b^2 = c^2$

$$\int_0^1 x\,dx = \frac{1}{2}$$
```

## Horizontal rule

Three or more hyphens on a line:

```
---
```

---

## Footnotes

```
Here is a statement.[^1]

[^1]: The note text.
```

## Front matter

A YAML (`---`), TOML (`+++`) or JSON block at the very top of the file is parsed as metadata and left untouched.

---

Need a hand getting going? See the [Quick Start](help:quick-start).
