<div align="center">

<img src="logo.svg" alt="Muffin" width="220">

# Muffin

**原生、轻量，所见即所得的实时渲染 Markdown 编辑器。**

Muffin 是一个基于 C++ 与 Qt 6 构建的块级 WYSIWYG Markdown 编辑器。它以 Markdown 文件为唯一真实来源，同时将文档呈现为可直接编辑的页面——没有分栏，没有预览延迟，不依赖 Electron。直接在渲染结果上编辑，编辑器会在底层自动维护 Markdown 源码。

Muffin 还提供与之同步的源码模式，你可以随时切换回原始 Markdown，且两个视图之间的光标位置完全互通。

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

<br />

## 功能特性

### ✍️ 编辑

- **实时所见即所得编辑** — 直接在渲染视图中写作和编辑。没有分栏，没有预览延迟。
- **源码模式** — 切换到带语法高亮的原始 Markdown 编辑器，两个视图之间光标位置完全同步。
- **专注模式**（`F8`）— 将非当前编辑的块淡化，让你专注于正在书写的内容。
- **打字机模式**（`F9`）— 光标始终保持在页面中央，配合柔和的动画滚动，如同纸张般自然。
- **可编辑表格** — 直接在渲染视图中添加、调整大小、对齐和删除行列，支持通过对话框插入表格。
- **可编辑代码块** — 内联编辑并通过 tree-sitter 支持 20+ 种语言的语法高亮，支持从自动补全下拉框设置语言。
- **可编辑数学块** — 通过 C++ 实现的完整 KaTeX 兼容引擎实时渲染 LaTeX 公式，提供编辑/预览双面板布局。支持自定义宏、braket 记号、交换图等。
- **可编辑 HTML 块** — 内联编辑原始 HTML 块，使用 Lexbor 解析和 Yoga 弹性盒子布局。
- **图片编辑** — 插入本地或网络图片、拖放上传、右键上下文菜单、预览渲染和批量处理。支持 WebP 和 AVIF 格式。
- **脚注与链接定义** — 完整支持脚注（`[^id]: text`）和链接引用定义的渲染、编辑和插入命令。
- **Front Matter** — 完整的 YAML front matter 支持。
- **丰富的段落命令** — 通过段落菜单切换标题、代码块、数学块等类型。
- **块移动** — 通过键盘快捷键上下移动段落。
- **查找替换** — 内置搜索栏，支持正则表达式、环绕搜索和替换/全部替换。
- **多格式复制** — 将选中内容复制为 Markdown、HTML 或纯文本。
- **链接交互** — 鼠标悬停时显示手型光标，Ctrl+点击在系统浏览器中打开链接。
- **文档打印** — 通过 文件 → 打印 (Ctrl+P) 打印当前文档。
- **换行符偏好** — 选择 Windows (CRLF) 或 Unix (LF) 换行符，可选保存时自动追加末尾换行。

### 🧭 导航与组织

- **文档大纲** — 从侧边栏大纲面板跳转到任意标题。
- **标题级别标记** — 在标题旁绘制 H3–H6 级别标记，便于快速识别文档层级。
- **文件树侧边栏** — 从文件夹树浏览和打开文件。
- **状态栏** — 显示解析时间、光标位置、字数统计，以及侧边栏和源码模式的快速切换。

### 🎨 外观

- **5 种内置主题** — GitHub、Newsprint、Night、Pixyll 和 Whitey。
- **外观偏好设置** — 字体大小、缩放比例、专注模式、打字机模式和状态栏可见性，所有设置跨会话持久化。
- **窗口置顶** — 将窗口保持在最前端 (Ctrl+Shift+F)。
- **15 种界面语言** — English、简体中文、繁體中文、日本語、한국어、Tiếng Việt、Français、Español、Deutsch、Português (Brasil)、Русский、Italiano、Türkçe、Polski 和 Nederlands。

### ⚡ 性能

- **原生 C++/Qt** — 不使用 Electron。启动快、内存低、滚动流畅。
- **增量解析** — 仅重新解析和重新渲染发生变化的块。
- **增量布局** — 通过顶层块范围差异比对，避免全量布局重建。
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
| `app` | 主窗口、偏好设置、侧边栏和界面语言管理。 |
| `editor` | 渲染编辑面、源码编辑器、查找栏和输入处理。 |
| `render` | 布局引擎与块/行内绘制，由主题驱动。 |
| `document` | Markdown 文档模型、大纲和源码 round-trip 映射。 |
| `parser` | 基于 cmark-gfm 的 Markdown 解析，由增量文本增量驱动。 |
| `blocks` | 各类块的运行时：代码、表格、数学、HTML、front matter、链接引用、literal。 |
| `edit` | 文本编辑操作：插入、删除、替换和块移动。 |
| `projection` | 渲染视图与原始 Markdown 之间的双向偏移映射。 |
| `export` | 文件导出管线。 |
| `math` | KaTeX 兼容的数学公式渲染。 |
| `theme` | 主题定义与运行时主题管理。 |
| `commands` | 命令注册表，将菜单动作与其实现解耦。 |
| `io` | 文件 I/O 与编码。 |
| `diagnostics` | 调试与性能分析工具。 |
| `unicode` | Unicode 文本工具。 |

### 第三方依赖

| 库 | 用途 | 许可证 |
|---------|---------|---------|
| Qt 6 | GUI 框架 | LGPL-3.0 |
| cmark-gfm | Markdown 解析 | BSD-2-Clause |
| tree-sitter | 代码块语法高亮 | MIT |
| KaTeX | 数学公式渲染 | MIT |

第三方源码位于 `third_party/`，作为 CMake 项目的一部分一同构建。

## 路线图

Muffin 已经支持几乎全部核心与扩展 Markdown 语法——标题、段落、列表、任务列表、引用、表格、代码块、行内格式、链接、reference-style 链接与图片、脚注、front matter、数学和 HTML。仍在推进的工作包括：

- [ ] 打磨渲染编辑面 — 选区、光标、IME 和局部刷新的边缘情况。
- [ ] 扩展导出流程 — PDF、HTML 和 DOCX 导出，支持模板。
- [ ] 强化性能 — 大文档压力测试、roundtrip 验证和性能诊断。
- [ ] 无障碍访问 — 键盘导航改进与屏幕阅读器支持。

## 贡献

欢迎贡献！请阅读[贡献指南](CONTRIBUTING.md)开始参与。

如果你发现了 bug 或有功能建议，欢迎[提交 Issue](https://github.com/jstzwj/Muffin/issues)。请附上操作系统、复现步骤和相关截图。

## 许可证

[**MIT**](LICENSE)

## Star History

[![Star History Chart](https://api.star-history.com/chart?repos=jstzwj/Muffin&type=date)](https://star-history.com/#jstzwj/Muffin&type=date)
