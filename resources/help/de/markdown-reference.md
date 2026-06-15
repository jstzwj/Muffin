# Markdown-Referenz

Muffin rendert **GitHub-Flavored Markdown (GFM)**. Dies ist eine kompakte Referenz der vom Editor unterstützten Funktionen.

## Überschriften

```
# Heading 1
## Heading 2
### Heading 3
```

Bis zu sechs Ebenen werden unterstützt.

## Hervorhebung

```
**bold**   *italic*   ***both***   ~~strikethrough~~
```

**bold** *italic* ***both*** ~~strikethrough~~

## Listen

Ungeordnete Listen verwenden `-`, `*` oder `+`:

```
- Apples
- Oranges
  - Tangerines
```

Geordnete Listen verwenden Zahlen:

```
1. First
2. Second
```

Aufgabenlisten werden zu Kontrollkästchen:

```
- [x] Done
- [ ] To do
```

- [x] Done
- [ ] To do

## Links und Bilder

```
[text](https://example.com)
![alt text](image.png)
```

Auch Referenz-Links werden unterstützt: `[label][id]` mit `[id]: https://example.com` an anderer Stelle.

## Code

Inline-Code verwendet Backticks: `` `inline` ``.

Codeblöcke — fügen Sie eine Sprache für Syntaxhervorhebung hinzu:

`````
```cpp
int main() { return 0; }
```
`````

## Zitate

```
> A quoted passage.
> Second line.
```

> A quoted passage.
> Second line.

## Tabellen

```
| Name  | Role |
| ----- | ---- |
| Muffin | Editor |
```

| Name | Role |
| --- | --- |
| Muffin | Editor |

## Mathe

Inline-Mathe mit einfachen Dollarzeichen, Block-Mathe mit doppelten:

```
Inline: $a^2 + b^2 = c^2$

$$\int_0^1 x\,dx = \frac{1}{2}$$
```

## Trennlinie

Drei oder mehr Bindestriche in einer Zeile:

```
---
```

---

## Fußnoten

```
Here is a statement.[^1]

[^1]: The note text.
```

## Front Matter

Ein YAML- (`---`), TOML- (`+++`) oder JSON-Block ganz am Anfang der Datei wird als Metadaten geparst und unangetastet gelassen.

---

Schwierigkeiten beim Einstieg? Siehe [Schnellstart](help:quick-start).
