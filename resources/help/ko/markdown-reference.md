# Markdown 참조

Muffin은 **GitHub‑Flavored Markdown (GFM)**을 렌더링합니다. 이 문서는 편집기가 지원하는 기능에 대한 간결한 참조입니다.

## 제목

```
# Heading 1
## Heading 2
### Heading 3
```

최대 6수준까지 지원됩니다.

## 강조

```
**bold**   *italic*   ***both***   ~~strikethrough~~
```

**bold** *italic* ***both*** ~~strikethrough~~

## 목록

순서 없는 목록은 `-`, `*`, `+` 중 하나를 사용합니다.

```
- Apples
- Oranges
  - Tangerines
```

순서 있는 목록은 숫자를 사용합니다.

```
1. First
2. Second
```

작업 목록은 확인란으로 바뀝니다.

```
- [x] Done
- [ ] To do
```

- [x] Done
- [ ] To do

## 링크와 이미지

```
[text](https://example.com)
![alt text](image.png)
```

참조형 링크도 지원됩니다. `[label][id]`로 적고 다른 곳에 `[id]: https://example.com`을 둡니다.

## 코드

인라인 코드는 백틱을 사용합니다. `` `inline` ``

코드 블록(펜스)에는 언어를 지정하면 구문 강조가 적용됩니다.

`````
```cpp
int main() { return 0; }
```
`````

## 인용

```
> A quoted passage.
> Second line.
```

> A quoted passage.
> Second line.

## 표

```
| Name  | Role |
| ----- | ---- |
| Muffin | Editor |
```

| Name | Role |
| --- | --- |
| Muffin | Editor |

## 수식

인라인 수식은 달러 기호 1개, 디스플레이 수식은 달러 기호 2개를 사용합니다.

```
Inline: $a^2 + b^2 = c^2$

$$\int_0^1 x\,dx = \frac{1}{2}$$
```

## 가로선

한 줄에 하이픈을 3개 이상 나열합니다.

```
---
```

---

## 각주

```
Here is a statement.[^1]

[^1]: The note text.
```

## 프런트 매터

파일 맨 위에 있는 YAML(`---`), TOML(`+++`), JSON 블록은 메타데이터로 파싱되며 내용은 그대로 보존됩니다.

---

시작하는 데 도움이 필요하신가요? [빠른 시작](help:quick-start)을 참고하세요.
