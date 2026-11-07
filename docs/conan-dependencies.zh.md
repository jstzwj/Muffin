# 可复现的 Conan 依赖

根目录 `conan.lock` 提交到版本库。它同时覆盖 Windows、Linux 和 macOS 的
运行依赖与构建工具，锁定版本及配方修订，不锁死不同架构的二进制包 ID。
正常的 `conan install .` 自动读取此文件；CI 与发布还显式传入
`--lockfile=conan.lock`，缺少文件时直接失败。

这是针对 CI 运行 42357499976 的修复：全新 Linux/macOS 缓存下载了 Qt 6.11.1
的新配方 `b4b7bf63674c82b58b825ffca311f656`，要求 Brotli ≥1.1.1；
FreeType 2.13.2 则要求 Brotli 1.1.0，解析阶段发生冲突。Windows 命中旧配方缓存，
同一次提交表现不同。固定顶层版本号不足以避免这种配方修订漂移。

当前锁定 Qt 配方 `7bcb4a48a83cd0a3353ceb431394ff8b` 和 Brotli 1.1.0，
保留已验证的 FreeType、HarfBuzz 和 PCRE2 版本。解决方法是统一依赖快照，
不通过关闭字体功能或强行覆盖 Brotli 版本绕过冲突。新增的 Linux 专有依赖
也纳入锁定。缓存键包含 `conanfile.py`、`conan.lock` 和 profile 文件的摘要；
回退缓存只提供已有包，不能覆盖锁文件选择的配方。

## 有意升级依赖

在对应平台生成候选锁文件：

```sh
conan lock create . --lockfile="" --update -s build_type=Release -s compiler.cppstd=20 --lockfile-out=build/candidate.lock
```

macOS 加 `-pr:h profiles/macos`；Linux 使用 `-s compiler.cppstd=gnu20`。
各平台的候选锁文件用 Conan 的 `conan lock merge` 合并，再用最终快照分别
验证 Windows x64/ARM64、Linux、ASan 和 macOS。检查同一依赖是否出现互相
冲突的配方修订，以及字体、布局和 TLS 的行为，再提交依赖文件与锁文件。
普通构建不运行 `--update`，不使用 `--lockfile-partial` 静默接纳新依赖。

历史标签重建优先保留标签自己的锁文件。没有锁文件的旧标签使用发布工具的
已审核快照；如果旧标签的需求不在该快照内，构建失败，需要单独审核兼容快照，
不会悄悄升级到当日最新版。
