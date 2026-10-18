# Linux 桌面与宿主验收（2026-10-18）

对应 #179，基于 `70258f37` 加本提交修复。源码摘要在 `provenance.json`；第 25 项意识流暂缓，不计为完成。

- `hosts/results.json`：实际 GTK3、Qt5、Firefox 158.0 DOM 和 xterm 390 PTY 原生上屏通过。GTK、Qt、Firefox 的 Buffer 捕获期间无正文泄漏，Return 后提交；Firefox 密码框收到公开测试字符串，Buffer 快捷键不接管。环境为 Xvfb + Xfwm4，不能推断 GNOME/KDE 或 Wayland 宿主结果。
- `e2e.log`：引擎、真实 Fcitx 测试前端、Buffer/保护态/启动器、Capsule 五模块和进程恢复、D-Bus 普通输入/Buffer/Capsule 及 Sway layer-shell 几何通过。旧无窗口管理器 GTK/Qt 探针仍报 warning，不计通过；宿主结果以独立带 Xfwm4 的 `hosts/results.json` 为准。Weston 未安装，明确跳过。
- `shortcuts.log`：真实 X11 键盘抓取释放后唤起、按住不重复、换窗取消、停用与 Wayland portal 协议回归通过。
- `wayland-clipboard.log`：headless Sway 多 MIME、二进制/大文本、敏感复制与真实粘贴、60 秒清除、更新内容保留、保护态通过。
- `wayland-capture.log`：真实 Sway 交互 PNG 截图通过。软件 headless 环境没有 DMA-BUF，录屏被 xdpw 拒绝，失败会清除未完成文件；物理录屏仍未验证。
- `practice-private-bus.log`：隔离总线下实际 GTK/Fcitx 练习、物理键、回退、预编辑排除和结束保存通过。
- `visual-inventory.json`：19 份对照清单、229 组双端截图与差异图文件/摘要存在，全部 `requires_review`。缺失交互状态和像素差异仍须处理。

本轮修复三个产品问题：10 毫秒输入源切换等待使用明确计时精度；首次加载所有插件后按 macOS 默认选择 Capsule 词条页（停用时保留可用页）；X11 快捷键待主键释放及原窗口焦点确认后才申请新输入目标。保留真实失焦、保护输入和输入源变化的撤权行为，不恢复旧输入租约。失败记录 `failure-launcher.log` 与 `failure-native-host.log` 与 `failure-capsule-default.log` 保留初次观察。

另修复三个测试问题：GTK 的空预编辑通知不再覆盖已提交值；D-Bus 就绪探测不再自动启动另一份 Fcitx，并等待 Rime 部署；Capsule 检查等待异步加载结束。浏览器初始失败来自首次启动欢迎弹窗，使用 Mozilla 提供的[测试安装政策](https://mozilla.github.io/policy-templates/#skiptermsofuse)准备独立下载目录，未改动宿主浏览器。

#179 保持开放：完整桌面矩阵、真实账号、锁屏/睡眠、多屏、硬件录屏与像素验收未完成。未创建 Release 或安装到本机 macOS 输入法。

后续 CI 另暴露 Buffer/Capsule 的失焦宽限计时器使用默认 250ms 调度容差，会让 200ms 撤权与 400ms 检查合并触发。修复明确使用 1ms 精度，保留检查时限；本地完整引擎/Fcitx、Buffer 四模式、进程恢复、Capsule 与网关复验通过；测试客户端另改为持续连接并读取快照，避免发送即断开与初始快照竞争。复验见 `focus-grace-e2e.log` 与 `focus-grace-provenance.json`，初次 `e2e.log` 和 `provenance.json` 仍对应前一轮，不覆盖既有证据。
