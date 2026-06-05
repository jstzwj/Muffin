# Muffin

[English](README.md)

Muffin 是一款快速、轻量的 Markdown 编辑器，专注于简洁的写作界面和快速响应的编辑体验。

编辑器以 Markdown 文件作为真实来源，同时把文档呈现为可直接编辑的页面。它适合希望获得渲染文档可读性，又不想放弃纯 Markdown 文件的用户。

## 功能亮点

- 简洁的单面板编辑体验。
- 源码模式和渲染编辑模式。
- 快速的本地 Markdown 解析和文档刷新。
- 新建、打开、保存、另存为、最近文件、文档属性和文件夹浏览等文件流程。
- 可编辑的表格、代码块、数学块和 HTML 块。
- 文档大纲和文件树侧边栏。
- 主题支持以及英文/简体中文 UI 资源。

Markdown 语法压力样例见 [example.md](example.md)。

## 构建和测试

本仓库使用 Conan 生成的 CMake presets。推荐的本地验证流程是：

```powershell
conan profile detect --force
conan install . -s build_type=Release -s compiler.cppstd=17 --build=missing
cmake --preset conan-default
cmake --build --preset conan-release
ctest --preset conan-release --output-on-failure
cmake --build --preset conan-release --target dist
```

发布目录：

```text
build/dist/Muffin.exe
```

`cmake --build --preset conan-release` 会更新 `build/Release/Muffin.exe`；需要刷新可分发应用包时，请再运行 `dist` 目标。

## 翻译流程

Muffin 当前包含英文和简体中文 UI 资源。

```powershell
cmake --build --preset conan-release --target update_translations
cmake --build --preset conan-release --target release_translations
```

如果 Qt Linguist 工具不在 `PATH` 中，可以将 `MUFFIN_QT_LINGUIST_BIN` 配置为包含 `lupdate` 和 `lrelease` 的目录。

## 路线图

1. 打磨渲染编辑面，补齐选区、光标、IME 和局部刷新细节。
2. 扩展标题、段落、列表、引用、Front Matter 和任务列表等块级变换命令。
3. 完善表格、代码块、数学块、HTML 块和图片等复杂块体验。
4. 增加查找替换、更完整的剪贴板行为、导出流程、打印和诊断能力。
5. 强化大 Markdown 文档的性能测试和 roundtrip 测试。
