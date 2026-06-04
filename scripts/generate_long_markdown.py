#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path

NOVELS = [
    ("红楼梦", "贾雨村偶遇故人，荣宁二府灯火如昼，诗社新题又起。"),
    ("西游记", "唐僧师徒越过高山，云雾之间妖风骤起，行者举棒相迎。"),
    ("水浒传", "梁山泊鼓角齐鸣，众好汉议事聚义，酒碗相碰声震厅堂。"),
    ("三国演义", "群雄逐鹿，帐中灯影摇曳，谋士展卷论天下大势。"),
]

POEM_LINES = [
    "青山隐隐水迢迢，秋尽江南草未凋。",
    "一声梆子惊残梦，万点灯花照短檠。",
    "云卷旌旗连北斗，风催战鼓过长亭。",
    "旧事翻成新话本，闲人读罢又三更。",
]

FORMATS = [
    "**粗体**、*斜体*、~~删除线~~、`行内代码` 与 [示例链接](https://example.com)。",
    "脚注引用[^note]、自动链接 <https://example.com/path?q=markdown>，以及普通中文标点。",
    "混合强调：***粗斜体***、__另一种粗体__、_另一种斜体_。",
    "转义字符测试：\\*不是斜体\\*、\\[不是链接\\]、\\`不是代码\\`。",
]

TABLE = """| 编号 | 人物 | 阵营 | 备注 |
| ---: | :--- | :---: | --- |
| 1 | 孙悟空 | 取经队伍 | 支持 **粗体** 与 `code` |
| 2 | 林黛玉 | 大观园 | 单元格内含中文、英文 English、数字 123 |
| 3 | 诸葛亮 | 蜀汉 | 很长很长很长很长很长很长的一段备注 |
"""

ADMONITION = """> 引用层级一：古人云，读万卷书，行万里路。
>
> > 引用层级二：这里测试嵌套引用、空行和连续段落。
> >
> > - 引用里的列表项 A
> > - 引用里的列表项 B
"""

CODE_BLOCKS = [
    """```cpp
#include <QString>
#include <QTextDocument>

QString renderTitle(const QString& title) {
    return QStringLiteral("# ") + title.trimmed();
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
  "features": ["heading", "table", "code", "task-list"],
  "largeDocument": true
}
```""",
]

RAW_HTML = """<details>
<summary>HTML 折叠块测试</summary>

这里包含一段原始 HTML，用于测试编辑器对 HTML block 的处理。

</details>
"""

FRONT_MATTER = """---
title: Muffin 超长 Markdown 压力测试文档
generated_by: scripts/generate_long_markdown.py
purpose: 覆盖标题、段落、列表、表格、代码块、引用、任务列表、脚注、HTML、特殊字符与超长正文
---
"""


def paragraph(novel: str, seed: str, chapter: int, index: int) -> str:
    sentence = (
        f"第 {chapter:03d} 回第 {index:02d} 段，{novel}测试文本：{seed}"
        f"这一段用于模拟连续中文长文输入、换行重排、滚动定位、撤销重做和增量解析。"
    )
    tail = "天地玄黄，宇宙洪荒，日月盈昃，辰宿列张。"
    return sentence + tail * ((index % 4) + 1)


def chapter_block(chapter: int) -> str:
    novel, seed = NOVELS[(chapter - 1) % len(NOVELS)]
    lines: list[str] = []

    lines.append(f"## 第 {chapter:03d} 回：{novel}格式混排测试")
    lines.append("")
    lines.append(f"### 章节导语")
    lines.append("")
    lines.append(paragraph(novel, seed, chapter, 1))
    lines.append("")
    lines.append(FORMATS[chapter % len(FORMATS)])
    lines.append("")

    if chapter % 2 == 0:
        lines.extend([
            "### 无序列表与嵌套列表",
            "",
            "- 第一层项目：人物、地点、事件",
            "  - 第二层项目：测试缩进与折叠",
            "    - 第三层项目：测试深层列表渲染",
            "- 第二个项目包含 `inline code` 和 **强调文本**",
            "",
        ])
    else:
        lines.extend([
            "### 有序列表与任务列表",
            "",
            "1. 打开超长文档",
            "2. 滚动到当前章节",
            "3. 修改其中一段文字",
            "   1. 子步骤一：验证光标位置",
            "   2. 子步骤二：验证撤销重做",
            "- [x] 已覆盖标题",
            "- [ ] 待观察编辑性能",
            "- [ ] 待观察保存性能",
            "",
        ])

    lines.append("### 长段落组")
    lines.append("")
    for index in range(2, 18):
        lines.append(paragraph(novel, seed, chapter, index))
        lines.append("")

    if chapter % 3 == 0:
        lines.append("### 表格测试")
        lines.append("")
        lines.append(TABLE)

    if chapter % 4 == 0:
        lines.append("### 引用测试")
        lines.append("")
        lines.append(ADMONITION)

    if chapter % 5 == 0:
        lines.append("### 代码块测试")
        lines.append("")
        lines.append(CODE_BLOCKS[(chapter // 5) % len(CODE_BLOCKS)])
        lines.append("")

    if chapter % 7 == 0:
        lines.append("### 诗词与分隔线")
        lines.append("")
        lines.extend(POEM_LINES)
        lines.append("")
        lines.append("---")
        lines.append("")

    if chapter % 11 == 0:
        lines.append("### 原始 HTML 测试")
        lines.append("")
        lines.append(RAW_HTML)

    lines.append(f"[^note]: 第 {chapter:03d} 回脚注，用于测试脚注解析与跳转。")
    lines.append("")
    return "\n".join(lines)


def build_document(chapters: int) -> str:
    parts = [
        FRONT_MATTER,
        "# Muffin 超长 Markdown 压力测试文档",
        "",
        "这份文档由脚本生成，包含大量仿中文古典小说段落和多种 Markdown 结构，用于测试 Muffin 编辑器在超长文档中的解析、渲染、滚动、编辑和保存表现。",
        "",
        "## 目录片段",
        "",
    ]

    for chapter in range(1, min(chapters, 40) + 1):
        parts.append(f"- [第 {chapter:03d} 回](#第-{chapter:03d}-回{NOVELS[(chapter - 1) % len(NOVELS)][0]}格式混排测试)")
    if chapters > 40:
        parts.append(f"- ……其余 {chapters - 40} 回省略目录，以避免目录本身过长。")
    parts.append("")

    for chapter in range(1, chapters + 1):
        parts.append(chapter_block(chapter))

    parts.extend([
        "## 末尾边界用例",
        "",
        "最后一行前包含空行、特殊符号和 Unicode：￥ € ✓ ★ 𠮷 😀。",
        "",
        "`末尾行内代码` **末尾粗体** [末尾链接](https://example.com/end)",
        "",
    ])
    return "\n".join(parts)


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate a very large Markdown document for Muffin editor testing.")
    parser.add_argument("--chapters", type=int, default=240, help="Number of generated chapters.")
    parser.add_argument("--output", type=Path, default=Path("docs/long_markdown_stress_test.md"), help="Output Markdown file.")
    args = parser.parse_args()

    if args.chapters < 1:
        raise SystemExit("--chapters must be at least 1")

    content = build_document(args.chapters)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(content, encoding="utf-8", newline="\n")

    line_count = content.count("\n") + 1
    size = args.output.stat().st_size
    print(f"Wrote {args.output} ({line_count} lines, {size} bytes, {args.chapters} chapters)")


if __name__ == "__main__":
    main()
