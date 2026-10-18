# Linux deb 生命周期验收（2026-10-18）

对应 #180；源码基线、逐文件摘要与环境见 `provenance.json`。意识流 #175 暂缓，不计完成。

实际产物：`fcitx5-rimes_1.1.5_arm64.deb`，SHA-256：
`e04ce6b4ba33f1ad315618a75d2308daffd3f783dd2440e18ac6ff225bfcefaa`。
这是本地开发验收包，没有公开发布；安装后占用 117624 KiB。

- `package-build.log`：干净 Release 构建，90/90 CTest、真实 Rime/并击/统计/练习 Fcitx、GTK 输入区与安装清单预检通过。
- `lifecycle.log`：确切包新装、移除、安装 `1.1.5~lifecycle.1` 夹具、真实 dpkg 升级、重装和最终移除通过。旧包独有文件在升级后消失；每步 32 个 XSIME/CLI 样例文件的内容、目录、所有者和权限不变。root 容器运行时，保留样例属于非 root 账号。新装和升级后分别验证安装后的 ELF、固定资源、设置页、Mailbox PTY 渲染和真实进程/通知交互。
- `source-tests.log`：22 项源安装、卸载、版本、历史 deb 选择与完整性检查通过；`release-tests.log`：5 项主线渠道与 23 项产品版本检查通过。
- `previous-release-selection.json` 和 `previous-asset-verification.log`：实际读取 28 个 GitHub Release；本次 1.1.5 的 amd64 历史候选为 v1.1.4，已下载核验摘要、包名、版本与架构。arm64 没有历史候选，因此本地升级明确使用夹具，不能宣称旧版应用数据迁移已验证。真实 amd64 历史升级由 CI 另行记录。

发布流程继续使用同一项目 v* Release，包含所有 Linux 模块路径的演练。公开 Release 必须等待本次包检查并附加相同字节的 deb/摘要；本记录没有触发发布。#180 保持开放，#179 的 GNOME/KDE、硬件录屏、多屏/睡眠/锁屏及像素验收尚未完成。
