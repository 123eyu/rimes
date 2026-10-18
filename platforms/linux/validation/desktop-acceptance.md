# Linux 桌面与跨模块验收

对应 #179，意识流 #175 按维护者要求暂缓。本页区分已执行的源码/容器检查和真实桌面验收，不把未运行项标成通过。

## 当前环境与证据

当前开发机为 macOS，Linux 验证在 Ubuntu 24.04 arm64 Docker 容器进行。Xvfb、GTK3、Qt5、Fcitx5 测试前端及 headless Sway 能验证协议、原生成文和部分合成器行为；容器没有真实 GNOME/KDE 登录会话、物理显示器、锁屏/睡眠或用户登录的外部 AI 账号。

| 场景 | Xvfb / X11 | headless wlroots | GNOME / KDE 实际桌面 |
| --- | --- | --- | --- |
| GTK / Qt 普通输入与候选 | 分别记录实际成文结果；warning/skip 不通过 | 不从 X11 推断 | 待执行 |
| 浏览器、终端与 Electron | 需要逐宿主实际成文 | 需要逐宿主实际成文 | 待执行 |
| Buffer 双轨翻译、AI 与原生发送 | Fcitx、离线引擎和本地假服务通过不等于真实账号通过 | 窗口及焦点另测 | 待执行 |
| Capsule 剪贴板、文件、捕获、密码 | 分模块协议/存储/桌面客户端日志 | 实际 clipboard 与 portal；录屏硬件路径另列 | 待执行 |
| Mailbox 收件、会话、PTY、附件流转 | GTK、真实 PTY 和 Fcitx 流转 | 窗口会话另测 | 待执行 |
| 并击、统计、文章练习 | 实际 Rime/Fcitx 和 GTK 输入区通过 | 不推断物理键盘与 Wayland 焦点 | 待执行 |
| 快捷键、拖动、多屏、缩放 | X11 虚拟显示及共享几何测试 | layer-shell / portal 的支持与降级分别记录 | 待执行 |
| 保护输入、锁屏/睡眠、恢复 | D-Bus 服务注入与宿主撤权回归 | 缺少完整登录会话 | 待执行 |

## 必须执行的实际桌面步骤

在 GNOME X11、GNOME Wayland、KDE X11、KDE Wayland 和目标 wlroots 桌面分别记录发行版、桌面版本、会话类型、显卡/缩放/显示器、Fcitx 与 GTK/Qt IM module 版本、源码提交及 deb SHA-256。每个目标都用独立公开测试档案，保留个人词库和学习记录。

1. GTK、Qt、浏览器、终端分别完成中英文、预编辑、候选翻页、退格、方案切换及原生上屏；密码字段不能接管内容。
2. Buffer 连续输入触发增量翻译；更改语言/渠道、取消、失焦、关闭和重开，确认旧结果不能发送；检查返回编辑、复制、自动发送与源消费。
3. Capsule 五模块完成新建、修改、搜索和投递；剪贴板原始 MIME、附件副本、截屏/录屏 portal 同意/拒绝/取消和密码解锁/自动锁定分别验证。
4. Mailbox 执行收件审核、附件到 Capsule/Buffer、同一渠道续聊、取消/重试、PTY 输入/退出和手动收取成果。真实 ChatGPT/Codex/Claude 账号验收与本地假服务单列。
5. 并击验证单键、跨批与取消；统计启停和练习隔离；文章练习首键计时、预编辑、并击、部分/完整成绩、历史和重启恢复。
6. 各模块依次经历锁屏、睡眠、切换用户、Fcitx 重启、UI 崩溃、跨屏拖动与 100%/150%/200% 缩放；重新聚焦后不能继承旧投递授权。
7. 同一内容/主题/尺寸/缩放对照 macOS，覆盖空、忙、错误、禁用、选中、悬停、焦点、保护与恢复状态。保存实际双端截图、布局读数和差异图，逐项记录尚未消除的差异。

## 视觉状态

`visual_inventory.py` 当前校验了 19 份对照清单、229 组双端截图及差异图，全部为 `requires_review`；该清单不代表完整状态覆盖。匹配页面尺寸、自动化通过或图片相似度均不能关闭像素验收。字体度量、控件、图标、材质及未覆盖的交互状态必须逐项处理；第 25 项的界面不在本轮验收中。

本轮 [源码与容器证据](../../../validation/linux-desktop-acceptance-20261018/README.md) 包含实际 GTK3/Qt5/Firefox/xterm 原生上屏，前三者的 Buffer 捕获/Return，以及 Firefox 密码框保护。详细逐项证据见各模块 `VALIDATION.md` 与 PARITY.md。只有所有适用目标有结果后才能关闭 #179；发行流程可准备并演练，公开发布不由此文档触发。

## 可复现宿主与截图检查

先按 `ime/README.md` 构建，生成共享数据目录。下面的宿主测试必须在新 D-Bus 会话和测试显示器内执行；只输入公开样例，使用临时 Fcitx/Firefox 档案。此输入测试的总线不自动启动 portal，避免文档 portal 在待清理目录内创建挂载；真正 portal 的截屏/录屏与剪贴板测试另行执行。实际成文由 GTK、Qt、浏览器 DOM 和终端 PTY 分别读回，普通输入没有使用剪贴板注入。

```sh
dbus-run-session --config-file=platforms/linux/ime/tests/portal_fixture_bus.conf -- \
  xvfb-run -a python3 platforms/linux/ime/tests/e2e/desktop_hosts.py \
  platforms/linux/ime/build /path/to/staged-data /tmp/xsime-desktop-results \
  --firefox /path/to/firefox
python3 platforms/linux/validation/visual_inventory.py \
  --output /tmp/xsime-visual-inventory.json
```

宿主测试需要 `xdotool`、`xfwm4`、GTK3/Qt5 Fcitx IM module、`xterm`。未传 `--firefox` 时将浏览器记为 `not_run`，不能由 GTK 结果推断浏览器通过。非零退出是失败，报告中任何 `not_run` 仍需补验；Xvfb 成功不等于 GNOME/KDE 或 Wayland 桌面通过。

X11 全局快捷键在主键释放后检查原窗口仍聚焦，再申请新的 Fcitx 输入目标。这样键盘抓取产生的临时失焦不会立即撤销刚打开的工作区；真实失焦、保护字段、停用与输入源变化仍撤权。计时使用明确精度，[sd-event 的零精度参数](https://www.freedesktop.org/software/systemd/man/257/sd_event_add_time.html)会选择默认调度容差；[X11 GrabKey 协议](https://www.x.org/releases/X11R7.5/doc/x11proto/proto.pdf)说明主键释放才结束主动抓取。

本轮 Firefox 158.0 从 Mozilla 官方 tarball 解压至容器临时目录，记录了下载摘要；独立测试副本使用 `SkipTermsOfUse`、`UserMessaging.SkipOnboarding` 和禁用遥测政策，以免首次启动欢迎页遮住公开测试输入框。不得将这些测试设置写入个人浏览器配置。
