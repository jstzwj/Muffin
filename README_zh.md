# Muffin

一款快速、轻量的原生 Markdown 编辑器，使用 C++ 和 Qt 6 Widgets 构建。

**[English](README.md)**

---

## 为什么做 Muffin？

许多现代 Markdown 编辑器基于 Web 运行时构建，包体积和运行时开销都更大。Muffin 探索使用原生 Qt 实现专注、轻量的 Markdown 编辑体验：

- **启动更快** — 原生 C++，无 Chromium 运行时
- **体积更小** — 基于 Qt Widgets 和原生渲染
- **内存占用更低** — 不内嵌浏览器引擎
- **跨平台** — Windows、macOS、Linux 统一代码库

---

## 功能特性

| 状态 | 功能 |
|:----:|------|
| 已完成 | Qt Widgets 桌面外壳与原生中文菜单栏 |
| 已完成 | 文件新建、打开、保存、另存为流程 |
| 已完成 | 命令行打开文件与截图参数，便于视觉检查 |
| 已完成 | 通过 cmark-gfm 解析 GitHub Flavored Markdown |
| 已完成 | 基于 `QTextDocument` 的只读 Markdown 渲染视图 |
| 已完成 | 居中的正文列与底部字数状态栏 |
| 已完成 | 标题、分割线、表格、代码块、行内代码、链接、列表、引用块等渲染细节优化 |
| 已完成 | 主题菜单，支持 Github、Newsprint、Night、Pixyll、Whitey 预设 |
| 已完成 | 菜单实用功能：新建窗口、最近文件、文件属性、打开文件位置、打印、全屏、窗口置顶、缩放、字数统计窗口、源码自动换行 |
| 进行中 | 源码模式 / 渲染模式切换 |
| 计划中 | 真正所见即所得编辑与 Markdown 行内语法显示/隐藏 |
| 计划中 | 侧边栏、大纲、文件树、搜索、设置、导出、图片、数学公式渲染 |

---

## 技术栈

| 组件 | 技术 |
|------|------|
| 编程语言 | C++17 |
| 界面框架 | Qt 6 Widgets |
| 渲染模型 | `QTextDocument`、`QTextBrowser`、`QTextCursor` |
| Markdown 解析 | [cmark-gfm](https://github.com/github/cmark-gfm) |
| 构建系统 | CMake 3.24+ |
| 包管理器 | Conan 2.x |

### 架构概览

```text
Markdown 源文本
      │
      ▼
cmark-gfm AST
      │
      ▼
DocumentRenderer + ThemeStylesheet
      │
      ▼
QTextDocument 渲染视图
```

Muffin 始终以原始 Markdown 文本作为唯一真实来源。AST 与渲染后的 `QTextDocument` 都由源文本派生，主题切换会重新渲染文档，但不会修改 Markdown 内容。

---

## 构建

### 环境要求

| 依赖 | 版本 |
|------|------|
| CMake | >= 3.24 |
| C++ 编译器 | MSVC 2022、GCC 11+ 或 Clang 14+ |
| Qt | 6.5+，需要 Widgets、Gui、PrintSupport |
| Conan | 推荐 2.x |

### 方式一：使用系统 Qt

确保 `Qt6_DIR` 已设置，例如：

```text
C:\Qt\6.8.3\msvc2022_64\lib\cmake\Qt6
```

然后构建：

```bash
cmake --preset dev
cmake --build build/dev --config Debug
```

### 方式二：使用 Conan

```bash
conan install . --build=missing -s compiler.cppstd=17 -of build/conan
cmake --preset dev-conan
cmake --build build/dev --config Debug
```

### 运行

```bash
./build/dev/muffin
```

Windows/MSVC 多配置构建通常为：

```bash
./build/dev/Debug/muffin
./build/dev/Muffin-dist/muffin.exe
```

从命令行打开文件：

```bash
./build/dev/Muffin-dist/muffin.exe README_zh.md
```

保存视觉检查截图：

```bash
./build/dev/Muffin-dist/muffin.exe README_zh.md --screenshot screenshot.png
```

### 运行测试

```bash
ctest --test-dir build/dev --output-on-failure
```

Windows/MSVC 多配置构建：

```bash
ctest --test-dir build/dev -C Debug --output-on-failure
```

---

## 开发路线图

| 阶段 | 内容 | 状态 |
|:----:|------|:----:|
| 1 | 项目骨架、构建系统、基本窗口、打开/保存 | 已完成 |
| 2 | Markdown 解析、渲染预览、主题、原生编辑器界面、菜单可用功能 | 进行中 |
| 3 | 真正所见即所得编辑与 Markdown 行内语法显示/隐藏 | 计划中 |
| 4 | 侧边栏、大纲、文件树、搜索、设置、导出、图片、数学公式 | 计划中 |
| 5 | 打磨、打包、快捷键、国际化、发布构建 | 计划中 |

---

## 参与贡献

Muffin 目前处于早期开发阶段，欢迎贡献代码、提交问题和提出建议。

1. Fork 本仓库
2. 创建功能分支（`git checkout -b feature/my-feature`）
3. 提交更改
4. 发起 Pull Request

---

## 开源许可

MIT License。详见 [LICENSE](LICENSE)。
