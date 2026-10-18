# Linux 文章练习验证

对应 [#178](https://github.com/scholay/xsime/issues/178)。原生 `TypingTestSession` 和 `TypingTestHistoryStore` 导出 46 组场景、945 次逐步状态以及 46 个成绩有效性样例；共享 C++ 核心逐项对比通过。11 个 Swift 文件的摘要守卫要求原生实现改变后重新导出并审核。样例覆盖中文、英文、扩展字素、规范等价、重复字符差分、相同文字替换、时钟、上限、取消、重复结束、回退与并击，不使用个人输入数据。

真实 Fcitx 测试前端验证 Fly 计时与 Yoyo 松键批次、组合串回退、当前输入区和调用方身份、日常统计隔离、结束屏障、失焦、保护输入、插件撤权、配置变更及崩溃后的有限租约；同时经过生产异步遥测客户端。真实 GTK IM module → Fcitx → 编辑区验证组字按键不会漏计、组合串回退、原生成文、明确结束与退出保存。GTK 控件测试验证预编辑/成文、键重复、替换、超限回滚、粘贴拦截、部分/完整成绩、历史分组、重启、关闭时后台落盘、独立清空和取消。存储测试覆盖外部替换、符号链接、损坏/过大文件、明确修复与数据不覆盖。

当前构建、日志、确切 deb 和生命周期结果见 [验证清单](../../../validation/linux-typing-practice-20261018/provenance.json)。源安装卸载也验证保留练习、日常统计及修复前备份。原生 debug 构建和 5 个原生烟测通过；已安装的 macOS 输入法未更新，系统级旧应用仍触发安装脚本预检。

## 视觉与桌面边界

[32 组原生 AppKit / GTK 对照](../../../validation/linux-typing-practice-20261018/README.md)涵盖中文/英文、准备/进行/完成/历史和四种主题，使用相同公开数据和 2 倍缩放。整页、卡片、原文区、编辑区和图表几何匹配（分数卡片允许半个逻辑像素的整数分配差）。字体度量、字重、图标、选择控件及完整交互状态仍需校准，保留 `requires_review`。

GNOME/KDE、X11/Wayland 的真实输入区焦点、硬件布局、多屏、锁屏/睡眠和辅助功能仍由 [#179](https://github.com/scholay/xsime/issues/179) 验收。Xvfb 与 Fcitx 测试前端不等于实际桌面验收，未关闭任务、创建发布标签或上传公开安装包。

## 复现

```sh
.build/out/Products/Debug/RimeBuffer typing-practice-linux-fixtures /tmp/native-practice.json
.build/out/Products/Debug/RimeBuffer typing-practice-linux-ui /tmp/macos-practice
python3 platforms/linux/typing-practice/verify_contract.py
ctest --test-dir platforms/linux/ime/build --output-on-failure -R practice
RIMES_SHARED_DIR=/tmp/rimes-shared dbus-run-session -- \
  platforms/linux/ime/build/practice-probe/rimes-settings platforms/linux/ime/build . test-data
NO_AT_BRIDGE=1 G_DEBUG=fatal-warnings dbus-run-session -- xvfb-run -a \
  platforms/linux/ime/build/runtime/rimes-practice-ui-tests /tmp/linux-practice
```

## CI 隔离目录修复（2026-10-18）

[首次 amd64 CI](https://github.com/scholay/xsime/actions/runs/39107396318) 的 90 项回归、实际练习输入和 Sway PNG 已通过，两个 job 随后都因文档 portal 的 FUSE 挂载无法清理临时目录而失败。练习输入测试改用不自动启动 portal 的隔离总线；真正 Wayland 捕获仍启动真实 portal，其清理根据 `/proc/self/mountinfo` 检查挂载，避免断开的 FUSE 根目录让 `Path.is_mount()` 返回假阴性。未忽略清理错误或把未验证录屏改成通过。

修复后本地 Ubuntu 24.04 非 root 用户的实际 GTK/Fcitx 练习及真实 Sway PNG 回归通过；具有 FUSE 的 GitHub runner 结果继续单独记录。

[amd64 重试](https://github.com/scholay/xsime/actions/runs/39108628489) 的 deb job、90 项 CTest、实际练习和 portal 清理通过，但最终 Buffer 回归在 `focus-out grace must drop capture` 失败，因此 CI 总状态仍为失败。原因是失焦计时器使用 sd-event 默认 250ms 调度容差，200ms 宽限与 400ms 检查可能同时触发；后续桌面验收将 Buffer/Capsule 撤权计时精度统一设为 1ms，保留原检查时限。结果继续由 PR #212 的复验单独记录。
