# Linux 日常输入统计验证

任务 [#177](https://github.com/scholay/xsime/issues/177)。共享聚合实现见 [官方插件 PR #17](https://github.com/scholay/rimes-plugins/pull/17)，Linux 使用锁定的插件源码；macOS 原生实现作为行为和视觉参照。

## 已验证行为

- 当前 macOS 实际存储导出 3 组场景、32 次观测；Linux 逐次回放并比较原生聚合 JSON。11 个 Swift 文件的摘要守卫保证参照发生变化时构建失败，要求重新导出并审核。
- 实际 Fcitx 测试前端验证物理键映射、按住重复、快捷键和导航、Rime 成文字数、Buffer 收取只计一次、飞耀计时批次、呦呦松键批次及取消。保护字段、敏感输入和自有设置窗口排除；采集开关与插件启用独立，关闭再开启保留选择。
- 聚合核心验证午夜、时钟倒退、10 秒会话边界、日期跨度、饱和计数及缺测值。私有存储验证重启、单写入者、外部修改、损坏文件、过大文件、符号链接、不覆盖原数据的明确修复、独立清空、后台队列顺序及退出落盘。
- GTK 控件验证日期与曲线联动、历史离线查看、两个独立开关、清空取消与独立性、仅包含聚合的私有导出、重启和明确修复。UI 测试启用 fatal critical diagnostics；请求和文件操作不阻塞 GTK 主线程。
- macOS debug 构建及 7 个原生烟测通过：键频、速度、聚合迁移、输入遥测、历史热力图、统计面板和插件分发。所有测试使用公开样例与隔离目录，不读取个人输入记录。

干净 Release 构建、全部 CTest、实际 Fcitx 回归、deb 安装/重装/卸载和产物摘要见 [验证清单](../../../validation/linux-statistics-20261018/provenance.json)。卸载保留词库、学习记录、统计文件及明确修复前的备份。

## 视觉与真实桌面

[8 组实际双端渲染](../../../validation/linux-statistics-20261018/README.md)覆盖每日/历史两页和四种主题。公开数据、日期、逻辑尺寸和 2 倍缩放相同。整页、卡片、趋势、键盘热力图、历史日历及会话图的坐标与尺寸已匹配；字体度量、字重、图标、日期选择器、控件细节和完整交互状态仍为 `requires_review`，不能据此宣布像素级还原完成。

GNOME/KDE、X11/Wayland 实际桌面、硬件键盘布局、锁屏/睡眠、缩放及辅助功能继续由 [#179](https://github.com/scholay/xsime/issues/179) 验收。容器中的测试前端和 Xvfb 不是这些桌面的验收结果。第 28 项的文章练习成绩独立实现；此处仅确认自有窗口不会污染日常统计。

本机 macOS 安装仍被已有系统级 `/Library/Input Methods/RIMES.app` 的安装脚本预检拦截，未删除系统应用或更新已运行版本；原生源码构建及隔离烟测通过不代表安装更新完成。

## 复现

```sh
# macOS：隔离的原生聚合与界面
.build/out/Products/Debug/RimeBuffer statistics-linux-fixtures /tmp/native-statistics.json
.build/out/Products/Debug/RimeBuffer statistics-dashboard-smoke /tmp/native-statistics-ui
python3 platforms/linux/statistics/verify_contract.py

# Linux：已配置完整原生数据的构建
ctest --test-dir platforms/linux/ime/build --output-on-failure -R metrics
RIMES_SHARED_DIR=/tmp/rimes-shared dbus-run-session -- \
  platforms/linux/ime/build/rimes-metrics-fcitx-e2e platforms/linux/ime/build . test-data
NO_AT_BRIDGE=1 G_DEBUG=fatal-criticals TZ=Asia/Tokyo dbus-run-session -- \
  xvfb-run -a -s '-screen 0 2560x2400x24' \
  platforms/linux/ime/build/runtime/rimes-metrics-ui-tests /tmp/linux-statistics-ui
```

没有创建发布标签或向公开 Release 上传安装包。
