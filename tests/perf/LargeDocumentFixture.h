#pragma once

#include <QString>
#include <QtGlobal>

namespace muffin::test {

inline QString makeInlineDenseMarkdown(qsizetype targetBytes) {
  static const char* units[] = {
      "This **bold** and _italic_ and `code` and [link](http://example.com/x) text.\n\n",
      "Another *em* phrase with **strong** words plus a [ref][1] inline node here.\n\n",
      "Plain sentence with `monospace` and **emphasis** scattered across the line.\n\n",
      "A line with [a link](https://site.example/path?q=1) and _underlined_ bits.\n\n",
  };

  QString document;
  document.reserve(targetBytes + 256);
  int index = 0;
  while (document.size() < targetBytes) {
    document += QString::fromLatin1(units[index++ % 4]);
  }
  return document;
}

inline QString makeMixedMarkdown(qsizetype targetUtf8Bytes) {
  QString document = QStringLiteral(
      "---\n"
      "title: Large document roundtrip fixture\n"
      "tags: [performance, regression]\n"
      "---\n\n"
      "# Large document roundtrip fixture\n\n");
  document.reserve(targetUtf8Bytes + 2048);
  qsizetype utf8Bytes = document.toUtf8().size();

  for (qint64 cycle = 0; utf8Bytes < targetUtf8Bytes; ++cycle) {
    const QString unit = QStringLiteral(
        "## Section %1\n\n"
        "Roundtrip editable paragraph %1 starts with plain text, **bold**, _emphasis_, "
        "`inline code`, [a link](https://example.com/%1), and Unicode "
        "\u4e2d\u6587 caf\u00e9 \U0001f642.\n\n"
        "> [!NOTE]\n"
        "> Quoted content for cycle %1 with a [reference][ref-%1].\n\n"
        "- [ ] pending item %1\n"
        "- [x] completed item %1\n"
        "  - nested item with ~~strikethrough~~\n\n"
        "1. ordered item %1\n"
        "2. ordered item with $x_%1 + y_%1$\n\n"
        "| Name | Value | Status |\n"
        "| :--- | ---: | :---: |\n"
        "| row-%1 | %1 | ready |\n"
        "| unicode | \u03b1\u03b2\u03b3 | \u2713 |\n\n"
        "```cpp\n"
        "int generated_%1() { return %1; }\n"
        "```\n\n"
        "$$\n"
        "\\sum_{i=0}^{%1} i = \\frac{%1(%1+1)}{2}\n"
        "$$\n\n"
        "<div data-cycle=\"%1\"><strong>HTML block %1</strong></div>\n\n"
        "```mermaid\n"
        "flowchart LR\n"
        "  A_%1[Start %1] --> B_%1{Ready?}\n"
        "  B_%1 -->|yes| C_%1[Done]\n"
        "```\n\n"
        "Reference use [cycle %1][ref-%1] and footnote [^note-%1].\n\n"
        "[ref-%1]: https://example.com/reference/%1 \"Reference %1\"\n"
        "[^note-%1]: Footnote content for cycle %1.\n\n"
        "---\n\n")
                             .arg(cycle);
    document += unit;
    utf8Bytes += unit.toUtf8().size();
  }

  return document;
}

}  // namespace muffin::test
