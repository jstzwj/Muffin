#!/usr/bin/env python3
"""Generate a large Markdown document for Muffin editor stress testing.

Size-targeted (--size-mb) and deterministic (--seed). Produces ASCII-rich content that mirrors
the TypingPerfBench document model (inline-formatted paragraphs: **bold**, _em_, `code`,
[links]) plus a rotating mix of block types (headings, nested lists, tables, fenced code,
blockquotes, math blocks). ASCII content keeps 1 byte ~= 1 QChar ~= 1 parser char, so the
top-level-block and inline counts at a given size stay comparable to the bench's per-MB
calibration — unlike generate_long_markdown.py (CJK, ~3 bytes/char, different counts/MB).

Example:
    python scripts/generate_big_markdown.py --size-mb 100 --out build/big.md
    python scripts/generate_big_markdown.py --size-mb 500 --seed 7
"""
from __future__ import annotations

import argparse
import random
from pathlib import Path

# Varied-length English words so wrapped line widths look realistic (mix of short and long).
WORDS = [
    "the", "markdown", "editor", "renders", "paragraphs", "quickly", "even", "when", "document",
    "very", "large", "because", "layout", "rebuilds", "only", "the", "edited", "block", "while",
    "suffix", "blocks", "shift", "their", "cached", "positions", "without", "recomputing", "text",
    "inline", "formatting", "bold", "italic", "code", "spans", "links", "and", "images", "each",
    "contribute", "to", "the", "node", "count", "that", "drives", "per", "keystroke", "cost",
    "so", "rich", "content", "stresses", "the", "projection", "path", "more", "than", "plain",
    "performance", "measurement", "requires", "representative", "documents", "with", "realistic",
    "density", "of", "structure", "across", "headings", "lists", "tables", "quotes", "and", "fences",
    "cursor", "movement", "selection", "scrolling", "undo", "redo", "search", "replace", "export",
    "all", "exercise", "different", "subsystems", "that", "scale", "with", "document", "size",
    "deterministic", "generation", "keeps", "runs", "comparable", "across", "benchmark", "sessions",
]

LINKS = [
    "https://example.com/page",
    "https://docs.example.org/guide/intro",
    "https://site.example/path?q=markdown&n=42",
    "https://github.com/example/repo",
    "https://en.wikipedia.org/wiki/Markdown",
]

CODE_SAMPLES = [
    """```cpp
#include <QString>

QString render(const QString& input) {
  return input.trimmed();
}
```""",
    """```python
def fibonacci(n: int) -> list[int]:
    values = [0, 1]
    while len(values) < n:
        values.append(values[-1] + values[-2])
    return values[:n]
```""",
    """```json
{
  "editor": "Muffin",
  "stressTest": true,
  "features": ["heading", "table", "code", "math"]
}
```""",
    """```bash
# build & run the stress bench
cmake --build --preset conan-release
ctest --preset conan-release -R TypingPerfBench -V
```""",
]

# Each generator returns one block (no trailing blank line); blocks are joined by "\n\n".
FRONT_MATTER = """---
title: Muffin ASCII stress-test document
generator: scripts/generate_big_markdown.py
purpose: size-targeted ASCII content matching the TypingPerfBench document model
---

"""


def sentence(rng: random.Random, lo: int = 9, hi: int = 22) -> str:
    n = rng.randint(lo, hi)
    words = [rng.choice(WORDS) for _ in range(n)]
    text = " ".join(words)
    return text[0].upper() + text[1:] + "."


def inline_paragraph(rng: random.Random) -> str:
    """Mirror the bench: paragraphs dense in **bold**/_em_/`code`/[link] inline nodes."""
    parts = [
        sentence(rng),
        f"This **{rng.choice(WORDS)}** and _{rng.choice(WORDS)}_ and "
        f"`{rng.choice(WORDS)}` text with a [{rng.choice(WORDS)}]({rng.choice(LINKS)}) "
        f"reference and {sentence(rng).lower()}",
        f"Another {rng.choice(WORDS)} phrase with **{rng.choice(WORDS)}** words plus a "
        f"[{rng.choice(WORDS)}][1] inline node here.",
        f"Plain sentence with `{rng.choice(WORDS)}` and **{rng.choice(WORDS)}** "
        f"scattered across the line. {sentence(rng)}",
    ]
    return " ".join(rng.sample(parts, k=min(3, len(parts))))


def heading(rng: random.Random) -> str:
    level = rng.randint(2, 4)
    words = [rng.choice(WORDS) for _ in range(rng.randint(2, 5))]
    return f"{'#' * level} {' '.join(words).title()}"


def bullet_list(rng: random.Random) -> str:
    lines = []
    for i in range(rng.randint(3, 6)):
        lines.append(f"- {rng.choice(WORDS)} {sentence(rng).lower()}")
        if i % 2 == 0:
            for _ in range(rng.randint(1, 2)):
                lines.append(f"  - nested {rng.choice(WORDS)} {sentence(rng).lower()}")
    return "\n".join(lines)


def numbered_list(rng: random.Random) -> str:
    lines = []
    for i in range(1, rng.randint(4, 7)):
        lines.append(f"{i}. {sentence(rng)}")
    return "\n".join(lines)


def blockquote(rng: random.Random) -> str:
    lines = [f"> {sentence(rng)}", ">"]
    if rng.random() < 0.5:
        lines.append(f"> > {sentence(rng)}")
        lines.append(f"> > - {rng.choice(WORDS)} item inside the quote")
    lines.append(f"> {sentence(rng)}")
    return "\n".join(lines)


def code_fence(rng: random.Random) -> str:
    return rng.choice(CODE_SAMPLES)


def table(rng: random.Random) -> str:
    cols = rng.randint(2, 4)
    header = " | ".join(rng.choice(WORDS).title() for _ in range(cols))
    sep = " | ".join("---" for _ in range(cols))
    rows = ["| " + header + " |", "| " + sep + " |"]
    for _ in range(rng.randint(2, 5)):
        cells = []
        for _ in range(cols):
            cell = rng.choice(WORDS)
            if rng.random() < 0.3:
                cell = f"**{cell}**"
            elif rng.random() < 0.3:
                cell = f"`{cell}`"
            cells.append(cell)
        rows.append("| " + " | ".join(cells) + " |")
    return "\n".join(rows)


def math_block(rng: random.Random) -> str:
    body = rng.choice(["E = mc^2", r"\sum_{i=1}^{n} i = \frac{n(n+1)}{2}", "a^2 + b^2 = c^2"])
    return f"$$\n{body}\n$$"


# Weighted mix: paragraphs dominate (typing-realistic), other block types rotate in.
BLOCK_GENERATORS = [
    (inline_paragraph, 12),
    (heading, 2),
    (bullet_list, 2),
    (numbered_list, 1),
    (blockquote, 1),
    (code_fence, 1),
    (table, 1),
    (math_block, 1),
]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--size-mb", type=float, default=100.0, help="Target document size in MiB (approx).")
    parser.add_argument("--out", type=Path, default=Path("build/big_stress.md"), help="Output Markdown file.")
    parser.add_argument("--seed", type=int, default=42, help="RNG seed for deterministic output.")
    args = parser.parse_args()

    if args.size_mb <= 0:
        raise SystemExit("--size-mb must be positive")

    rng = random.Random(args.seed)
    generators = [g for g, _ in BLOCK_GENERATORS]
    weights = [w for _, w in BLOCK_GENERATORS]

    target_bytes = int(args.size_mb * 1024 * 1024)
    chunks: list[str] = [FRONT_MATTER]
    total = len(chunks[0])  # ASCII: bytes ~= chars

    while total < target_bytes:
        gen = rng.choices(generators, weights=weights, k=1)[0]
        block = gen(rng)
        chunks.append(block)
        chunks.append("\n\n")
        total += len(block) + 2

    # Trailing content so the document ends on a normal paragraph boundary.
    chunks.append("End of stress-test document.\n")

    content = "".join(chunks)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(content, encoding="utf-8", newline="\n")

    line_count = content.count("\n")
    size = args.out.stat().st_size
    print(f"Wrote {args.out} ({line_count} lines, {size} bytes ~= {size / (1024 * 1024):.1f} MiB, seed={args.seed})")


if __name__ == "__main__":
    main()
