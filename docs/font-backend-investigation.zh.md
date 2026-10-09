# 分数字号后端实验

本轮先完成伪元素入口收敛，再验证字体后端方案。应用仍使用现有 Qt 排版设备，尚未切换到实验后端。

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

下一步应先把高精度设备封装成统一的文本布局接口，集中处理行坐标、绘制变换、命中测试、选区和内在尺寸，再替换实际编辑路径。接入前需要验证所有文字入口、缩放与资源缓存，并测量长文档开销。
