# Windows 便携模式、卸载设置与发行产物复核

## 便携包

Windows `.7z` 发行包通过可执行文件旁的 `Muffin.portable` 标记启用便携模式。直接解压后运行，不需要安装；已有安装版继续使用原有注册表设置和应用数据目录。

便携版的应用自有持久数据统一存放在同目录的 `Data` 中：

| 内容 | 相对路径 |
| --- | --- |
| 偏好设置、快捷键、会话、最近文件、导出预设 | `Data/settings/Muffin/Muffin.ini` |
| 未保存文档的恢复草稿 | `Data/drafts` |
| 用户主题 | `Data/themes` |
| 未命名文档粘贴的图片 | `Data/muffin-user-images` |
| 链接更新和文件夹替换备份 | `Data` 下各操作的专用目录 |
| 更新准备、图片下载等应用临时文件 | `Data/temp` |

移动便携版时，将程序、`Muffin.portable` 与 `Data` 一起移动。用户明确选择的文档、图片和导出目标仍使用其原有位置；这些外部文件和用户配置的绝对路径不会自动迁移。Windows 自身的最近使用记录等操作系统记录不属于此数据目录。

便携版不修改资源管理器菜单和文件关联，也不会复用安装版的标签页实例。只读目录无法建立可写的 `Data` 时，启动会明确报错，不会退回注册表保存个人设置。便携版通过下载并解压新包更新，保留原来的 `Data`。

开发时生成便携包：

```powershell
cmake --build --preset conan-release
cmake --build --preset conan-release --target portable
```

输出位于 `build/portable`。该目标是构建暂存目录，不宜直接长期写作：如果已有 `Data`，再次构建会拒绝覆盖，必须先保留并移走个人数据。常规 `dist` 目标也拒绝清空包含便携标记或 `Data` 的目录。

## 卸载时清除偏好设置

Windows 安装程序的卸载确认页增加“清除当前用户的个人设置”，默认不勾选。从系统“已安装的应用”进入独立卸载时也会显示此选项。

只有用户明确选择、独立卸载成功后才清除 `HKCU\Software\Muffin\Muffin`。升级中的旧版本移除、修复、取消、失败均不会触发清理。应用仍在运行时保留设置并显示提醒，防止运行中的窗口再次把设置写回。

此选项清理偏好设置和其中的历史、会话记录，保留文档、恢复草稿、图片、主题文件及操作备份。裸 MSI 卸载仍保留个人设置。静默运行安装程序时可显式选择：

```powershell
Muffin-Setup.exe /uninstall /quiet MuffinRemoveSettings=1
```

该选择不会成为下一次安装或升级的默认值。

## 签名现状

正式 Windows Authenticode 代码签名按当前决定暂缓，发行程序仍可能显示“未知发布者”。现有更新清单使用的 ECDSA 签名继续保留，用于校验更新清单和安装包哈希；它不提供 Windows 发布者身份认证。

## 体积

使用以下脚本统计解压后的文件和 PE 节，避免仅以安装包压缩大小判断原因：

```powershell
python scripts/analyze_distribution_size.py build/dist --output build/windows-distribution-size.json
```

调整前，本机 x64 Release 分发目录为 **159,444,502 字节（152.06 MiB）**。主要组成是 `MuffinUi.dll` 67.72 MiB、`MuffinCore.dll` 30.92 MiB、`Muffin.exe` 13.59 MiB，其余主要为 Qt。前两个库的大量内容位于只读数据节，其中包含语法和 Unicode 等数据；不能把整个只读节都当作可删除的资源。

Windows 安装包和便携包继续同时提供 **OpenSSL 与 Schannel** TLS 后端，由 Qt 按默认机制选择可用后端。本次保留网络兼容性，不通过移除 OpenSSL 插件缩减体积。发布流程检查两个后端均存在，并验证 Qt 能选择可用后端；可额外进行实际 HTTPS 探测：

```powershell
build/Release/MuffinWindowsTlsTest.exe --plugins build/dist --url https://github.com --url https://cnb.cool
```

探测保持正常的证书校验。CI 的基础测试不依赖外网服务可用性。后续若要进一步大幅缩减，需要单独评估语法数据、Unicode 数据和资源组织，并验收语言、字体与渲染行为。

本机复核（2026-11-04）：保留两个 TLS 后端的分发目录为 **159,451,158 字节（152.06 MiB）**。从该目录加载插件后，Qt 默认选择机制及访问 `github.com`、`cnb.cool` 的实际 HTTPS 探测均通过。手动运行上述测试前，可将 `build/dist` 加入当前进程的 `PATH`，使测试程序也使用分发包中的 Qt DLL。

## 本次验证记录

- Release 构建、MSI 和 Burn 安装包构建通过；`dist` 与便携暂存目录的主程序哈希均与 Release 一致。
- 首轮完整 358 项测试中，357 项通过；剩余语言翻译在测试启动后完成，导致翻译资源检查失败。重新生成并嵌入资源后，该项通过；包含新增用例在内的相关 12 项复测全部通过。
- 测试覆盖便携 INI 与恢复草稿存储、不可写目录拒绝启动、安装版注册表隔离、未命名文档图片、备份目录保护，以及卸载清理的成功、失败、取消和升级边界。
- 将便携目录复制到新的临时位置后，编辑器和 `muffin-mmdc` 的实际启动检查通过。测试个人数据仅生成于隔离目录。
- 对新 MSI 进行了只读查询和功能选择核对，128 种默认选择组合及 4 种显式选择均通过；对真实 Burn 包验证了无效更新入口在执行安装操作前被拒绝。未在日常使用环境执行真实卸载。
- OpenSSL 保留调整后的 Release 构建和相关 4 项复测通过；分发包明确同时包含 OpenSSL 与 Schannel，由 Qt 默认选择可用后端。
