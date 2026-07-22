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

**补充回归（2026-07-23）：** cmark 脚注节点的起始列位于 `[^label]:` 标记之后，旧的精确 offset 匹配会漏掉真实节点并再合成一个重复脚注。现改为按源行匹配并恢复完整定义范围；512 组密集链接定义/脚注及缩进定义测试验证每个定义只出现一次且保持源顺序。

---

### 2. HTML 导出扩展与编辑器不一致

- [x] 让导出路径复用主解析器的扩展配置，或补一遍相同后处理 pass

**已实现（2026-07-08，方案 A 单源真值）：** 新增 `src/projection/MarkdownHtmlSerializer.{h,cpp}`，`SelectionSerializer::renderMarkdownToHtml` 不再用独立裸 cmark，改为把导出 markdown 喂给**编辑器自己的** `CmarkGfmParser::parseDocument`（跑完整流水线：cmark 解析 + `splitDelimInlines` + `annotateAlertKinds` + 全部 pass），再把得到的 `MarkdownNode` 树序列化为 cmark-gfm 兼容 HTML。因为树已完全解析（autolink/math/tasklist/table/代码语言都由 cmark 扩展在解析期就绪），序列化器是纯节点→HTML 映射，零再检测。结果：高亮→`<mark>`、下标→`<sub>`、上标→`<sup>`、GitHub Alerts→`<blockquote class="markdown-alert …">`+标题（并剥去 `[!KIND]` 标记）、emoji `:smile:`→字形，全部按设置门控（与编辑器口径一致）。镜像 cmark 的 HTML 约定（含打过补丁的 `mfn-inline-math`/`mfn-math-block` 类、tasklist 属性串、表格 `align=`、紧凑/松散列表 `<p>`），并复刻 cmark 的安全 URL 过滤（丢弃 `javascript:`/`vbscript:`/`data:`，`data:image/` 放行）以防 XSS。剪贴板复制 / 源码模式复制 / 文件导出 三路同受益。测试：`tests/projection/MarkdownHtmlSerializerTest.cpp`（逐类型精确串 + 安全 + emoji + 设置门控）。

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

## P1 — 常见扩展特性

### 5. 图表（Mermaid）

- [x] 接入原生图表渲染引擎，支持 ```` ```mermaid ```` 代码块

**已实现（2026-07-22）：** 使用纯 C++20/Qt 实现 Mermaid 11.16.0 兼容层，
支持 flowchart、sequence、class 和 state diagram。Node/Puppeteer 仅用于离线生成
上游 fixture，构建、测试、运行和发布产物均无 JavaScript/浏览器依赖。渲染通过
`MermaidRenderCache`、不可变 scene 和各 family painter 接入所见即所得编辑器及
打印/PDF；编辑时、关闭 diagrams 设置或选择“显示 Mermaid 源码”时保留代码围栏。
语法与语义错误通过统一结构化诊断保留 stage/code、实际值、预期值及精确行列；
预处理前后的 offset 映射可穿过 front matter、init directive 和注释。错误范围会在
源码中标记，点击诊断面板可直接把光标定位到对应位置。四类 scene painter 均支持
dirty viewport culling；屏幕路径只执行可见 primitive，打印/PDF 保持全量绘制。

**证据：** `src/mermaid/`、`src/render/BlockLayoutBuilder.cpp`、
`tests/render/RenderMermaidBlockTest.cpp`，以及 `tests/mermaid/` 下的 parser、layout、
scene、structural、pixel、coverage 和 differential fuzz 门禁。

**验证：** Conan Release 全量构建与 165/165 项测试通过，`dist` 目标已刷新。

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
| HTML 导出 | `src/projection/SelectionSerializer.cpp:renderMarkdownToHtml` → `src/projection/MarkdownHtmlSerializer.cpp`（树→HTML 序列化） |
| 自定义分隔符内联（高亮/上下标） | `src/parser/CmarkGfmParser.cpp:1458-1475`（`splitDelimInlines`） |
