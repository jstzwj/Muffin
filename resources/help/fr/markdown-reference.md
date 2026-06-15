# Référence Markdown

Muffin prend en charge **GitHub‑Flavored Markdown (GFM)**. Ceci est une référence concise de ce que l'éditeur prend en charge.

## Titres

```
# Heading 1
## Heading 2
### Heading 3
```

Jusqu'à six niveaux sont pris en charge.

## Mise en valeur

```
**bold**   *italic*   ***both***   ~~strikethrough~~
```

**bold** *italic* ***both*** ~~strikethrough~~

## Listes

Les listes non ordonnées utilisent `-`, `*` ou `+` :

```
- Apples
- Oranges
  - Tangerines
```

Les listes ordonnées utilisent des numéros :

```
1. First
2. Second
```

Les listes de tâches se transforment en cases à cocher :

```
- [x] Done
- [ ] To do
```

- [x] Done
- [ ] To do

## Liens et images

```
[text](https://example.com)
![alt text](image.png)
```

Les liens de type référence sont également pris en charge : `[label][id]` avec `[id]: https://example.com` ailleurs.

## Code

Le code en ligne utilise des accents graves : `` `inline` ``.

Blocs de code délimités — ajoutez un langage pour la coloration syntaxique :

`````
```cpp
int main() { return 0; }
```
`````

## Citations en bloc

```
> A quoted passage.
> Second line.
```

> A quoted passage.
> Second line.

## Tableaux

```
| Name  | Role |
| ----- | ---- |
| Muffin | Editor |
```

| Name | Role |
| --- | --- |
| Muffin | Editor |

## Mathématiques

Mathématiques en ligne avec des dollars simples, mathématiques en bloc avec des dollars doubles :

```
Inline: $a^2 + b^2 = c^2$

$$\int_0^1 x\,dx = \frac{1}{2}$$
```

## Filet horizontal

Trois tirets ou plus sur une ligne :

```
---
```

---

## Notes de bas de page

```
Here is a statement.[^1]

[^1]: The note text.
```

## Front matter

Un bloc YAML (`---`), TOML (`+++`) ou JSON tout au début du fichier est analysé comme métadonnées et laissé intact.

---

Besoin d'un coup de main pour démarrer ? Consultez le [Démarrage rapide](help:quick-start).
