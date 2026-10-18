# Windows Buffer、Capsule 与设置滚动条修正

2026-10-17。本机版本 `1.1.5-win-buffer-ui.20261017`，基于 `823c910` 和当前工作树。

## 完成内容

- Buffer：按 macOS 当前图标语义重绘原生矢量图标：四宫格、向下导入文档、向右复制退出、三颗星芒、实心纸飞机。参考 `BufferWindowController.swift` 的 `arrow.down.doc`、`rectangle.portrait.and.arrow.forward`、`sparkles`、`paperplane.fill`；没有依赖或分发 Apple 字体。
- 发送快捷键：按住时在内容轨底部显示 2 DIP 进度条，短按发下一块、满进度启动原有顺序发送。保留用户配置的长按时长。松开、焦点/配置变化、关闭与保护时清除；组字确认及生成请求不显示发送进度。现有精确目标和逐块确认机制保留。
- Capsule：修复 popup 使用 `CW_USEDEFAULT` 后被挤到左上角。首次在指针所在屏幕的工作区水平居中、距底部 24 DIP；再次打开保留手动移动位置。底栏/管理/紧凑编辑切换保留水平中心及底边，并限制在工作区内。
- 插件与统计设置页：用 12 DIP 宽的主题原生滚动控件取代系统非客户区滚动槽，滑块为 5/7 DIP，移除白底和立体边框。保留 Windows 原生拖动、分页、键盘和可访问滚动范围；无溢出时隐藏。

## 验证

- x64 / x86 Release 相关目标编译通过。
- 两架构 Runtime、Peripherals、BufferLayout 通过；x64 SettingsUi 首次通过。x86 SettingsUi 首次在既有“前台定时器撤销模拟宿主”断言超时，独立重跑通过，保留两次日志。
- 两架构原生 MetricsUi 和 Peripheral UI 验证通过。新增检查涵盖长按半程/完成/释放、组字与焦点取消、Capsule 实际窗口首次位置及重开位置保留、滚动范围与底部可达性。
- 生产 Buffer 绘制器在四主题、96/144/192 DPI 导出；检查待生成、生成中、结果、翻译和长按进度状态。设置导出覆盖四主题的顶部/滚动位置。
- 本地插件权威源码 `OfficialPlugins/native/windows/metrics_ui.cpp` 与宿主导入副本一致；子模块仍有未提交修改，开发安装包的源码归档已包含实际工作树版本及摘要，不将旧子模块提交号当作完整源码。
- `git diff --check` 通过。保留已有 MSB8028 构建目录别名警告。

## 安装与范围

安装记录见 `installed-result.json`。词库、设置、内容库、统计与插件配置已备份保留；配置摘要未变化。新版 Broker 已启动。

本次 x64/x86 TSF DLL 与前一安装版本完全相同，修正随 Broker 重启生效，无需注销或重启 Codex。安装器仍保留旧的 `requiresSignOut` 标记，它不代表本次界面修改需要注销。

界面图片及输入测试使用隔离数据；未向用户实际输入框输入测试文字。真实使用体验仍由用户验收。主仓库与插件修改未提交、未推送。

## 预览

- [Buffer 深色与长按进度](buffer-night.png)、[Buffer 浅色](buffer-day.png)
- [插件滚动条](plugins-night.png)、[统计滚动条](statistics-day.png)
