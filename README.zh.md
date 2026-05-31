# Muffin

[English](README.md)

Muffin 是一款快速、轻量的原生 Markdown 编辑器，使用 C++20 和 Qt 6 Widgets 构建。

项目目标是探索类似 Typora 的单面板编辑模型。Markdown 文本会被解析成文档 AST，界面渲染的是 AST 的可编辑投影，保存和导出最终都应该从文档模型序列化回 Markdown。Muffin 不打算做成左右分栏预览器。

## 当前状态

Muffin 仍处于早期开发阶段。

- M0/M2 应用外壳已经可用：原生 Qt 菜单栏、源码模式、打开/保存/另存为、最近文件、文档属性、状态栏、命令行打开文件、发布目录打包。
- M3 已开始：已有只读 AST 渲染视图，可以通过 `视图 -> 源代码模式` 切换回源码编辑器。
- `third_party/cmark-gfm` 已作为嵌入库精简引入。
- 已在 cmark-gfm 之上加入 Typora 风格数学公式扩展，包含行内公式和块级公式 AST 节点。
- 真正的 WYSIWYG 编辑、块选择、AST 事务撤销/重做、表格编辑控件、图片处理、导出流程和原生公式渲染仍在计划中。

## 技术选型

| 领域 | 选择 |
| --- | --- |
| UI | Qt 6 Widgets |
| 语言 | C++20 |
| 依赖管理 | Conan |
| Markdown 解析器 | vendored `cmark-gfm` |
| Markdown 扩展 | GFM 表格、删除线、自动链接、任务列表、标签过滤、Muffin 数学公式 |
| 打包 | CMake `dist` 目标，复制 Qt DLL 和插件 |

Muffin 选择 Qt Widgets 而不是 QML，因为它更适合原生菜单、复杂文本输入、桌面对话框、打印和高性能文档编辑。

## 构建

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

当前 Conan 配置使用动态 Qt：

```ini
qt/*:shared=True
qt/*:widgets=True
qt/*:gui=True
qt/*:qtdeclarative=False
qt/*:with_odbc=False
qt/*:with_pq=False
```

## 仓库结构

```text
src/app/        主窗口、文档会话、命令注册
src/document/   Markdown 文档模型和 AST 节点类型
src/editor/     基于 QPlainTextEdit 的临时源码编辑器
src/io/         文件打开和保存控制器
src/parser/     cmark-gfm 适配器和 Markdown 序列化器
src/render/     只读 AST 渲染视图
tests/parser/   解析器冒烟测试
docs/           设计文档和菜单参考
third_party/    精简后的嵌入式 cmark-gfm 源码
```

## Markdown 功能样例

这一节故意覆盖较多 Markdown 语法，方便 Muffin 打开自己的 README 来测试解析和渲染效果。

### 标题

# 一级标题样例
## 二级标题样例
### 三级标题样例
#### 四级标题样例
##### 五级标题样例
###### 六级标题样例

### 段落和换行

Markdown 段落由空行分隔。普通换行通常仍属于同一个段落。
这一行跟在一个软换行之后。

行尾两个空格会生成硬换行。  
这一行从硬换行之后开始。

### 强调

普通文本可以包含 *斜体*、_另一种斜体_、**粗体**、__另一种粗体__、***粗斜体***、~~删除线~~、`行内代码`，也可以包含行内 HTML，例如 <kbd>Ctrl</kbd> + <kbd>S</kbd>。

### 转义和实体

被转义的标点会保持字面量：\*不是斜体\*，\`不是代码\`，\[不是链接\]。

实体会由渲染器解码：&amp; &lt; &gt; &copy;。

### 引用

> 引用块可以包含段落。
>
> 也可以包含 **格式化文本**、`代码` 和嵌套引用。
>
> > 嵌套引用。

### 列表

无序列表：

- 第一项
- 第二项，包含 **粗体**
- 第三项
  - 嵌套项
  - 另一个嵌套项

有序列表：

1. 第一步
2. 第二步
3. 第三步
   1. 嵌套步骤
   2. 另一个嵌套步骤

任务列表：

- [x] 解析 CommonMark 块结构
- [x] 解析 GFM 表格和任务标记
- [x] 解析行内公式和块级公式
- [ ] 实现可编辑 AST 投影
- [ ] 实现导出流程

### 链接和图片

行内链接：[GitHub cmark-gfm](https://github.com/github/cmark-gfm)

引用链接：[Qt][qt-link]

自动链接：https://www.qt.io

邮箱自动链接：<hello@example.com>

图片语法：

![Muffin 占位图](docs/muffin-placeholder.png "Muffin")

[qt-link]: https://www.qt.io

### 代码

行内代码：`cmark_node_get_type(node)`。

缩进代码：

    conan install . -s build_type=Release -s compiler.cppstd=17
    cmake --build --preset conan-release

围栏代码：

```cpp
#include <QApplication>

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  return app.exec();
}
```

```powershell
cmake --build --preset conan-release --target dist
.\build\dist\Muffin.exe README.zh.md
```

### 表格

| 功能 | 状态 | 备注 |
| :--- | :---: | ---: |
| 段落 | 已完成 | CommonMark |
| 表格 | 已完成 | GFM |
| 数学公式 | 进行中 | Muffin 扩展 |
| WYSIWYG 编辑 | 计划中 | AST 投影 |

### 分割线

---

***

___

### 数学公式

行内公式使用单美元符号：$E = mc^2$ 和 $a_1 + b_1 = c_1$。

块级公式使用双美元围栏：

$$
\int_0^1 x^2\,dx = \frac{1}{3}
$$

另一个公式块：

$$
\begin{aligned}
a^2 + b^2 &= c^2 \\
e^{i\pi} + 1 &= 0
\end{aligned}
$$

### HTML

<details>
<summary>HTML 块样例</summary>

Markdown 渲染器通常允许原始 HTML 块。Muffin 当前会把它作为 Markdown 输入的一部分解析，HTML 行为会在渲染器阶段继续细化。

</details>

### 尚未完整支持或计划中的 Markdown 相邻特性

下面这些语法适合用作兼容性测试，但完整编辑和渲染支持尚未完成：

- 脚注：`一个脚注引用[^sample-note]`
- 定义列表
- Mermaid 图
- Front matter 编辑器
- 嵌入式富媒体

[^sample-note]: 脚注语法目前作为兼容性样例保留。

## 路线图

1. 提升只读 AST 渲染质量。
2. 增加块级命中测试和源码范围映射。
3. 将选中的渲染块切换为局部源码编辑控件。
4. 增加 AST 事务、撤销/重做分组和保留格式的序列化编辑。
5. 实现表格、数学公式、图片、导出和主题流程。

