# Tham chiếu Markdown

Muffin kết xuất **GitHub‑Flavored Markdown (GFM)**. Đây là tài liệu tham khảo ngắn gọn về những gì trình soạn thảo hỗ trợ.

## Tiêu đề

```
# Heading 1
## Heading 2
### Heading 3
```

Hỗ trợ tối đa sáu cấp độ.

## Nhấn mạnh

```
**bold**   *italic*   ***both***   ~~strikethrough~~
```

**bold** *italic* ***both*** ~~strikethrough~~

## Danh sách

Danh sách không thứ tự dùng `-`, `*` hoặc `+`:

```
- Apples
- Oranges
  - Tangerines
```

Danh sách có thứ tự dùng số:

```
1. First
2. Second
```

Danh sách tác vụ chuyển thành hộp kiểm:

```
- [x] Done
- [ ] To do
```

- [x] Done
- [ ] To do

## Liên kết và hình ảnh

```
[text](https://example.com)
![alt text](image.png)
```

Liên kết kiểu tham chiếu cũng được hỗ trợ: `[label][id]` cùng với `[id]: https://example.com` đặt ở nơi khác.

## Mã

Mã nội dòng dùng dấu huyền: `` `inline` ``.

Khối mã có viền — thêm ngôn ngữ để tô sáng cú pháp:

`````
```cpp
int main() { return 0; }
```
`````

## Trích dẫn khối

```
> A quoted passage.
> Second line.
```

> A quoted passage.
> Second line.

## Bảng

```
| Name  | Role |
| ----- | ---- |
| Muffin | Editor |
```

| Name | Role |
| --- | --- |
| Muffin | Editor |

## Toán học

Toán nội dòng dùng một dấu đô la, toán hiển thị dùng hai dấu:

```
Inline: $a^2 + b^2 = c^2$

$$\int_0^1 x\,dx = \frac{1}{2}$$
```

## Đường kẻ ngang

Ba dấu gạch ngang trở lên trên một dòng:

```
---
```

---

## Chú thích

```
Here is a statement.[^1]

[^1]: The note text.
```

## Front matter

Một khối YAML (`---`), TOML (`+++`) hoặc JSON ở ngay đầu tệp được phân tích cú pháp như siêu dữ liệu và để nguyên không thay đổi.

---

Cần trợ giúp để bắt đầu? Xem [Bắt đầu nhanh](help:quick-start).
