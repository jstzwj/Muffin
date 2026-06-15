# Markdown 参考手册

Muffin 渲染 **GitHub 风格 Markdown (GFM)**。以下是编辑器支持语法的简要参考。

## 标题

```
# Heading 1
## Heading 2
### Heading 3
```

最多支持六级标题。

## 强调

```
**bold**   *italic*   ***both***   ~~strikethrough~~
```

**粗体** *斜体* ***两者*** ~~删除线~~

## 列表

无序列表使用 `-`、`*` 或 `+`：

```
- Apples
- Oranges
  - Tangerines
```

有序列表使用数字：

```
1. First
2. Second
```

任务列表会渲染为复选框：

```
- [x] Done
- [ ] To do
```

- [x] 已完成
- [ ] 待办

## 链接与图片

```
[text](https://example.com)
![alt text](image.png)
```

也支持引用式链接：使用 `[label][id]`，并在其他位置定义 `[id]: https://example.com`。

## 代码

行内代码使用反引号：`` `inline` ``。

代码块 —— 添加语言标识即可获得语法高亮：

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

> 这是一段引用。
> 第二行。

## 表格

```
| Name  | Role |
| ----- | ---- |
| Muffin | Editor |
```

| 名称 | 角色 |
| --- | --- |
| Muffin | 编辑器 |

## 数学公式

行内公式使用单个美元符号，块级公式使用双美元符号：

```
Inline: $a^2 + b^2 = c^2$

$$\int_0^1 x\,dx = \frac{1}{2}$$
```

## 分隔线

一行中使用三个或更多连字符：

```
---
```

---

## 脚注

```
Here is a statement.[^1]

[^1]: The note text.
```

## 元数据（Front Matter）

文件开头的 YAML（`---`）、TOML（`+++`）或 JSON 块会被解析为元数据，并原样保留。

---

需要上手帮助？请查阅 [快速开始](help:quick-start)。
