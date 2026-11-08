# ICU 数据外置

Muffin 随包发布与 Conan ICU 相匹配的完整数据文件，不依赖系统 ICU，不设置或修改进程的 `ICU_DATA` 环境变量。

## 文件位置

- Windows、Linux、便携版：程序旁的 `resources/icu/icudt74l.dat`。
- macOS 应用包：`Muffin.app/Contents/Resources/icu/icudt74l.dat`。
- 开发和测试：相应构建配置的输出目录下 `resources/icu/`。

文件名随 ICU 主版本和目标字节序生成。数据文件是发行资源，不属于便携版的个人 `Data` 目录。

## 构建与部署

ICU 代码库继续由 Conan 管理，数据直接使用 ICU 官方发布的同版本预构建 common-format 数据包。CMake FetchContent 下载 `icu4c-74_2-data-bin-l.zip`，大端目标使用 `-b.zip`；固定下载地址和 SHA-256，解包后再次校验 `.dat`，不再解析 COFF、ELF 或 Mach-O 静态库。

官方 Release 下载地址暂时不可用时，构建会尝试同一附件的 GitHub 资产下载接口；两条地址必须通过同一个固定 SHA-256 校验，均验证 TLS，并设置总超时和无数据超时。备用接口只用于构建，不参与客户端检查更新。手动重建历史标签时，发布流程可从选定工作流版本覆盖这份下载配置，保留标签中的应用源码不变。

数据版本必须与 Conan ICU 头文件中的完整版本一致，否则配置阶段报错，要求同步更新数据包。小端数据在 Windows、Linux、macOS 的 x64 和 ARM64 之间通用，不需要运行目标平台的工具。

MuffinUi 使用同版本 ICU 官方的空数据桩，防止链接器再把完整数据数组嵌入二进制。Conan 依赖仍由原来的 ICU 组件管理，无需修改 Conan 缓存或生成的依赖文件。

首次 CMake 配置会获取数据，后续配置复用构建目录中的 FetchContent 缓存。离线构建可通过 `-DFETCHCONTENT_SOURCE_DIR_MUFFIN_ICU_DATA=<已解包的官方数据目录>` 指定本地副本；该目录中的 `.dat` 同样必须通过固定 SHA-256 校验。应用运行时不下载数据。

构建会自动生成大小、SHA-256 配置。`dist`、Windows 便携包、MSI 和 macOS 应用包包含该数据。MSI 缺少数据时拒绝打包；更新与卸载由正常安装事务管理这个发行文件。

官方资料：[ICU Data](https://unicode-org.github.io/icu/userguide/icu_data/)、[ICU 74.2 数据包](https://github.com/unicode-org/icu/releases/tag/release-74-2)。这是官方预构建 `.dat`、官方空数据桩及 `udata_setCommonData()` 的组合；大小和 SHA-256 检查属于 Muffin 的部署完整性保护。

## 程序内初始化

应用在打开文档前调用 `initializeIcuData()`；分词、文件编码处理、拼写检查入口也会保证初始化，以覆盖测试和独立调用。

初始化使用绝对路径打开文件，校验大小和 SHA-256，再将只读内存映射传给 `udata_setCommonData()`。映射由进程内单例持有，避免 ICU 缓存指向已释放的内存。初始化线程安全，成功或失败结果只计算一次。

注册后禁止 Muffin 的 ICU 再自行搜索外部数据文件。缺失、版本不匹配或损坏会给出带路径的错误，应用提示重新安装，不会用系统数据或环境变量指定的数据代替。

macOS 继续使用系统 ICU 自己的数据；Muffin 仅向自身静态 ICU 实例注册映射。所有平台采用静态数据依赖模式，避免 Conan 的 archive 模式在运行环境中自动注入 `ICU_DATA`。

## 体积与验收

本次只改变存放方式，未裁剪功能。约 29.36 MiB 数据从 DLL 移到 `.dat`，安装总大小和压缩下载大小不保证下降。

回归覆盖实际数据的中文分词、编码转换和拼写检查，以及并发初始化、改变工作目录、缺失文件、同尺寸内容损坏、错误文件大小。失败场景在子进程隔离，检查初始化不会修改 `ICU_DATA`。

### 本机验证（Windows x64 Release）

- 切换官方数据包后，Release 完整构建通过，8 项相关回归通过，覆盖 ICU 加载、中文分词、单词导航、编码转换、便携路径与打包、构建一致性。
- 已核对官方小端 `.dat` 与此前静态库提取的数据逐字节相同。此前外置加载版本的全套 362 项中 361 项首遍通过，`MuffinDocumentSessionAsyncParseTest` 在并行运行中触发超时断言；独立运行与 CTest 单独复测均通过。此次仅改变数据来源，未重复运行全套，未修改该用例或放宽超时。
- 配置错误场景已验证：ICU 版本不匹配，以及离线数据副本被修改，都会在配置阶段拒绝继续。首次 CMake 直连下载遇到连接重置；本机使用已经下载并通过固定 SHA-256 验证的官方压缩包填充 FetchContent 下载缓存，完成解包、配置和构建。
- 新增 ICU 加载测试通过，现有中文分词、文件编码测试通过。现有本机配置不注册 `MuffinSpellCheckerTest`，本次没有执行该测试；保留 CI 中的拼写检查回归。
- `dist`、便携目录、MSI 和 Setup 重新生成；MSI 数据表确认 `.dat` 位于 `resources/icu` 并属于必装的 `MainFeature`。官方数据缓存、运行目录、`dist` 和便携目录中的数据校验值一致。
- `dist` 和临时便携副本均从其他工作目录成功执行 `--version`；子进程中故意设置无效 `ICU_DATA` 仍正常启动。便携测试仅在临时副本创建个人配置，打包目录保持干净。
- `MuffinUi.dll` 从 67.72 MiB 降为 38.37 MiB；独立数据文件 30,782,896 字节（29.36 MiB）。Setup 约 53.78 MiB，拆分没有明显降低压缩下载体积。

Linux、macOS 和 ARM64 尚未在本机实际构建运行，完整打包与运行仍需相应平台 CI 验证。
