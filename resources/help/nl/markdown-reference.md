# Markdown-verwijzing

Muffin geeft **GitHub‑Flavored Markdown (GFM)** weer. Dit is een beknopte verwijzing van wat de editor ondersteunt.

## Kopteksten

```
# Heading 1
## Heading 2
### Heading 3
```

Er worden maximaal zes niveaus ondersteund.

## Nadruk

```
**bold**   *italic*   ***both***   ~~strikethrough~~
```

**bold** *italic* ***both*** ~~strikethrough~~

## Lijsten

Ongeordende lijsten gebruiken `-`, `*` of `+`:

```
- Apples
- Oranges
  - Tangerines
```

Geordende lijsten gebruiken getallen:

```
1. First
2. Second
```

Takenlijsten worden selectievakjes:

```
- [x] Done
- [ ] To do
```

- [x] Done
- [ ] To do

## Koppelingen en afbeeldingen

```
[text](https://example.com)
![alt text](image.png)
```

Verwijzingsstijl-koppelingen worden ook ondersteund: `[label][id]` met ergens anders `[id]: https://example.com`.

## Code

Inline code gebruikt backticks: `` `inline` ``.

Afgesloten codeblokken — voeg een taal toe voor syntaxisaccentuering:

`````
```cpp
int main() { return 0; }
```
`````

## Blokcitaten

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

## Wiskunde

Inline wiskunde met enkele dollartekens, weergavewiskunde met dubbele:

```
Inline: $a^2 + b^2 = c^2$

$$\int_0^1 x\,dx = \frac{1}{2}$$
```

## Horizontale lijn

Drie of meer streepjes op één regel:

```
---
```

---

## Voetnoten

```
Here is a statement.[^1]

[^1]: The note text.
```

## Front matter

Een YAML- (`---`), TOML- (`+++`) of JSON-blok helemaal bovenaan het bestand wordt als metagegevens geparseerd en onaangeroerd gelaten.

---

Hulp nodig om te beginnen? Bekijk de [Snel aan de slag](help:quick-start).
