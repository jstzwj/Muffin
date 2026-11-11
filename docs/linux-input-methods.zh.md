# Linux 输入法与发布包

Muffin 的 Linux 通用包携带自己的 Qt。Fcitx 的 Qt 输入法插件使用 Qt 私有接口，必须与应用使用同一份 Qt 构建；系统安装的插件即使同为 Qt 6，也可能因小版本或补丁版本不同而无法加载。不能把系统 Qt 库或系统输入法插件直接复制到发布包里替换。

## 构建与打包

- `conanfile.py` 在 Linux 开启 Qt 的 DBus 支持，让 Qt 自带的 IBus 插件和 Fcitx 插件可以连接桌面的会话总线。
- `cmake/MuffinLinuxInputMethods.cmake` 下载官方 fcitx5-qt **5.1.16** 源码，固定 SHA-256，用 Muffin 已选中的 Qt 公共和私有目标构建官方 `BUILD_ONLY_PLUGIN` 目标。不会重新寻找系统 Qt，不会改写 Conan 缓存，也不构建输入法服务或词库。
- `cmake/MuffinDeploy.cmake` 收集 Qt 的 `platforminputcontexts`，加入刚构建的 Fcitx 插件、许可证和动态依赖，修正相对 RPATH。Fcitx 插件或许可证缺失时拒绝生成发布包。
- 依赖必须从可执行文件和插件的原始位置解析，再复制到发布目录并重写 RPATH。Qt 插件原有的 `$ORIGIN/../../lib` 在复制后会失效，不能先搬动再解析；否则 CMake 会优先选中系统的同名 Qt 库。打包时也检查所有 Qt 依赖来自应用所使用的同一个库目录，拒绝混用系统 Qt。
- `src/app/BundledQtPlugins.h` 在构造 QApplication 之前读取 `qt.conf` 中的 `Muffin/SelfContainedPlugins` 标记，将该进程的 Qt 插件搜索范围设为包目录，避免桌面 `QT_PLUGIN_PATH` 注入另一版本的 Qt 插件。即使插件缺失，也不回退加载系统插件。开发构建保持原有路径。

额外的 Linux 构建依赖为 `extra-cmake-modules`、`libxkbcommon-dev`，部署需要 `patchelf`。其余 X11 开发依赖沿用工作流。插件源码的地址和校验值在 CMake 文件中；两个上游许可证随包放入 `licenses/fcitx5-qt/`。

应用不设置 `QT_IM_MODULE`、`QT_IM_MODULES`，不改动用户的全局环境。桌面继续决定使用 Fcitx 或 IBus。输入法服务、中文引擎、词库及会话配置由系统提供。

## 支持范围

当前通用包使用 Qt 的 **xcb/X11** 后端，Wayland 桌面通过 **XWayland** 运行。这次修复覆盖这条发布路径。原生 Wayland 的 QtWayland 后端及 Fcitx Wayland 适配尚未启用，不能据此宣称已经验证原生 Wayland 输入与候选框定位。

## 发布检查

CI 和 Release 都在干净的 D-Bus 会话与 Xvfb 中运行：

```sh
cmake --build --preset conan-release --target dist MuffinLinuxInputMethodProbe
dbus-run-session -- xvfb-run -a python scripts/check_linux_input_methods.py \
  --dist build/Release/dist --probe build/Release/MuffinLinuxInputMethodProbe
```

需要安装 `dbus-x11`、`xvfb`、`xdotool`、`fcitx5` 和 `fcitx5-chinese-addons`。检查会：

1. 将整个包复制到临时目录，清除 Conan 的库路径环境，以相对 RPATH 加载。
2. 对所有二进制运行 `ldd -r`，拒绝未解析符号、包外 Qt 库及构建目录 RPATH。
3. 故意传入系统 `QT_PLUGIN_PATH`，验证隔离；用立即解析符号的 QPluginLoader 加载 Fcitx、IBus 插件，并模拟插件缺失，验证隔离仍然有效。
4. 启动真实 Fcitx 服务，通过原生 X11 键盘事件输入 `nihao` 并提交，分别验证源码与所见即所得编辑器收到预编辑与“你好”，文档更新且标记为修改，光标矩形有效。

不会将探针、输入法服务、测试配置或测试词库放入发布包。Wayland 原生候选框、多显示器缩放及不同发行版桌面仍需在相应环境实测。
