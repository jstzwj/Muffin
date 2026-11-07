# macOS Finder Markdown 预览

Muffin 随 macOS 应用附带 `MuffinQuickLook.appex`，使用现代
`com.apple.quicklook.preview` 扩展点。安装到“应用程序”并正常打开一次 Muffin 后，
macOS 会发现应用内的扩展。在 Finder 选中 `.md` 或 `.markdown` 文件，按空格即可请求预览。
扩展启用状态和同类型预览器的选择由 macOS 管理；安装 Muffin 不会强制替换默认编辑器，
也不会自动关闭其他应用的预览扩展。

如果仍然只看到文件图标或源码，在“系统设置 → 通用 → 登录项与扩展 → Quick Look”
检查 `Muffin Markdown Preview` 是否启用。不同 macOS 版本的入口名称可能不同。
已有其他 Markdown 预览器时，系统可能优先使用它。

## 预览范围

预览使用仓库里的 cmark-gfm 解析器和 AppKit 原生文本视图，不启动编辑器、Qt 或 WebKit。
支持中文、标题、粗斜体、删除线、行内及围栏代码、引用、有序/无序列表、任务列表、表格和网页链接。
预览只读，可选中复制、滚动；颜色跟随系统深浅色。

这一预览不是 Muffin 编辑器的完整渲染副本：数学公式和 Mermaid 不做排版/绘图，
HTML 显示为源码，图片显示替代文字。扩展没有网络权限，也不读取文档旁的图片或其他文件。
支持 UTF-8（含 BOM）和带 BOM 的 UTF-16；单文件限制 8 MiB，另有节点数和嵌套深度限制。
超限或读取失败时向 Quick Look 返回标准错误，由系统显示失败或回退界面。

## 构建与签名

macOS 上使用正常的 Conan Release 构建即可：

```sh
conan install . -pr:h profiles/macos -s build_type=Release -s compiler.cppstd=20 --build=missing
cmake --preset conan-release
cmake --build --preset conan-release
ctest --preset conan-release -R 'MuffinQuickLook.*Test' --output-on-failure
cmake --build --preset conan-release --target dmg
```

主程序、扩展和依赖统一以 macOS 13.0 为部署目标，打包时检查实际二进制。
详见[最低系统版本与发布检查](macos-deployment.zh.md)。

扩展嵌入 `Muffin.app/Contents/PlugIns/MuffinQuickLook.appex`，只静态链接 Markdown 解析器，
不复制整套编辑器依赖。扩展带 App Sandbox 和用户所选文件只读权限。
DMG 构建先部署/签名 Qt 依赖，再重新签名扩展并最后签名宿主，避免部署破坏嵌套签名。

当前使用无需证书的 ad-hoc 签名。这保证包内签名和沙盒声明一致，但不提供 Developer ID 身份或公证，
不能保证下载包在所有 Mac 上自动获准加载。发布前仍需在目标 macOS 上检查首次安装、扩展启用和 Finder 空格预览。
后续接入正式签名时，应同时替换依赖、扩展和宿主的签名流程。

CI 在 macOS 构建扩展及原生测试，并检查嵌入路径、UTI、版本、签名、实际沙盒权限以及运行库依赖。
发布流程在 `macdeployqt` 完成后对最终发布包再次检查。

开发机只读诊断：

```sh
pluginkit -m -A -D -i com.muffin-editor.Muffin.QuickLook
mdls -name kMDItemContentType /path/to/note.md
python scripts/check_macos_quicklook.py --bundle build/Release/dist/Muffin.app
```

参考：[Apple Quick Look UI](https://developer.apple.com/documentation/quicklookui)
与 [QLPreviewingController](https://developer.apple.com/documentation/quicklookui/qlpreviewingcontroller)。
