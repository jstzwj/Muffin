# Referência de Markdown

O Muffin renderiza o **GitHub‑Flavored Markdown (GFM)**. Esta é uma referência concisa do que o editor oferece.

## Títulos

```
# Heading 1
## Heading 2
### Heading 3
```

Há suporte a até seis níveis.

## Ênfase

```
**bold**   *italic*   ***both***   ~~strikethrough~~
```

**bold** *italic* ***both*** ~~strikethrough~~

## Listas

Listas não ordenadas usam `-`, `*` ou `+`:

```
- Apples
- Oranges
  - Tangerines
```

Listas ordenadas usam números:

```
1. First
2. Second
```

Listas de tarefas viram caixas de seleção:

```
- [x] Done
- [ ] To do
```

- [x] Done
- [ ] To do

## Links e imagens

```
[text](https://example.com)
![alt text](image.png)
```

Links no estilo de referência também são suportados: `[label][id]` com `[id]: https://example.com` em outro ponto do texto.

## Código

Código embutido usa crases: `` `inline` ``.

Blocos de código cercados — adicione uma linguagem para realce de sintaxe:

`````
```cpp
int main() { return 0; }
```
`````

## Citações em bloco

```
> A quoted passage.
> Second line.
```

> A quoted passage.
> Second line.

## Tabelas

```
| Name  | Role |
| ----- | ---- |
| Muffin | Editor |
```

| Name | Role |
| --- | --- |
| Muffin | Editor |

## Matemática

Matemática embutida com cifrões simples, matemática em bloco com cifrões duplos:

```
Inline: $a^2 + b^2 = c^2$

$$\int_0^1 x\,dx = \frac{1}{2}$$
```

## Régua horizontal

Três ou mais hifens em uma linha:

```
---
```

---

## Notas de rodapé

```
Here is a statement.[^1]

[^1]: The note text.
```

## Front matter

Um bloco YAML (`---`), TOML (`+++`) ou JSON bem no início do arquivo é analisado como metadados e deixado intacto.

---

Precisa de uma mão para começar? Veja o [Início rápido](help:quick-start).
