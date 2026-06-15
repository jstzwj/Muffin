# Referencia de Markdown

Muffin renderiza **GitHub‑Flavored Markdown (GFM)**. Esta es una referencia concisa de lo que admite el editor.

## Encabezados

```
# Heading 1
## Heading 2
### Heading 3
```

Se admiten hasta seis niveles.

## Énfasis

```
**bold**   *italic*   ***both***   ~~strikethrough~~
```

**bold** *italic* ***both*** ~~strikethrough~~

## Listas

Las listas no ordenadas usan `-`, `*` o `+`:

```
- Apples
- Oranges
  - Tangerines
```

Las listas ordenadas usan números:

```
1. First
2. Second
```

Las listas de tareas se convierten en casillas de verificación:

```
- [x] Done
- [ ] To do
```

- [x] Done
- [ ] To do

## Enlaces e imágenes

```
[text](https://example.com)
![alt text](image.png)
```

También se admiten los enlaces de tipo referencia: `[label][id]` con `[id]: https://example.com` en otro lugar.

## Código

El código en línea usa acentos graves: `` `inline` ``.

Bloques de código delimitados — añade un lenguaje para el resaltado de sintaxis:

`````
```cpp
int main() { return 0; }
```
`````

## Citas en bloque

```
> A quoted passage.
> Second line.
```

> A quoted passage.
> Second line.

## Tablas

```
| Name  | Role |
| ----- | ---- |
| Muffin | Editor |
```

| Name | Role |
| --- | --- |
| Muffin | Editor |

## Matemáticas

Matemáticas en línea con un solo dólar, matemáticas en bloque con doble dólar:

```
Inline: $a^2 + b^2 = c^2$

$$\int_0^1 x\,dx = \frac{1}{2}$$
```

## Regla horizontal

Tres o más guiones en una línea:

```
---
```

---

## Notas al pie

```
Here is a statement.[^1]

[^1]: The note text.
```

## Front matter

Un bloque YAML (`---`), TOML (`+++`) o JSON al principio del archivo se analiza como metadatos y se deja intacto.

---

¿Necesitas ayuda para empezar? Consulta el [Inicio rápido](help:quick-start).
