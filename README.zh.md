<div align="center">

# Muffin

**快速、轻量的 Markdown 编辑器，所见即所得的实时渲染写作体验。**

Muffin 以 Markdown 文件为真实来源，同时将文档呈现为可直接编辑的页面——没有分栏，没有预览延迟。直接在渲染结果上编辑，编辑器会在底层自动维护 Markdown 源码。

<sup>支持 Linux、macOS 和 Windows。</sup>

<br />

<!-- License -->
<a href="LICENSE">
  <img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="License: MIT">
</a>
<!-- Platform -->
<a href="#下载">
  <img src="https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20%7C%20Linux-lightgrey.svg" alt="Platform">
</a>
<!-- Language -->
<a href="#外观">
  <img src="https://img.shields.io/badge/UI_languages-14-blueviolet.svg" alt="14 UI Languages">
</a>

<br />

<sup>其他语言：</sup>
<a href="README.md">English</a>

</div>

<br />

## 功能特性

### 编辑

- **实时所见即所得编辑** — 直接在渲染视图中写作和编辑。没有分栏，没有预览延迟。
- **源码模式** — 切换到带语法高亮的原始 Markdown 编辑器，两个视图之间光标位置完全同步。
- **专注模式**（`F8`）— 将非当前编辑的块淡化，让你专注于正在书写的内容。
- **打字机模式**（`F9`）— 光标始终保持在页面中央，配合柔和的动画滚动，如同纸张般自然。
- **可编辑表格** — 直接在渲染视图中添加、调整大小、对齐和删除行列。
- **可编辑代码块** — 内联编辑并通过 tree-sitter 支持 20+ 种语言的语法高亮，支持从自动补全下拉框设置语言。
- **可编辑数学块** — 通过 KaTeX 实时渲染 LaTeX 公式，提供编辑/预览双面板布局。
- **可编辑 HTML 块** — 内联编辑原始 HTML 块。
- **Front Matter** — 完整的 YAML front matter 支持。
- **丰富的段落命令** — 通过段落菜单切换标题、代码块、数学块等类型。
- **块移动** — 通过键盘快捷键上下移动段落。
- **查找替换** — 内置搜索栏，支持正则表达式。
- **多格式复制** — 将选中内容复制为 Markdown、HTML 或纯文本。

### 导航与组织

- **文档大纲** — 从侧边栏大纲面板跳转到任意标题。
- **文件树侧边栏** — 从文件夹树浏览和打开文件。
- **状态栏** — 显示解析时间、光标位置、字数统计，以及侧边栏和源码模式的快速切换。

### 外观

- **5 种内置主题** — GitHub、Newsprint、Night、Pixyll 和 Whitey。
- **外观偏好设置** — 字体大小、缩放比例、专注模式、打字机模式和状态栏可见性，所有设置跨会话持久化。
- **14 种界面语言** — English、简体中文、繁體中文、日本語、한국어、Tiếng Việt、Français、Español、Deutsch、Português (Brasil)、Русский、Italiano、Türkçe、Polski 和 Nederlands。

### 性能

- **原生 C++/Qt** — 不使用 Electron。启动快、内存低、滚动流畅。
- **增量解析** — 仅重新解析和重新渲染发生变化的块。
- **增量布局** — 通过顶层块范围差异比对，避免全量布局重建。
- **文本增量编辑** — 发送增量文本更新，而非全文替换。

## 下载

|         | Windows | macOS | Linux |
|:--------|:-------:|:-----:|:-----:|
| 安装包 | [MSIX（即将发布）](https://github.com/jstzwj/Muffin/releases) | [DMG（即将发布）](https://github.com/jstzwj/Muffin/releases) | [从源码构建](#开发) |

> 预构建安装包正在准备中。目前可以按照以下说明从源码构建。

## 开发

Muffin 使用 [Conan](https://conan.io/) 管理依赖，使用 CMake 构建。你需要 C++17 编译器、Qt 6（通过 Conan 安装）、Conan 2.x 和 CMake 3.24+。

### 构建

```bash
# 检测 Conan 配置
conan profile detect --force

# 安装依赖
conan install . -s build_type=Release -s compiler.cppstd=17 --build=missing

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

更多构建细节请参见 [CLAUDE.md](CLAUDE.md)。

### 翻译

```bash
cmake --build --preset conan-release --target update_translations   # 提取待翻译字符串
cmake --build --preset conan-release --target release_translations   # 编译 .qm 文件
```

## 贡献

欢迎贡献！请阅读[贡献指南](CONTRIBUTING.md)开始参与。

如果你发现了 bug 或有功能建议，欢迎[提交 Issue](https://github.com/jstzwj/Muffin/issues)。请附上操作系统、复现步骤和相关截图。

## 路线图

1. 打磨渲染编辑面 — 选区、光标、IME 和局部刷新的边缘情况。
2. 扩展块级变换 — 标题、段落、列表、引用、Front Matter 和任务列表命令。
3. 完善复杂块体验 — 表格、代码块、数学块、HTML 块和图片。
4. 增加查找替换、更完整的剪贴板行为、导出流程、打印和诊断功能。
5. 强化大文档的性能压力测试和 roundtrip 验证。

## 许可证

[**MIT**](LICENSE)
