# Windows 周边与 Buffer 联动验证 — 2026-10-17

基线 `823c910`，包含此前未提交的翻译、并击首字、设置等改动；本记录只说明新增的
Capsule / Mailbox 原生预览与 Buffer 暂存联动。**没有安装、发布或修改用户数据。**

## 结果

- x64 / x86：`RimesBroker`、`RimesPeripheralTests` 编译通过。
- 每种架构 8 组 CTest 均通过：`RimesPeripherals`、`RimesRuntime`、`RimesSettingsUi`、
  `RimesWorkbench`、`RimesBufferLayout`、`RimesSettings`、`RimesFirstKey`、`RimesCaretRegression`。
- 最后 UI 调整后，两种架构再次运行完整 Peripheral 基础及原生窗口探针，均通过。
- 每种架构导出 32 帧：8 模块 × 深/浅主题 × 1000×700 / 680×500 DIP 外框。
  检查所有可见原生控件不越界、不重叠；本机实际 DPI 为 144。
- `git diff --check` 通过。构建输出保留既有 MSBuild 中间目录别名警告，无编译/链接错误。

基础探针覆盖 revision 冲突、模块启停后数据保留、损坏记录隔离、路径限制、DPAPI 往返、
密码禁止普通附件导入、物理并击默认/自定义/损坏凭据、独立收件附件、归档、冻结路由、
有界 VT/UTF-8/CJK/emoji、真实 ConPTY 输入/调整尺寸/关闭，以及 Buffer 暂存不继承旧目标。

UI 探针使用模拟连接器，覆盖新对话选路由、全局切换后原路由保持、完整回复落盘、
重命名、关窗保存草稿、停用后迟到回复不保存、终端选择器不继承对话禁用状态。
测试只使用随机临时目录，不调用在线 AI，不读取或改写日常系统剪贴板。

日志和 Broker SHA-256 见本目录文件。完整生成图在 native/out/peripherals-preview
和 peripherals-preview-x86（忽略目录），此处保留四张代表图。

## 视觉检查

下面是生产原生控件在隔离样本上的离屏渲染，不是真实账户或 macOS 截图。

![Capsule 浅色管理窗口](day-notes-1000.png)

![Mailbox 对话最小窗口](night-chat-680.png)

![Mailbox 终端最小窗口](night-terminal-680.png)

## 验证边界

完整实现范围和未移植项目见 [PERIPHERALS.md](../../platforms/windows/native/PERIPHERALS.md)。
当前首版尚不等于全部 macOS 对齐：快捷底栏、RTFD/iCloud、结构化密码/TOTP、文献排版、
外部推送协议和完整 VT 终端仍有缺口。

物理键盘查看口令、真实 AI/Codex TUI、Windows 剪贴板敏感格式、多屏实际交互，
以及日常 Codex 并击首词问题，仍需已安装版本的人工验收。
前序首字修复的隔离验证见 [首字记录](../windows-first-key-20261017/README.md)，不能据此宣称真机问题已经解决。
