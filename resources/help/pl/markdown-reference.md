# Dokumentacja Markdown

Muffin renderuje **GitHub‑Flavored Markdown (GFM)**. To zwięzła dokumentacja tego, co obsługuje edytor.

## Nagłówki

```
# Heading 1
## Heading 2
### Heading 3
```

Obsługiwane jest do sześciu poziomów.

## Wyróżnienia

```
**bold**   *italic*   ***both***   ~~strikethrough~~
```

**bold** *italic* ***both*** ~~strikethrough~~

## Listy

Listy nieuporządkowane używają `-`, `*` lub `+`:

```
- Apples
- Oranges
  - Tangerines
```

Listy uporządkowane używają liczb:

```
1. First
2. Second
```

Listy zadań zamieniają się w pola wyboru:

```
- [x] Done
- [ ] To do
```

- [x] Done
- [ ] To do

## Odnośniki i obrazy

```
[text](https://example.com)
![alt text](image.png)
```

Obsługiwane są też odnośniki referencyjne: `[label][id]` wraz z `[id]: https://example.com` gdzieś indziej w dokumencie.

## Kod

Wbudowany kod używa odwróconych apostrofów: `` `inline` ``.

Ogrodzone bloki kodu — dodaj język, aby uzyskać podświetlanie składni:

`````
```cpp
int main() { return 0; }
```
`````

## Cytaty blokowe

```
> A quoted passage.
> Second line.
```

> A quoted passage.
> Second line.

## Tabele

```
| Name  | Role |
| ----- | ---- |
| Muffin | Editor |
```

| Name | Role |
| --- | --- |
| Muffin | Editor |

## Matematyka

Wbudowana matematyka z pojedynczymi znakami dolara, matematyka wyświetlana z podwójnymi:

```
Inline: $a^2 + b^2 = c^2$

$$\int_0^1 x\,dx = \frac{1}{2}$$
```

## Linia pozioma

Trzy lub więcej myślników w jednej linii:

```
---
```

---

## Przypisy

```
Here is a statement.[^1]

[^1]: The note text.
```

## Front matter

Blok YAML (`---`), TOML (`+++`) lub JSON na samym początku pliku jest analizowany jako metadane i pozostawiony bez zmian.

---

Potrzebujesz pomocy na start? Zobacz [Szybki start](help:quick-start).
