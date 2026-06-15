# Guida di riferimento Markdown

Muffin renderizza il **GitHub-Flavored Markdown (GFM)**. Questa è una guida sintetica a ciò che l'editor supporta.

## Titoli

```
# Heading 1
## Heading 2
### Heading 3
```

Sono supportati fino a sei livelli.

## Enfasi

```
**bold**   *italic*   ***both***   ~~strikethrough~~
```

**bold** *italic* ***both*** ~~strikethrough~~

## Elenchi

Gli elenchi non ordinati usano `-`, `*` o `+`:

```
- Apples
- Oranges
  - Tangerines
```

Gli elenchi ordinati usano i numeri:

```
1. First
2. Second
```

Gli elenchi di attività diventano caselle di controllo:

```
- [x] Done
- [ ] To do
```

- [x] Done
- [ ] To do

## Collegamenti e immagini

```
[text](https://example.com)
![alt text](image.png)
```

Sono supportati anche i collegamenti in stile reference: `[label][id]` con `[id]: https://example.com` altrove.

## Codice

Il codice in linea usa i backtick: `` `inline` ``.

Blocchi di codice — aggiungi una lingua per l'evidenziazione della sintassi:

`````
```cpp
int main() { return 0; }
```
`````

## Citazioni

```
> A quoted passage.
> Second line.
```

> A quoted passage.
> Second line.

## Tabelle

```
| Name  | Role |
| ----- | ---- |
| Muffin | Editor |
```

| Name | Role |
| --- | --- |
| Muffin | Editor |

## Formule

Formule in linea con il singolo dollaro, formule in blocco con il doppio dollaro:

```
Inline: $a^2 + b^2 = c^2$

$$\int_0^1 x\,dx = \frac{1}{2}$$
```

## Linea orizzontale

Tre o più trattini su una riga:

```
---
```

---

## Note a piè di pagina

```
Here is a statement.[^1]

[^1]: The note text.
```

## Front matter

Un blocco YAML (`---`), TOML (`+++`) o JSON proprio all'inizio del file viene analizzato come metadati e lasciato invariato.

---

Serve una mano per iniziare? Vedi l'[Avvio rapido](help:quick-start).
