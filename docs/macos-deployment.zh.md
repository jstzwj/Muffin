# macOS 最低系统版本与发布检查

Muffin 的 macOS ARM64 构建目标为 **macOS 13.0 及以上**，包括 Ventura 13、
Sonoma 14、Sequoia 15 和 Tahoe 26。这是构建兼容性目标；新产物仍需要在实际
系统上验收。Qt 6.11 的官方最低支持版本也是
[macOS 13](https://doc.qt.io/qt-6.11/macos.html)。

## v0.7.13 的问题

对正式发布的 `Muffin-v0.7.13-macos-arm64.tar.gz` 逐个读取 Mach-O 头，结果如下：

归档 SHA-256：`c0a5cba894bcbc8356264521a65f8c2bf6a2d0f61dd526549a6477abde020a19`。

| 文件 | 二进制最低系统版本 |
| --- | --- |
| `Muffin.app/Contents/MacOS/Muffin` | 26.0 |
| `libMuffinCore.dylib`、`libMuffinUi.dylib` | 26.0 |
| `MuffinQuickLook.appex/Contents/MacOS/MuffinQuickLook` | 26.0 |
| `muffin-mmdc` | 26.0 |
| 随包 Qt 动态库和插件 | 13.0 |

构建没有明确指定部署目标，应用自己的二进制继承了构建环境的 26.0。
主程序 plist 还使用了 `CFBundleMinimumSystemVersion=10.15`，扩展则声明
`LSMinimumSystemVersion=10.15`，均与实际二进制要求不符。

因此 Sequoia 的禁止图标和版本不兼容提示有实际依据。移除 quarantine、修改
plist 或重新签名都不能降低二进制的系统要求；必须用正确目标重新构建。
已发布的旧附件不会因为仓库修复而自动改变。

## 统一构建目标

```sh
conan profile detect --force
conan install . -pr:h profiles/macos -s build_type=Release -s compiler.cppstd=20 --build=missing
cmake --preset conan-release
cmake --build --preset conan-release
ctest --preset conan-release --output-on-failure
cmake --build --preset conan-release --target dmg
```

`profiles/macos` 继承本机编译器和架构，将 `os.version` 固定为 `13.0`，让 Conan
对整个依赖图使用相同部署目标并区分二进制缓存。没有目标版本的 macOS
Conan 配置会给出明确错误。需要更高目标的本地构建可以显式传入
`-s os.version=15.0`，但不能据此发布声称支持 13.0 的包。

CMake 在编译器初始化前提供 13.0 的默认值，保留显式部署目标；主程序和
Quick Look plist 都使用实际的 `CMAKE_OSX_DEPLOYMENT_TARGET`。CI 与发布使用
同一 profile 和独立的 `macOS-target-13.0` 缓存空间。

Mermaid 的浮点数转文本统一使用 Qt 的最短表示，再按 ECMAScript 规则选择
定点或科学计数法。Apple libc++ 的浮点 `std::to_chars` 要求 macOS 13.3，
不能用于声明支持 13.0 的代码；共享转换避免各图表单独维护兼容分支。

## 检查实际产物

```sh
python scripts/test_check_macos_deployment.py
python scripts/check_macos_deployment.py build/Release/dist --minimum-os 13.0
```

检查器同时核对主程序和扩展的 `LSMinimumSystemVersion`，读取所有随包
Mach-O 的 arm64 切片及 `LC_BUILD_VERSION` / `LC_VERSION_MIN_MACOSX`。
它覆盖应用、内部库、Qt 库、插件、Quick Look 和同目录 CLI；要求每个文件
都包含兼容的 arm64 切片，并且最低版本不高于声明。SDK 版本可以是 26，
不要求使用旧 SDK。检查器也可在 Windows/Linux 上检查已解包的 macOS 产物。

DMG 目标在部署 Qt 后、生成镜像前执行检查；发布任务在上传前再检查完整
dist。现代构建机上的启动测试继续保留，但不能代替最低系统版本检查和
旧系统实机验收。手动重建历史标签时，发布流程仅覆盖部署 profile、plist、
打包与检查工具，应用源码仍来自指定标签。
