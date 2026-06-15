# Markdown リファレンス

Muffin は **GitHub‑Flavored Markdown (GFM)** をレンダリングします。これは、エディターが対応している機能の簡潔なリファレンスです。

## 見出し

```
# Heading 1
## Heading 2
### Heading 3
```

最大 6 レベルまで対応しています。

## 強調

```
**bold**   *italic*   ***both***   ~~strikethrough~~
```

**bold** *italic* ***both*** ~~strikethrough~~

## リスト

箇条書きリストは `-`、`*`、`+` のいずれかを使います。

```
- Apples
- Oranges
  - Tangerines
```

番号付きリストは数字を使います。

```
1. First
2. Second
```

タスクリストはチェックボックスになります。

```
- [x] Done
- [ ] To do
```

- [x] Done
- [ ] To do

## リンクと画像

```
[text](https://example.com)
![alt text](image.png)
```

参照式リンクにも対応しています。`[label][id]` と記述し、別の場所に `[id]: https://example.com` を置きます。

## コード

インラインコードはバッククォートを使います。`` `inline` ``

コードブロック（フェンス）では、言語を指定するとシンタックスハイライトが効きます。

`````
```cpp
int main() { return 0; }
```
`````

## 引用

```
> A quoted passage.
> Second line.
```

> A quoted passage.
> Second line.

## 表

```
| Name  | Role |
| ----- | ---- |
| Muffin | Editor |
```

| Name | Role |
| --- | --- |
| Muffin | Editor |

## 数式

インライン数式には `$` 1 つ、ディスプレイ数式には `$` 2 つを使います。

```
Inline: $a^2 + b^2 = c^2$

$$\int_0^1 x\,dx = \frac{1}{2}$$
```

## 水平線

1 行にハイフンを 3 つ以上並べます。

```
---
```

---

## 脚注

```
Here is a statement.[^1]

[^1]: The note text.
```

## フロントマター

ファイルの先頭にある YAML（`---`）、TOML（`+++`）、JSON ブロックはメタデータとして解析され、内容はそのまま残されます。

---

使い始める前に困ったことはありませんか？[クイックスタート](help:quick-start) をご覧ください。
