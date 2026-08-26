<div align="center">

<img src="logo.svg" alt="Muffin" width="220">

# Muffin

**原生、轻量、所见即所得的 Markdown 编辑器。**

Muffin 由 C++ 与 Qt 6 打造。它把你的 Markdown 渲染成一张可以直接编辑的页面——落笔即所见，源码在背后由编辑器自动维护。不必在源码与预览之间来回切换，输入也不会有延迟。

[下载](#下载) · [功能特性](#功能特性) · [从源码构建](#开发) · [架构](#架构)

![C++](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=c%2B%2B&logoColor=white)
![Qt](https://img.shields.io/badge/Qt-6-41CD52?logo=qt&logoColor=white)
![Platforms](https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20%7C%20Linux-2ea44f)
![UI Languages](https://img.shields.io/badge/UI_languages-15-blueviolet)
[![Releases](https://img.shields.io/badge/releases-GitHub-181717?logo=github)](https://github.com/jstzwj/Muffin/releases)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

<sup>其他语言：</sup>
<a href="README.md">English</a>

</div>

## 为什么选择 Muffin

- **原生应用，不是网页套壳** — 真正的 C++/Qt 桌面程序：不捆绑 Chromium，不依赖 Node 运行时，没有任何 Web 技术栈，因此启动飞快、内存占用极低。
- **秒开超大文件** — 懒加载的视口感知布局只渲染屏幕上的块，百兆字节的文档也能瞬间打开、毫不卡顿。增量解析与文本增量编辑则保证任何篇幅下打字都跟手。
- **真正的所见即所得** — 直接在渲染后的页面上写作与编辑，底层 Markdown 自动同步——没有并排预览，也没有渲染延迟。
- **以 Markdown 为唯一真实来源** — 你的 `.md` 文件可干净地往返转换；同步的源码模式让你随时切回原始 Markdown，两个视图间光标位置完全互通。
- **全链路主题化** — 一份主题定义同时驱动渲染页面、源码编辑器与全部界面外壳（菜单、侧边栏、对话框、状态栏）。内置主题以标准 CSS 编写，你也可以放入自己的 `.css`（或 `.json`）主题。

### 横向对比

在同类编辑器中，Muffin 是唯一原生、完全开源的所见即所得编辑器，也是唯一能在超大文件上保持流畅的。

| | Muffin | Typora | MarkText | Obsidian |
|:--|:--:|:--:|:--:|:--:|
| 核心技术 | C++ / Qt 6 | Electron | Electron | Electron |
| 开源 | ✅ | ❌ | ✅ | ❌ |
| 所见即所得 | ✅ | ✅ | ✅ | ❌ |
| 原生界面 | ✅ | ❌ | ❌ | ❌ |
| 本地优先 | ✅ | ✅ | ✅ | ✅ |
| 免费 | ✅ | 付费 | ✅ | 个人免费 |
| 大文件 | ✅ | ⚠️ | ⚠️ | ⚠️ |

<sub>Obsidian 的实时预览（Live Preview）是渲染后 Markdown 与原始语法的混合，并非真正的所见即所得。⚠️ 表示该编辑器能打开超大文档，但在滚动或编辑时可能出现卡顿。</sub>

<br />

## 功能特性

### ✍️ 编辑

- **实时所见即所得编辑** — 直接在渲染视图中写作和编辑。没有分栏，没有预览延迟。
- **源码模式** — 切换到带语法高亮的原始 Markdown 编辑器，两个视图之间光标位置完全同步。
- **专注模式**（`F8`）— 将非当前编辑的块淡化，让你专注于正在书写的内容。
- **打字机模式**（`F9`）— 光标始终保持在页面中央，配合柔和的动画滚动，如同纸张般自然。
- **智能标点** — 输入时自动把直引号转为弯引号，`--`/`---` 转为短破折号/长破折号，`...` 转为省略号。另有可选的"仅渲染"模式，只美化显示而不改动 Markdown 源码。
- **扩展 Markdown** — GitHub 风格的提示框（`[!NOTE]`、`[!TIP]`、`[!WARNING]` 等）、`==高亮==`、`~下标~`、`^上标^`、Setext 标题，以及 `\[ ... \]` LaTeX 数学块。
- **拼写检查** — 基于 Nuspell 的拼写检查，在渲染模式和源码模式下均标记拼写错误，支持右键建议菜单、忽略单词，并内置 11 种语言词典。
- **表情自动补全** — 输入 `:` 加短代码即可弹出基于内置数据集的表情选择器，按 `Tab` 确认；默认在输入时开启。
- **可编辑表格** — 直接在渲染视图中添加、调整大小、对齐和删除行列，支持通过对话框插入表格。
- **可编辑代码块** — 内联编辑并通过 tree-sitter 支持 20+ 种语言的语法高亮，支持从自动补全下拉框设置语言。代码工具提供逐行缩进/减少缩进与复制代码块内容。
- **可编辑数学块** — 通过 C++ 实现的完整 KaTeX 兼容引擎实时渲染 LaTeX 公式，提供编辑/预览双面板布局。支持自定义宏、braket 记号、交换图，以及一键"全部刷新"重新渲染所有公式。
- **原生 Mermaid 图表** — 通过纯 C++/Qt 引擎实时渲染 Mermaid 11.16 全部 38 个图族 ID，包括流程图/Flowchart ELK 内置的 Dagre 回退、泳道、时序图、类图、状态图、ER、需求图、饼图、象限图、旅程图、雷达图、XY 图、时间线、报文图、看板、思维导图、Block、GitGraph、C4、TreeView、事件建模、石川图、维恩图、桑基图、树图、Cynefin、Wardley、架构图、甘特图、Info 以及 Railroad/EBNF/ABNF/PEG 语法图家族，全部经 ```` ```mermaid ```` 代码围栏驱动，无 JavaScript 或浏览器运行时依赖。无效源码会保持可编辑，并显示精确的行列诊断、源码错误标记和点击跳转定位。共享标题/可访问性元数据、圆角流程、受控链接与提示、实时边动画、Sequence 链接菜单、确定性的原生 SVG/HTML 导出、逐图右键 SVG/PNG 保存、`muffin-mmdc` 无头批量渲染 CLI，以及自动生成的 533 行配置生效矩阵明确约束编辑器和导出行为。
- **可编辑 HTML 块** — 内联编辑原始 HTML 块，使用 Lexbor 解析和 Yoga 弹性盒子布局，并跟随当前主题配色。
- **脚注与链接定义** — 完整支持脚注（`[^id]: text`）和链接引用定义的渲染、编辑和插入命令。
- **Front Matter** — 完整的 YAML front matter 支持。
- **丰富的段落命令** — 通过段落菜单切换标题、代码块、数学块等类型。
- **块移动** — 通过键盘快捷键上下移动段落。
- **可配置缩进** — 选择默认缩进宽度（2/4/8 空格），并提供"对齐缩进"命令以匹配周围缩进。
- **查找替换** — 内置搜索栏，支持正则表达式、环绕搜索和替换/全部替换。
- **多格式复制** — 将选中内容复制为 Markdown、HTML 或纯文本。
- **复制为 Markdown** — 可选偏好：以纯文本复制时改为复制底层 Markdown 源码。
- **整行复制与剪切** — 未选中文本时，复制和剪切作用于整行。
- **链接交互** — 鼠标悬停时显示手型光标，Ctrl+点击在系统浏览器中打开链接。
- **换行渲染** — 通过 Markdown 偏好设置，可将单个换行渲染为硬换行，或按 CommonMark 将软换行合并为一个段落。硬换行 `<br>` 在三种形式（`<br>`、`<br/>`、`<br />`）下均可正确渲染与编辑。
- **换行符偏好** — 选择 Windows (CRLF) 或 Unix (LF) 换行符，可选保存时自动追加末尾换行。

### 🧭 导航与组织

- **文档大纲** — 从侧边栏大纲面板跳转到任意标题，长文档可折叠子树。
- **标题级别标记** — 在标题旁绘制 H3–H6 级别标记，便于快速识别文档层级。
- **文件树侧边栏** — 从文件夹树浏览和打开文件。
- **快速打开** — 对工作区内任意文件进行模糊跳转，优先展示最近使用与最近修改的文件。
- **文件操作** — 通过文件菜单移动、删除、在文件管理器中显示、以指定编码重新打开，以及保存所有已打开文件。
- **草稿恢复** — 在崩溃或异常退出后恢复未保存的内容。
- **自动保存** — 可选的定时保存与退出时保存，保护未保存的工作。
- **自绘状态栏** — 跟随主题自绘的状态栏，显示解析时间、光标位置和字数统计，点击可打开统计弹窗（字数、字符数、行数、阅读时长和选区计数），并内置拼写检查语言快速切换。
- **内置帮助** — 通过帮助菜单在内置查看器中查看快速入门、Markdown 参考与致谢，使用原生 Markdown 引擎渲染，支持前进/后退导航。
- **自动检查更新** — 启动时检查新版本（每 24 小时一次），也可从帮助菜单手动检查。

### 📤 导出与导入

- **多格式导出** — 原生导出为 PDF（自带渲染器）、HTML 和纯 HTML；通过外部的 [Pandoc](https://pandoc.org) 进程导出 Word (DOCX)、ODT、RTF、ePub、LaTeX、MediaWiki、RST、Textile 和 OPML。
- **通过 Pandoc 导入** — 通过 文件 → 导入 把其他格式的文档转换为 Markdown。
- **文档打印** — 通过 文件 → 打印 (Ctrl+P) 打印当前文档。

### 🖼️ 图片

- **图片编辑** — 插入本地或网络图片、拖放上传、右键上下文菜单、预览渲染和批量处理。支持 WebP 和 AVIF 格式，并内置 PNG/JPEG 解码器，确保无论 Qt 插件是否可用都能可靠加载图片。
- **图片插入策略** — 一套统一管控粘贴、对话框与拖放场景下图片插入的系统，提供六种可配置的操作（不处理、复制到 `./`、`./assets`、`./<文件名>.assets`、上传或自定义目录），支持 front matter 中的上传覆盖，并可配置相对路径、前导斜杠与 URL 转义格式。
- **自定义命令上传** — 通过可配置的外部命令上传图片，将其标准输出按行解析为图片 URL。

### 🎨 外观

- **5 种内置主题** — GitHub、Newsprint、Night、Pixyll（现已使用衬线正文字体）和 Whitey。
- **自定义主题** — 内置主题以标准 CSS 编写，你也可以用同样方式编写自己的主题：将 `.css`（或 `.json`）文件放入主题文件夹后立即出现在动态主题菜单中。也可从菜单或外观偏好页面直接导入主题。
- **外观偏好设置** — 字体大小、缩放比例、专注模式、打字机模式和状态栏可见性，所有设置跨会话持久化。
- **窗口置顶** — 将窗口保持在最前端 (Ctrl+Shift+F)。
- **15 种界面语言** — English、简体中文、繁體中文、日本語、한국어、Tiếng Việt、Français、Español、Deutsch、Português (Brasil)、Русский、Italiano、Türkçe、Polski 和 Nederlands。

### ⚡ 性能

- **原生 C++/Qt** — 不使用 Electron。启动快、内存低、滚动流畅。
- **懒加载视口布局** — 打开文档时只为整个文件计算轻量的高度估算；完整的文本排版、语法高亮、数学与 HTML 渲染都延迟到块滚动进入视口时才进行。提升屏幕外块时配合锚点校正保持页面稳定，让大文件打开和滚动都不卡顿。
- **增量解析** — 仅重新解析和重新渲染发生变化的块。
- **增量布局** — 编辑时通过顶层块范围差异比对，避免全量布局重建。
- **文本增量编辑** — 发送增量文本更新，而非全文替换。

## 下载

|         | Windows | macOS | Linux |
|:--------|:-------:|:-----:|:-----:|
| 安装包 | [MSI](https://github.com/jstzwj/Muffin/releases) | [DMG](https://github.com/jstzwj/Muffin/releases) | [从源码构建](#开发) |

## 开发

Muffin 使用 [Conan](https://conan.io/) 管理依赖，使用 CMake 构建。你需要 C++20 编译器（MSVC 2022+、GCC 12+ 或 Clang 15+）、Qt 6（通过 Conan 安装）、Conan 2.x 和 CMake 3.24+。

### 构建

```bash
# 检测 Conan 配置
conan profile detect --force

# 安装依赖
conan install . -s build_type=Release -s compiler.cppstd=20 --build=missing

# 配置并构建
cmake --preset conan-default
cmake --build --preset conan-release
```

### 测试

```bash
ctest --preset conan-release --output-on-failure
```

### 运行

```bash
# 构建可分发包
cmake --build --preset conan-release --target dist

# 启动
./build/dist/Muffin          # Linux / macOS
.\build\dist\Muffin.exe      # Windows
```

更多构建细节与常见问题请参见 [CLAUDE.md](CLAUDE.md)。

### 翻译

```bash
cmake --build --preset conan-release --target update_translations   # 提取待翻译字符串
cmake --build --preset conan-release --target release_translations   # 编译 .qm 文件
```

## 架构

Muffin 以原生 block tree 作为运行时模型。导入时，Markdown 被解析为结构化的可编辑块；保存时，block tree 会重新序列化为规范化 Markdown。双向的行内投影（inline projection）让渲染视图与原始源码始终保持映射。

| 层级 | 职责 |
| --- | --- |
| `app` | 主窗口、偏好设置、侧边栏、快速打开和界面语言管理。 |
| `editor` | 渲染编辑面、源码编辑器、查找栏和输入处理。 |
| `render` | 布局引擎与块/行内绘制，由主题驱动。 |
| `document` | Markdown 文档模型、大纲和源码 round-trip 映射。 |
| `parser` | 基于 cmark-gfm 的 Markdown 解析，由增量文本增量驱动。 |
| `blocks` | 各类块的运行时：代码、表格、数学、HTML、front matter、链接引用、literal。 |
| `html` | HTML 块布局引擎 —— 供 HTML 块使用的 Lexbor 解析与 Yoga 弹性盒子布局。 |
| `edit` | 文本编辑操作：插入、删除、替换和块移动。 |
| `projection` | 渲染视图与原始 Markdown 之间的双向偏移映射。 |
| `export` | 原生 PDF/HTML 导出，以及用于其他格式的 Pandoc 运行器。 |
| `math` | KaTeX 兼容的数学公式渲染。 |
| `unicode` | 用于光标移动与选区的词边界切分。 |
| `theme` | 统一的主题定义、外壳样式表生成与运行时主题管理。 |
| `image` | 图片插入策略与自定义命令上传。 |
| `io` | 文件 I/O、编码与图片文件操作。 |
| `spellcheck` | Nuspell 拼写检查与内置词典。 |
| `commands` | 命令注册表，将菜单动作与其实现解耦。 |

### 第三方依赖

| 库 | 用途 | 许可证 |
|---------|---------|---------|
| Qt 6 | GUI 框架 | LGPL-3.0 |
| cmark-gfm | GitHub 风格 Markdown 解析 | BSD-2-Clause |
| tree-sitter | 代码块语法高亮 | MIT |
| KaTeX | 数学公式渲染（内嵌字体） | MIT |
| Yoga | HTML 块的弹性盒子布局 | MIT |
| Lexbor | HTML 解析 | Apache-2.0 |
| Nuspell | 拼写检查 | LGPL-3.0-or-later |
| ICU | Unicode 文本处理 | ICU License |
| libwebp / libavif / dav1d | WebP 和 AVIF 图片解码（libavif 由内置源码构建，dav1d 为其 AV1 解码器） | BSD-3-Clause / BSD-2-Clause |
| libpng / libjpeg | PNG 和 JPEG 图片解码 | libpng / libjpeg |

第三方源码位于 `third_party/`，作为 CMake 项目的一部分一同构建。

## 路线图

Muffin 已经支持几乎全部核心与扩展 Markdown 语法——标题、段落、列表、任务列表、引用、表格、代码块、行内格式、链接、reference-style 链接与图片、脚注、front matter、数学和 HTML——并已实现多格式导出与导入。仍在推进的工作包括：

- [x] 打磨渲染编辑面 — 选区（表格跨格高亮、主题化颜色、Typora 式连续填充、三击选段、拖拽阈值/扩展/自动滚动）、光标（闪烁、残影擦除修复）、IME（完整上下文查询、源码模式组合渲染、失焦重置）、局部刷新（BuiltStamp 旁路、光标脏矩形）。
- [x] 加入原生 Mermaid 流程图、泳道、时序图、类图、状态图、ER、需求图、饼图、象限图、旅程图、雷达图、XY 图、时间线、报文图、看板、思维导图、Block、GitGraph、C4、TreeView、事件建模、石川图、维恩图、桑基图、树图、Cynefin、Wardley、架构图、甘特图和 Info 图。
- [x] 加入 Mermaid 安全链接/提示、实时 Flowchart 边动画和 Sequence 参与者菜单。
- [x] 加入确定性的原生 Mermaid SVG 导出，包括 HTML 内联输出和右键保存单图。
- [x] 补齐 SVG marker URL 绝对化兼容（流程图/泳道/时序图在导出上下文提供文档 URL 时序列化绝对 marker 引用；其余 11.16 图族保持片段引用，与上游一致）。
- [ ] 继续扩展 GFM 覆盖。
- [ ] 强化性能 — 结构化 1–100 MB parser 阶段已接近线性增长；AST 块节点和内联节点本体各缩小 96 B，100 MiB 往返常驻内存降低约 834 MiB。下一步降低 cmark 构树峰值和超大文档布局成本。
- [x] 无障碍访问 — 词/段落/页级键盘导航、表格 Tab 导航、焦点逃逸（Ctrl+Tab 与 F6 面板循环）、双编辑画布的 QAccessible 屏幕阅读器适配。

## 贡献

欢迎贡献！请阅读[贡献指南](CONTRIBUTING.md)开始参与。

如果你发现了 bug 或有功能建议，欢迎[提交 Issue](https://github.com/jstzwj/Muffin/issues)。请附上操作系统、复现步骤和相关截图。

## 许可证

[**MIT**](LICENSE)

## Star History

[![Star History Chart](https://api.star-history.com/chart?repos=jstzwj/Muffin&type=date)](https://star-history.com/#jstzwj/Muffin&type=date)
