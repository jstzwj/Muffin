# CNB 发布镜像与更新清单

Muffin 继续在 GitHub 发布完整二进制，同时把同一批产物同步到公开仓库
<https://cnb.cool/jstzwj/Muffin>。应用优先从 CNB 检查并下载更新，不再调用 GitHub Releases REST API。

GitHub 是源码、构建与正式版本发布的主入口。所有平台统一由 GitHub Actions 构建、测试及签名，
发布任务再将同一批产物上传到 CNB，CNB 负责镜像分发。自动发布直接使用 Actions 产物，在云端完成同步，
无需经过维护者的本机，也不会在 CNB 重新编译或重新签名。

## 地址与权限

- 固定更新入口：<https://cnb.cool/jstzwj/Muffin/-/releases/download/updates/muffin-update.json>
- 对应版本附件：`https://cnb.cool/jstzwj/Muffin/-/releases/download/vX.Y.Z/文件名`
- 备用更新入口：<https://github.com/jstzwj/Muffin/releases/latest/download/muffin-update.json>
- 发布凭据：GitHub Actions Secret `CNB_RELEASE_TOKEN`，仅发布脚本使用；需要对 `jstzwj/Muffin` 的 `repo-release:rw` 权限。
- 初次使用需在 CNB 初始化 `main`。本仓库的 CNB 镜像只保存发布说明和二进制，开发源码及其提交仍以 GitHub 为准；CNB 的标签不代表 GitHub 源码提交。

客户端及匿名下载不携带令牌。API 发布请求禁止跨域重定向；预签名存储上传与公开下载不发送发布凭据。
不要把令牌提交进 Git、更新清单或日志。CNB 的 Git 用户名 `cnb` 仅用于 Git 认证，不是下载所需配置。

## 发布顺序

1. GitHub Actions 中各平台构建、测试及 Windows 签名全部成功。
2. 发布脚本使用应用内置公钥验证 x64/ARM64 的 ECDSA P-256 签名，以及安装包大小、文件名和 SHA-256。
3. 从这批产物生成 GitHub 与 CNB 各自的 `muffin-update.json`。清单列出稳定版本及对应平台产物，地址限定到官方仓库。
4. GitHub 发布所有原产物，并附加 GitHub 静态清单，保留旧客户端兼容。
5. CNB 上传到版本发布；草稿可重试上传，已公开的同名附件必须与原产物一致，拒绝静默替换已发布版本文件。
6. 公开该版本，再通过匿名下载逐个核验所有产物的大小及 SHA-256。
7. 全部通过后，才覆盖 `updates` 发布中的固定清单。固定入口不会引用未上传完毕或未公开的文件。

发布任务跨标签串行执行。历史版本重建可补齐其附件，但不能把固定清单指向比当前更旧的版本。
上传中途失败可以重跑；未通过公开下载核验时，不推进更新入口。

静态发现清单通过 HTTPS 分发，其作用是发现版本与地址。它不代替安装授权：Windows 更新器仍下载并验证已有的
`.update.json` 签名清单，再下载及校验安装包；切换镜像也不会跳过签名或哈希验证。
两边复用同一批字节及原签名，迁移不更换用户已有的信任公钥。

## 客户端行为

- 优先请求 CNB 固定附件入口，失败后请求 GitHub 静态附件；均不调用平台的版本查询 API。
- 逐源保存 ETag 和已验证结构的清单。只有服务器返回 304 时才重用缓存，不把离线旧缓存报告成“已经是最新版本”。
- 清单大小限制为 128 KiB，每个源设置连接传输超时及整体超时。
- 403/429 尊重 `Retry-After`，暂时跳过对应源；自动检查连续失败后退避，最多到 24 小时。
- 手动检查失败给出可读提示，启动时检查保持静默。
- CNB 安装包或签名清单网络下载失败时，从相同 GitHub 版本附件重新下载，文件从零写入；签名或哈希错误直接拒绝安装。
- 非 Windows 安装版保持发布页下载方式，默认进入 CNB 对应版本页；GitHub 仍保留全部产物。

旧版本客户端的检查地址编译在原程序中，必须先升级到包含本次改动的版本，才能改走新入口。

## 手动补齐既有发布

安装 `requirements-release.txt`，下载同一个 GitHub 标签的完整发布附件到独立目录。
在当前进程环境中提供 `CNB_RELEASE_TOKEN`，不要通过命令行参数传递或输出该值，然后执行：

```powershell
python scripts/publish_cnb_release.py --tag vX.Y.Z --assets build/mirror-assets --output build/mirror-metadata
```

脚本可加 `--prepare-only`，仅验证签名和生成静态清单，不写外部服务。
生成的 `github/muffin-update.json` 也应作为同一个 GitHub Release 的附件上传，供备用检查入口使用。

离线发布测试：`python scripts/test_publish_cnb_release.py`。
客户端回归：`ctest --preset conan-release --output-on-failure -R MuffinWindowsUpdateTest`。

## 平台边界

无需新建 Docker/npm/Conan 制品库、开通付费预算或配置 CNB 构建流水线；使用公开 Release 附件即可。
计费以 CNB 控制台及官方政策为准，平台限流和临时下载地址的有效期不属于 Muffin 可以保证的行为。
