# Markdown başvurusu

Muffin, **GitHub‑Flavored Markdown (GFM)** oluşturur. Bu, düzenleyicinin destekledikleri için kısa bir başvurudur.

## Başlıklar

```
# Heading 1
## Heading 2
### Heading 3
```

Altı düzeye kadar desteklenir.

## Vurgu

```
**bold**   *italic*   ***both***   ~~strikethrough~~
```

**bold** *italic* ***both*** ~~strikethrough~~

## Listeler

Sırasız listeler `-`, `*` veya `+` kullanır:

```
- Apples
- Oranges
  - Tangerines
```

Sıralı listeler sayı kullanır:

```
1. First
2. Second
```

Görev listeleri onay kutularına dönüşür:

```
- [x] Done
- [ ] To do
```

- [x] Done
- [ ] To do

## Bağlantılar ve görüntüler

```
[text](https://example.com)
![alt text](image.png)
```

Başvuru tarzı bağlantılar da desteklenir: Başka bir yerde `[id]: https://example.com` ile birlikte `[label][id]`.

## Kod

Satır içi kod, ters eğik tırnak kullanır: `` `inline` ``.

Çitlenmiş kod blokları — sözdizimi vurgulaması için bir dil ekleyin:

`````
```cpp
int main() { return 0; }
```
`````

## Alıntı blokları

```
> A quoted passage.
> Second line.
```

> A quoted passage.
> Second line.

## Tablolar

```
| Name  | Role |
| ----- | ---- |
| Muffin | Editor |
```

| Name | Role |
| --- | --- |
| Muffin | Editor |

## Matematik

Satır içi matematik tek dolar işaretiyle, görüntü matematik ise çift dolar işaretiyle:

```
Inline: $a^2 + b^2 = c^2$

$$\int_0^1 x\,dx = \frac{1}{2}$$
```

## Yatay ayraç

Tek bir satırda üç veya daha fazla tire:

```
---
```

---

## Dipnotlar

```
Here is a statement.[^1]

[^1]: The note text.
```

## Ön madde

Dosyanın en başındaki bir YAML (`---`), TOML (`+++`) veya JSON bloğu, üst veri olarak ayrıştırılır ve dokunulmadan bırakılır.

---

Başlamak için yardıma mı ihtiyacınız var? [Hızlı Başlangıç](help:quick-start)'a bakın.
