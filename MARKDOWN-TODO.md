# Markdown 特性支持 TODO

> 基于对解析层（cmark 集成）、块/内联转换、渲染分发、HTML 导出路径的代码扫描整理（2026-07-08）。
> 每一项都标注了证据（文件:行号），便于直接定位修改。`- [ ]` 表示待办，`- [x]` 表示已完成。

---

## 优先级约定

- **P0** — 现有功能不完整 / 一致性 bug。修复成本低、收益高，建议优先处理。
- **P1** — 常见 GFM / Markdown 扩展特性，社区文档中高频使用。
- **P2** — 锦上添花的扩展能力，可延后。

---

## P0 — 不完整 / 一致性问题

### 1. 脚注引用（内联 `[^label]`）未渲染

- [x] 在 `mapInlineType` 中补充 `CMARK_NODE_FOOTNOTE_REFERENCE` 的映射

**已实现（2026-07-08）：** 新增 `InlineType::FootnoteReference` + `InlineNode::footnoteReference(label, ordinal)`。`convertInline` 用 cmark 解析期的 `cmark_node_parent_footnote_def` + `cmark_node_get_literal`（引用 literal=序号、定义 literal=标签）取数据。投影按**原子叶子**建模：非激活态序号 `1` 作 N:1 上标链接 span（`superscript`+`link` 正交标志）+ `linkRange` 带 `#fn:<label>`；激活态 reveal 原始 `[^label]`。导航复用 TOC 的片段拦截模式：`EditorView` 拦截 `#fn:` → `DocumentLayout::footnoteDefinitionIdForLabel` 按标签查定义块 → `scrollToNode`。测试：`tests/render/RenderInlineProjectionTest.cpp`（testFootnoteReferenceRendersAsSuperscriptLink / RevealsLiteralWhenActive / ResolvesToDefinition）。

---

### 2. HTML 导出扩展与编辑器不一致

- [ ] 让导出路径复用主解析器的扩展配置，或补一遍相同后处理 pass

**现状：** 导出路径（`renderMarkdownToHtml`）用的是**另一个独立的 cmark 实例**，它只接 cmark 原生扩展（table / strikethrough / autolink / tasklist / math），**不知道 Muffin 自己的 `splitDelimInlines` 那一套**。结果：编辑器里能渲染的特性，导出 HTML 时会消失：

| 特性 | 编辑器 | HTML 导出 |
|---|:---:|:---:|
| 高亮 `==text==` | ✅ | ❌ 丢失 |
| 下标 `~text~` | ✅ | ❌ 丢失 |
| 上标 `^text^` | ✅ | ❌ 丢失 |
| GitHub Alerts（`> [!NOTE]` 等） | ✅ | ❌ 无 alert 标注 |

**证据：** `src/projection/SelectionSerializer.cpp:174-207`（独立的 cmark parser，扩展配置与主解析器不同）

**建议方案：**
- 方案 A：抽取主解析器的扩展配置 + 后处理 pass，导出路径复用。
- 方案 B：在导出的 cmark 输出之后，补一遍 highlight / subscript / superscript / alerts 的 HTML 后处理（与编辑器口径一致）。

---

### 3. 高亮 / 下标 / 上标默认关闭

- [ ] 评估是否默认开启这三个特性（或至少高亮）

**现状：** `==高亮==`、`~下标~`、`^上标^` 在编辑器内已实现，但 `ParseOptions` 默认全部关闭，用户不进设置开关就看不到效果。

**证据：** `src/parser/MarkdownParser.h:25-27`（`enableHighlight` / `enableSubscript` / `enableSuperscript` 均默认 `false`）

**注意：** 下标 `~x~` 与删除线 `~~x~~` 共用波浪号，开启前需确认 `splitDelimInlines` 的 run-length 区分逻辑不会误伤（删除线用 run-length 2，下标用 1）。

---

### 4. HTML 内联 `<sub>` / `<sup>` 未对齐渲染

- [ ] 让 `<sub>` / `<sup>` 走上下标对齐路径，而非通用 HTML 渲染

**现状：** `<sub>` / `<sup>` 被列入允许的 HTML 标签，但只通过 `InlineHtmlRenderer` 当作通用 HTML 渲染，**未应用上下标垂直对齐**。

**证据：** `src/parser/HtmlSanitizer.cpp:42`（允许该标签）；渲染侧无专属对齐处理

**建议方案：** 在投影/渲染层识别 `<sub>` / `<sup>`，复用 `InlineLayout.cpp:1602/1604` 的 `AlignSubScript` / `AlignSuperScript` 对齐。

---

## P1 — 常见扩展特性（完全未支持）

### 5. 图表（Mermaid / 流程图 / 时序图）

- [ ] 接入图表渲染引擎，支持 ```` ```mermaid ```` 代码块

**现状：** 明确未实现。设置页的 diagrams 复选框被禁用并标注 "coming soon"。

**证据：** `src/app/PrefsMarkdownPage.cpp:235-244`

**备注：** README Roadmap 已把 diagrams (Mermaid) 列为下一步 Markdown 特性。可选实现：嵌入 mermaid.js（或其精简 C++ 端口），在代码块渲染时按语言分发。

---

### 6. 标题锚点 / 自动 ID

- [ ] 为标题生成 slug id，支持 `# Heading {#id}` 显式锚点

**现状：** 无 slug 生成逻辑，标题不可作为页内链接目标。

**证据：** 全树未找到 `headingId` / `slugify` 相关代码

---

### 7. Emoji 短代码渲染（`:smile:` → 字形）

- [x] 在渲染管线中加入短代码 → 字形的转换（与输入补全解耦）

**已实现（2026-07-08）：** 把 `:shortcode:` 作为 `InlineProjection` 的**第三种 decode span**（与 `\*` 转义、`&amp;` 实体同构），复用既有 N:1 源→显示偏移机器。共享 `src/editor/EmojiDictionary.{h,cpp}` 字典（从 `:/emoji/emoji.txt` 惰性加载），输入补全与渲染共用一份。新增 `markdown/renderEmoji` 设置（默认开启，可在 Markdown 设置页关闭）。测试：`tests/render/RenderInlineProjectionTest.cpp`（testEmojiShortcodeDisplay / testEmojiOffsetMapping / testEmojiAndEscapeMix / testEmojiRevealsLiteralWhenActive）。

---

### 8. `[TOC]` 目录渲染

- [x] 把 `[TOC]` 标记渲染为真实的目录（链接到标题锚点）

**已实现（2026-07-08）：** `buildParagraphLike` 检测 `[TOC]` 段落（build 期，不改 parser），光标不在块内时用 `buildOutline()` 的标题列表合成缩进链接目录（`BlockLayout::isToc_` + `paintToc`），块高 = 行数 × 行高。Ctrl+点击条目命中测试产出 `#toc:<nodeId>` 片段，`EditorView::mousePressEvent` 拦截并 `scrollToNode`。光标进入块时重建为普通段落显示字面 `[TOC]`（Typora 式 live toggle）。测试：`tests/render/TableOfContentsTest.cpp`。不依赖标题 HTML 锚点（用 NodeId 直接跳转）。

---

### 9. 跨块格式化（下划线 / 链接 / 图片）

- [ ] 支持跨块选区的下划线、链接、图片样式切换

**现状：** 下划线、链接、图片的样式切换仅支持**单块选区**，跨块选区会触发 `unsupportedStyleRequested`。

**证据：** `src/commands/StylizeController.cpp:67`（下划线）、`:788-791`（链接）、`:860-861`（图片）

---

## P2 — 扩展特性（完全未支持）

### 10. 定义列表（Pandoc / PHP-Markdown 风格）

- [ ] 支持 `术语\n: 定义` 语法定义列表

**现状：** 无任何代码，未接 cmark `def_list` 扩展。

**建议方案：** 可选 cmark-gfm 的 `definition_list` 扩展（需自建或引入），新增 `BlockType::DefinitionList` / `DefinitionTerm` / `DefinitionItem`。

---

### 11. Wiki 链接（`[[目标]]`）

- [ ] 支持 `[[目标]]` / `[[目标|显示文本]]` 内部链接

**现状：** 无代码。

---

### 12. 缩写（`*[ABBR]: 解释`）

- [ ] 支持缩写定义与悬停提示

**现状：** 无代码。

---

### 13. tagfilter 扩展

- [ ] 评估是否启用 cmark 的 tagfilter 扩展（对原始 HTML 标签做安全过滤）

**现状：** 全树从未引用。若导出 HTML 对外发布，未过滤的原始 HTML 标签会原样透出。

---

## 参考路径速查

| 关注点 | 文件 |
|---|---|
| cmark 扩展挂载 | `src/parser/CmarkGfmParser.cpp:1542-1559`（`attachExtensions`） |
| 解析选项默认值 | `src/parser/MarkdownParser.h:17-36`（`ParseOptions`） |
| 块类型映射 | `src/parser/CmarkNodeAdapter.cpp:276-293`（`mapBlockType`） |
| 内联类型映射 | `src/parser/CmarkNodeAdapter.cpp:295-309`（`mapInlineType`） |
| 块类型枚举 | `src/document/MarkdownTypes.h:5-23`（`BlockType`） |
| 内联类型枚举 | `src/document/MarkdownTypes.h:32-49`（`InlineType`） |
| 块构建分发 | `src/render/BlockLayoutBuilder.cpp:448-473` |
| 块渲染分发 | `src/render/BlockLayout.cpp:925-959` |
| 内联投影 | `src/projection/InlineProjection.cpp:1192-1435` |
| 内联渲染 | `src/render/InlineLayout.cpp` |
| HTML 导出 | `src/projection/SelectionSerializer.cpp:174-207` |
| 自定义分隔符内联（高亮/上下标） | `src/parser/CmarkGfmParser.cpp:1458-1475`（`splitDelimInlines`） |
