# Markdown 參考手冊

Muffin 渲染 **GitHub 風格 Markdown (GFM)**。以下是編輯器支援語法的簡要參考。

## 標題

```
# Heading 1
## Heading 2
### Heading 3
```

最多支援六級標題。

## 強調

```
**bold**   *italic*   ***both***   ~~strikethrough~~
```

**粗體** *斜體* ***兩者*** ~~刪除線~~

## 清單

無序清單使用 `-`、`*` 或 `+`：

```
- Apples
- Oranges
  - Tangerines
```

有序清單使用數字：

```
1. First
2. Second
```

工作清單會渲染為核取方塊：

```
- [x] Done
- [ ] To do
```

- [x] 已完成
- [ ] 待辦

## 連結與圖片

```
[text](https://example.com)
![alt text](image.png)
```

也支援參照式連結：使用 `[label][id]`，並在其他位置定義 `[id]: https://example.com`。

## 程式碼

行內程式碼使用反引號：`` `inline` ``。

程式碼區塊 —— 加上語言標記即可獲得語法高亮：

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

> 這是一段引用。
> 第二行。

## 表格

```
| Name  | Role |
| ----- | ---- |
| Muffin | Editor |
```

| 名稱 | 角色 |
| --- | --- |
| Muffin | 編輯器 |

## 數學公式

行內公式使用單一錢號，區塊公式使用雙錢號：

```
Inline: $a^2 + b^2 = c^2$

$$\int_0^1 x\,dx = \frac{1}{2}$$
```

## 分隔線

一列中使用三個或多個連字號：

```
---
```

---

## 註腳

```
Here is a statement.[^1]

[^1]: The note text.
```

## Front Matter

檔案最開頭的 YAML（`---`）、TOML（`+++`）或 JSON 區塊會被解析為中繼資料，並原樣保留。

---

需要上手協助嗎？請查閱 [快速開始](help:quick-start)。
