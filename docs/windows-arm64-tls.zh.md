# Windows ARM64 的 OpenSSL 构建约束

Windows ARM64 CI 曾在打开包含远程图片的 README 时崩溃。原生转储显示，故障线程是
`QNetworkAccessManager` 的工作线程，发生在 `qopensslbackend.dll` 内静态链接的
OpenSSL `tls_parse_all_extensions`，并非文档绘制线程。

OpenSSL 的 Windows 构建默认继承 `/Gs0`。MSVC ARM64 的 `/O2 /Gs0` 组合存在代码生成
缺陷：函数保存返回地址之前调用栈探测函数，覆盖返回地址。转储中的 ARM64 指令与此一致。
微软将这个问题列在 [MSVC 14.52 RC 的修复清单](https://devblogs.microsoft.com/cppblog/msvc-build-tools-version-14-52-release-candidate-now-available/)
中；当前 CI 使用的稳定工具链尚不包含此修复。

`profiles/windows-arm64` 只对 OpenSSL 恢复标准的 `/Gs4096` 栈探测阈值，保留优化、汇编、
`/GS` 安全检查及 OpenSSL、Schannel 两个 TLS 后端。`/Gs` 控制栈探测阈值，大小写不同的
`/GS` 控制安全 cookie，两者不是同一个选项。

Conan 的编译标志配置本身不会改变 package ID，因此此 profile 还把 `tools.build:cflags`
加入 OpenSSL 的 package ID。OpenSSL 及依赖它的 Qt 将重新构建，避免恢复缓存时复用有缺陷的
旧二进制。CI 与 Release 的 Windows ARM64 构建使用同一个 profile。

在原生 Windows ARM64 环境构建：

```powershell
conan install . --lockfile=conan.lock -pr:h profiles/windows-arm64 -s build_type=Release -s compiler.cppstd=20 --build=missing
cmake --preset conan-default
cmake --build --preset conan-release
ctest --preset conan-release --output-on-failure
```

`MuffinWindowsTlsTest`、`MuffinWindowsTls-openssl` 和 `MuffinWindowsTls-schannel` 使用
本地回环服务器完成证书校验、TLS 握手及数据往返，不依赖外网。支持 TLS 1.3 的后端会同时
测试 TLS 1.2 与 TLS 1.3，确保这类缺陷可以直接在网络回归测试中暴露。
