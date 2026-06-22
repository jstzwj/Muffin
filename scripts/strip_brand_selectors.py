#!/usr/bin/env python3
"""Remove trademark-bearing selectors/comments from theme CSS.

Whitespace- and structure-preserving: only selectors/comments containing the
target substring are touched. When a selector is comma-grouped with legitimate
ones, only the offending selector is dropped. A rule with no survivors, and an
@media that empties out, are removed entirely. Idempotent.

Usage: strip_brand_selectors.py <substring> <file.css> [file.css ...]
"""
import sys


def find_top_level(s, frm, delim):
    i, n = frm, len(s)
    paren = brk = 0
    while i < n:
        c = s[i]
        if c in '"\'':
            i += 1
            while i < n and s[i] != c:
                i += 1 if s[i] != '\\' else 2
            i += 1
            continue
        if c == '/' and i + 1 < n and s[i + 1] == '*':
            end = s.find('*/', i + 2)
            i = n if end < 0 else end + 2
            continue
        if paren == 0 and brk == 0 and c == delim:
            return i
        if c == '(':
            paren += 1
        elif c == ')':
            paren = max(0, paren - 1)
        elif c == '[':
            brk += 1
        elif c == ']':
            brk = max(0, brk - 1)
        i += 1
    return -1


def match_brace(s, open_idx):
    i, n = open_idx + 1, len(s)
    depth = 1
    while i < n:
        c = s[i]
        if c in '"\'':
            i += 1
            while i < n and s[i] != c:
                i += 1 if s[i] != '\\' else 2
            i += 1
            continue
        if c == '/' and i + 1 < n and s[i + 1] == '*':
            end = s.find('*/', i + 2)
            i = n if end < 0 else end + 2
            continue
        if c == '{':
            depth += 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return n


def split_commas(s):
    out, start, i, n = [], 0, 0, len(s)
    paren = brk = 0
    while i < n:
        c = s[i]
        if c in '"\'':
            i += 1
            while i < n and s[i] != c:
                i += 1 if s[i] != '\\' else 2
            i += 1
            continue
        if c == '/' and i + 1 < n and s[i + 1] == '*':
            end = s.find('*/', i + 2)
            i = n if end < 0 else end + 2
            continue
        if c == '(':
            paren += 1
        elif c == ')':
            paren = max(0, paren - 1)
        elif c == '[':
            brk += 1
        elif c == ']':
            brk = max(0, brk - 1)
        elif c == ',' and paren == 0 and brk == 0:
            out.append(s[start:i])
            start = i + 1
        i += 1
    out.append(s[start:])
    return out


def has_rule(content):
    i, n = 0, len(content)
    while i < n:
        c = content[i]
        if c == '/' and i + 1 < n and content[i + 1] == '*':
            end = content.find('*/', i + 2)
            i = n if end < 0 else end + 2
            continue
        if c == '{':
            return True
        i += 1
    return False


def strip(css, needle):
    needle = needle.lower()
    out, i, n = [], 0, len(css)
    while i < n:
        c = css[i]
        if c in ' \t\r\n':
            j = i
            while j < n and css[j] in ' \t\r\n':
                j += 1
            out.append(css[i:j])
            i = j
            continue
        if c == '/' and i + 1 < n and css[i + 1] == '*':
            end = css.find('*/', i + 2)
            end = n if end < 0 else end + 2
            if needle not in css[i:end].lower():
                out.append(css[i:end])
            i = end
            continue
        if c == '@':
            m = 0
            while i + m < n and (css[i + m].isalnum() or css[i + m] in '-_'):
                m += 1
            kw = css[i:i + m]
            semi = find_top_level(css, i + m, ';')
            brace = find_top_level(css, i + m, '{')
            if brace < 0 or (0 <= semi < brace):
                end = n if semi < 0 else semi + 1
                out.append(css[i:end])
                i = end
                continue
            body_end = match_brace(css, brace)
            if kw == '@media':
                # @media holds nested rules: filter them, drop the block if empty.
                body = strip(css[brace + 1:body_end], needle)
                if has_rule(body):
                    out.append(css[i:brace] + '{' + body + '}')
            else:
                # @font-face / @keyframes / @page … : body is declarations or
                # keyframe offsets, not theme selectors — keep verbatim.
                out.append(css[i:body_end + 1])
            i = body_end + 1
            continue
        brace = find_top_level(css, i, '{')
        if brace < 0:
            out.append(css[i:])
            break
        body_end = match_brace(css, brace)
        selector_text = css[i:brace]
        body = css[brace + 1:body_end]
        kept = [seg for seg in split_commas(selector_text) if needle not in seg.lower()]
        if kept:
            out.append(','.join(s.strip() for s in kept) + ' {' + body + '}')
        i = body_end + 1
    return ''.join(out)


def main():
    needle = sys.argv[1]
    for path in sys.argv[2:]:
        with open(path, encoding='utf-8') as f:
            original = f.read()
        cleaned = strip(original, needle)
        if cleaned != original:
            with open(path, 'w', encoding='utf-8') as f:
                f.write(cleaned)
            print(f"cleaned: {path} ({len(original)} -> {len(cleaned)} bytes)")
        else:
            print(f"unchanged: {path}")


if __name__ == '__main__':
    main()
