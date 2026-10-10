# 分数字号后端实验

应用已接入统一文本布局接口，默认仍使用现有 Qt 后端；分数字号后端可显式启用，尚未设为生产默认。

## 实验方法

`MuffinFontBackendProbe` 使用应用共同的 `configureCssFont()`，对同一个字体文件测试 16、18.4、28.8、29、32.4px，普通／粗体、正体／斜体，以及可选连字开关。以 96、768、6144 DPI 的排版设备分别提供 1、8、64 倍精度；文字塑形、换行、光标和命中测试均来自同一个 `QTextLayout`，绘制时按相同倍率还原，避免只缩放绘制而保留旧光标坐标。

输出包括字体文件 SHA-256、Qt 版本、平台插件、实际 glyph run 的字体／字号／样式、每行范围、光标坐标、命中结果和 PNG。浏览器脚本验证相同字体 SHA，测量同一段中英文混排文本；数据与图片只写入 `build`。

## 当前观察

Windows 原生平台插件、Qt 6.11.1、Chromium 148，LXGW WenKai Regular 原始字体，28.8px，字距 0.7px、词距 0.3px、240px 行宽（offscreen 对照结果基本相同）：

| 设备精度 | 实际 glyph 字号 | 与浏览器最大水平光标差 | 换行 | 光标命中 |
| --- | ---: | ---: | --- | --- |
| 1 | 29px | 1.0781px | 一致 | 一致 |
| 8 | 28.75px | 0.9277px | 一致 | 一致 |
| 64 | 28.796875px | 0.7329px | 一致 | 一致 |

在这组 28.8px 文本中，合成粗体和合成斜体没有改变以上 advance 结果，字形绘制仍保持对应样式。LXGW WenKai 覆盖全部中文字符；改用相同的 Open Sans Regular 字节后，Qt glyph run 记录中文回退到 **SimSun**，Chromium 的平台字体查询记录回退到 **Noto Sans SC**。这证明只固定拉丁主字体仍不足以保证中英文一致，后续需共同指定并验证中文回退字体。

可选连字开关在这组字体／后端上没有改变测量结果，不据此修改公共字体配置。全量实验中，64 倍精度下仍有部分斜体组合换行不同，所有记录的光标命中仍然一致；因此当前方案尚不能作为全局字体后端直接启用。

高精度设备验证了保留分数字号的可行性，但仍有塑形、字距取整、行高度等残差。字体更换时还要核对实际 glyph run，尤其是拉丁字体缺少中文时的回退。当前结果只适用于上述字体与 Windows 后端，不构成跨平台精度或性能保证。

## 复现

先按仓库 Release preset 构建并刷新 `dist`，再构建诊断目标：

```powershell
cmake --build --preset conan-release --target MuffinFontBackendProbe
$env:PATH = "$PWD/build/dist;$env:PATH"
$env:QT_PLUGIN_PATH = "$PWD/build/dist"
$fontPath = 'C:/path/to/LXGWWenKai-Regular.ttf'
./build/Release/MuffinFontBackendProbe.exe $fontPath build/theme-audit/font-backend/lxgw
$env:CHROME_EXECUTABLE = 'C:/Program Files/Google/Chrome/Application/chrome.exe'
node scripts/probe_font_backend.mjs $fontPath build/theme-audit/font-backend/lxgw
```

脚本需要 Playwright；也可用 `MUFFIN_PLAYWRIGHT_MODULE` 指向现有安装。诊断程序默认 `offscreen`，可在启动前设置 `QT_QPA_PLATFORM=windows` 对照应用平台，Linux/macOS 同理。再用 `build/theme-fonts/github/open-sans-v17-latin-ext_latin-regular.ttf` 复测中英文混排，以检查真实回退字体；回退结果还依赖系统安装的字体。

## 统一接口与实验开关

`render/TextLayout` 统一行坐标、基线、内在尺寸、绘制、命中、光标与选区；
`TextFontMetrics` 使用相同的排版设备。段落、标题、HTML、代码块、生成内容和
行内组件均经过该入口。代码块的光标与选区读取保存的塑形行，不再重新测量
字符串前缀。CSS 字体别名、通用字体和回退列表也由 Markdown 与 HTML 共用。

实验后端采用 64 倍精度的排版设备，所有外部几何仍为逻辑像素；像素字号、
制表位、绘制变换和内在尺寸克隆均在接口内处理。排版设备共享且不分配大画布。
后端选择在进程启动后固定，因此缓存不会在旧、新度量之间混用。现有原生后端
保留，默认行为仍可直接与 `QTextLayout` 逐行及逐像素对照。

```powershell
$env:MUFFIN_TEXT_LAYOUT_BACKEND = 'fractional'
./build/dist/Muffin.exe
```

关闭实验时删除此环境变量并重新启动应用。该开关不修改用户设置。

`MuffinTextLayoutTest` 与 `MuffinFractionalTextLayoutTest` 分别验证两种配置。
浏览器夹具固定 Open Sans、PT Serif、Latin Modern Roman、文楷及 Noto Sans SC 子集的实际字节，
检查字号、中文实际 glyph run、光标坐标、行范围与内在尺寸，同时验证窄窗口、
缩放、生成内容、HTML、代码块，以及编辑后完整／惰性布局的几何一致性。
字体子集只用于测试，TTF 仅生成在 build 内，不增加发布包字体。

斜体宽行夹具用于隔离塑形度量；窄行合成斜体仍可能与 Chromium 不同，继续
保留诊断。没有通过任意容差或更换字体把它当作已修复。生产默认的切换还需
各平台 CI 和真实机器上的字体栅格缓存、长文档测量结果支持。

新的 96 个固定字体夹具在 Windows／Qt 6.11.1／Chromium 153.0.8010.54 上，
原生后端有 15 个换行差异，实验后端本次没有换行差异。文楷 28.8px 正体、
240px 行宽的最大水平光标误差为原生 1.078125px、实验 0.733154px；长斜体
仍有约 2.018066px 的累计差异。这不覆盖之前 Chromium 148 的所有原始字体
组合，也不构成斜体换行已在各平台修复的结论。

LaTeX 场景使用主题首选的 Latin Modern Roman 10 的四个真实字形，而非用 PT Serif
代替。字号包含 9.5pt 正文、1.5／1.9em 标题，以及 28.8px 的共同对照；中文仍使用
相同 Noto 子集。这些夹具隔离字体度量，不代替完整主题 CSS 的截图验收。

产品字体策略保持主题声明顺序，统一处理别名、通用字体与平台回退尾部。
相同中文回退字体下的对照已固定；跨机器的最终字体仍取决于主题提供的字体
与系统安装字体。应用运行中注册字体会刷新共同的字体列表与度量资源版本。

运行可选性能测量：

```powershell
$env:MUFFIN_TEXT_BENCH = '1'
ctest --preset conan-release -R '^(MuffinTextLayoutTest|MuffinFractionalTextLayoutTest)$' -V
```

输出分别记录 1500 个保留布局的排版、冷／热绘制、进程内存，以及 3000 块
文档的首次布局和局部编辑耗时。Windows 内存是当前工作集；Linux/macOS 的
公共测量入口返回进程历史峰值，不能把它解释为精确的阶段增量或 glyph cache
单独占用。冷／热绘制的工作集变化用于观察缓存影响，不代表对 Qt 私有缓存
进行了直接计数。

## 本机性能对照

Windows／Qt 6.11.1、Release、offscreen，两个后端分别启动独立进程，连续测量三次。
下表为中位数；首次绘制表示这批保留布局的第一次绘制，进程此前已执行字体回归，
并非操作系统或 Qt 全局字体缓存完全为空。

| 项目 | 原生 | 分数字号 |
| --- | ---: | ---: |
| 1500 个文本布局 | 58ms | 245ms |
| 这批布局首次绘制 | 294ms | 537ms |
| 这批布局再次绘制 | 230ms | 248ms |
| 绘制后进程工作集 | 47.19MiB | 47.50MiB |
| 3000 块文档首次排版 | 2570ms | 2650ms |
| 局部编辑 | 32ms | 35ms |
| 文档编辑后进程工作集 | 156.92MiB | 157.37MiB |

本机样本中，分数字号方案的独立塑形开销约为原生的 4.2 倍，整份文档首次排版增加约
3%，重复绘制增加约 8%。此前三次测量的首次排版增幅约为 12%；本轮原生局部编辑的
一个样本达到 70ms，说明短时计时存在抖动，应保留原始样本而非只看中位数。
工作集没有出现同量级增长，但这不能推导出其他字体、窗口
缩放或长期编辑下的缓存上限。当前保留原生默认，继续通过显式开关采集真实文档数据。

CTest 还以分数字号配置重跑源代码／输入法、代码选区、点击、标题和链接生成内容的
五个编辑回归。输入法测试固定真实拉丁、中文与假名字形，避免 headless 平台缺失系统
字体时，用占位方框误判提交前后的文字落位。

## Mermaid 接入

`TextLayout.cpp` 归入 `MuffinCore`，文档和 Mermaid 复用同一实现。FlowLabel 的换行、
格式范围、基线、装饰线和字形读取，以及普通图形标签的测量／绘制，都经过公共入口。
`TextLine::glyphRuns()` 保留字形索引、源文字索引和方向，并将位置、边界及 `QRawFont`
字号转换为逻辑像素；`QRawFont::setPixelSize(qreal)` 保留分数尺寸，准备好的字形可以
直接绘制或转为轮廓。调用方不能把高 DPI 塑形设备的坐标当成场景坐标。

原生后端保留整数字号及 `CssPixelFont` 的既有缩放方式；分数后端直接塑形 CSS 字号，
不再先取整后缩放。FlowLabel 中一次没有消费结果的重复塑形已删除。数学公式和
OpenType 专用计算继续使用原后端，只通过公共外层布局交付坐标。`FlowSceneCompare`
的整数墨迹边界属于像素对照工具，不是文档排版；普通 UI 的文字绘制也无需替换。

`MuffinMermaidTextBackendTest`／`MuffinMermaidFractionalTextBackendTest` 在两个独立进程
检查原生兼容性、字形坐标、固定 Noto 中英／阿拉伯文字体回退、长标签、合成粗体／斜体、
数学外层尺寸、80%／100%／125%／200% 缩放，以及 Flowchart、Gantt、C4、Journey、Venn、
Mindmap 在窄／宽视口下的缓存复用和完整／惰性排版。既有 Mermaid 浏览器几何／像素
参考继续约束默认后端；这些一致性测试不表示分数后端已经达到浏览器像素等价。

剩余直接 Qt 调用按用途保留：

| 位置 | 用途 |
| --- | --- |
| `render/TextLayout.cpp` | 原生后端的委托实现 |
| `math/`、`mermaid/math/` | 数学字形、OpenType 塑形和公式内部排版 |
| `mermaid/scene/FlowSceneCompare.cpp` | 浏览器像素对照中的整数墨迹边界 |
| `app/StatusBarWidget.cpp`、`PreferencesPage.cpp` | 状态栏和设置图标 |
| `editor/VirtualSourceEdit.cpp` | 固定侧栏的行号；可编辑源码正文已走 `TextLayout` |
| `editor/EditorViewPaint.cpp`、`HtmlBlockHoverController.cpp`、`TableToolbar.cpp` | 加载提示、标题级别标记、HTML 悬浮按钮和表格工具栏 |
