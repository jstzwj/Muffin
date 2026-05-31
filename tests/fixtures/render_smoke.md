# Render Smoke

This fixture covers **strong text**, *emphasis*, `inline code`, [a link](https://example.com), and inline math $a^2 + b^2 = c^2$ in one paragraph so the inline layout has to wrap and expose text hit testing.

## Lists

- First bullet
- [x] Completed task
- [ ] Pending task
  - Nested bullet with ~~strike~~

> A quoted paragraph with enough text to wrap across more than one visual line.
>
> - Quoted list item

```cpp
int main() {
  return 0;
}
```

$$
E = mc^2
$$

| Feature | Status | Count |
| :--- | :---: | ---: |
| Tables | ready | 3 |
| Math $x$ | parsed | 42 |

<div>Raw HTML block stays visible as source for now.</div>

---

Final paragraph after the rule.
