# Muffin

一款快速、轻量的 Markdown 所见即所得编辑器——使用 C++ 和 Qt 6 对 Typora 的原生复刻。

**[English](README.md)**

---

## 为什么做 Muffin？

Typora 是一款优秀的编辑器，但它基于 Electron 构建（约 150 MB，冷启动慢）。Muffin 使用原生 Qt 实现与 Typora 1:1 的功能对等，同时带来：

- **即时启动** — 原生 C++，无 Chromium 开销
- **小巧体积** — 目标二进制 < 30 MB
- **低内存占用** — 无内嵌浏览器引擎
- **跨平台** — Windows、macOS、Linux 统一代码库

---

## 功能特性

| 状态 | 功能 |
|:----:|------|
| 开发中 | Typora 风格的所见即所得编辑 — 渲染视图与源码无缝切换 |
| 开发中 | GitHub 风格 Markdown（表格、删除线、任务列表、自动链接） |
| 开发中 | 行内语法切换 — `**粗体**` 渲染为 **粗体**，点击显示语法标记 |
| 开发中 | 主题支持 — 亮色、暗色、自定义 CSS |
| 计划中 | 图片粘贴 / 拖放 / 缩放 |
| 计划中 | 数学公式渲染（LaTeX） |
| 计划中 | 导出 HTML / PDF |
| 计划中 | 大纲侧栏、文件树 |
| 计划中 | 国际化（英文、中文） |

---

## 技术栈

| 组件 | 技术 |
|------|------|
| 编程语言 | C++17 |
| 界面框架 | Qt 6 (Widgets) |
| Markdown 解析 | [cmark-gfm](https://github.com/github/cmark-gfm) |
| 构建系统 | CMake 3.24+ |
| 包管理器 | Conan 2.x |

### 架构概览

```
 ┌─────────────┐     解析       ┌────────────┐    渲染      ┌────────────────┐
 │  Markdown    │──────────────►│  cmark-gfm │────────────►│  QTextDocument │
 │  源文件      │               │    AST      │             │  (渲染结果)     │
 └─────────────┘               └────────────┘             └────────────────┘
       ▲                                                         │
       │                     同步引擎                            │
       │              (位置映射 +                               │
       │               行内切换)                                │
       └────────────────────────────────────────────────────────┘
```

编辑器维护两套并行表示——原始 Markdown 源文本和渲染后的 `QTextDocument`。**行内切换管理器（InlineToggleManager）** 在光标移动时隐藏/显示 Markdown 语法（如 `**粗体**` ↔ **粗体**），实现 Typora 般的编辑体验。原始源文本始终是唯一真实来源，渲染视图由其派生。

---

## 项目结构

```
Muffin/
├── CMakeLists.txt            # 顶层 CMake 配置
├── CMakePresets.json         # 开发 / 发布预设
├── conanfile.py              # Conan 2 依赖声明
├── src/
│   ├── main.cpp
│   ├── app/                  # 应用程序、主窗口、标签页
│   ├── core/                 # 文档模型、文件管理、撤销管理
│   ├── parser/               # cmark-gfm C++ 封装（AstNode、AstTree）
│   ├── renderer/             # AST → QTextDocument（块级/行内/表格渲染）
│   ├── editor/               # MarkdownEditor、行内切换管理、输入处理
│   ├── sync/                 # 同步引擎、位置映射
│   ├── theme/                # 主题管理、CSS 加载
│   └── settings/             # QSettings 封装、快捷键配置
├── resources/
│   ├── themes/               # default_light.css、default_dark.css
│   └── resources.qrc
└── tests/                    # Qt Test 单元测试
```

---

## 构建

### 环境要求

| 依赖 | 版本 |
|------|------|
| CMake | >= 3.24 |
| C++ 编译器 | MSVC 2022、GCC 11+ 或 Clang 14+ |
| Qt | 6.5+（Widgets、Gui、Svg） |
| Conan | 2.x（可选） |

### 方式一：使用系统 Qt

确保 `Qt6_DIR` 已设置（如 `C:\Qt\6.8.3\msvc2022_64\lib\cmake\Qt6`）。

```bash
cmake --preset dev
cmake --build build/dev --config Debug
```

### 方式二：使用 Conan（自动安装 Qt）

```bash
conan install . --build=missing -s compiler.cppstd=17 -of build/conan
cmake --preset dev-conan
cmake --build build/dev --config Debug
```

> **注意：** 首次 Conan 构建会从源码编译 Qt（约 15–30 分钟），后续构建将使用缓存。

### 运行

```bash
./build/dev/muffin        # Linux / macOS
./build/dev/Debug/muffin  # Windows (MSVC)
```

### 运行测试

```bash
ctest --test-dir build/dev --output-on-failure
```

---

## 开发路线图

| 阶段 | 内容 | 状态 |
|:----:|------|:----:|
| 1 | 项目骨架、构建系统、基本窗口（打开/保存） | 已完成 |
| 2 | Markdown 解析 & 只读渲染预览 | 进行中 |
| 3 | 所见即所得行内切换（核心创新） | |
| 4 | 表格、图片、数学公式、主题、导出 | |
| 5 | 完善 — 快捷键、设置、国际化、打包 | |

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
