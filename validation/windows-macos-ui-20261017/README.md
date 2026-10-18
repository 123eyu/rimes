# Windows 对齐 macOS：界面与交互检查

> 后续非激活 Capsule、完整消息组件与安装结果见
> [2026-10-17 安装记录](../windows-parity-installed-20261017/README.md)。以下保留本轮当时的检查状态。

本轮先替换 Capsule / Mailbox 的普通表单布局，再核对 Buffer 当前尺寸与交互。
基线 `823c910`，包含已有未提交开发工作。**已构建、未安装；不是 macOS 全功能对等声明。**

## 已落地并检查

| 区域 | 当前结果 |
| --- | --- |
| Capsule 底栏 | 940×208，206×126 卡片、60pt 筛选列、顶部模块/搜索/新建/管理，选中菜单与悬停复制 |
| Capsule 详情 | 双击原位查看、方向键选行、Esc 逐层返回；密码四个脱敏并击槽位，授权和显示分别计时 |
| Capsule 管理/编辑 | 940×660 三栏管理、460×400 紧凑编辑；保留底边切换，筛选按模块记忆 |
| Mailbox | 64pt 图标导航、独立未读徽标、84pt 列表行、标题/来源分层、圆角消息输入区 |
| Mailbox 抽屉 | 列表、终端扩展区在窄窗覆盖正文；打开关闭不重排正文/终端，遮罩与 Esc 关闭、宽窗手动收起 |
| 消息输入 | Enter 发送、Shift+Enter 换行；IME marked composition 期间不发送；重命名同步标题；刷新保留原生文字选择/滚动位置 |
| 终端扩展区 | 产物/文件/会话，最多 60 个当前目录文件、有界文本/图片预览、显式收取、打开既有收件 |
| Buffer | 根据当前 Swift 实现核对 30pt 工具栏、73/105 总高、默认宽 760、翻译轨道内部留白；现有保存宽度不变 |

渲染复查修正了 150% 缩放下相邻分段按钮的 1 像素重叠、列表下边界取整、
遮罩坐标和层叠、中文字体回退与下拉框外观。布局不再先隐藏全部输入控件，
避免在布局更新时打断原生 IME 组字。

## 图像

图像是 Windows **生产控件与 painter 的隔离离屏渲染**，不是 macOS 截图，也不是日常安装版本。
macOS 基准为仓库当前 Swift 源码；精确文件 SHA-256 见 `build-info.json`。

- [Capsule 底栏](day-capsule-rail.png)、[原位详情](day-capsule-detail.png)、[紧凑编辑](day-capsule-editor.png)
- [Capsule 三栏管理](day-notes-1000.png)
- [Mailbox 浅色](day-chat-1000.png)、[深色窄窗](night-chat-680.png)、[覆盖式列表](night-mailbox-drawer.png)
- [终端扩展区](day-terminal-1000.png)、[窄窗扩展抽屉](night-terminal-inspector.png)
- [实时翻译](translation-1-680-144.png)、[520pt 窄窗实时翻译](translation-0-520-144.png)

## 验证结果

- x64 / x86 构建均通过，含 Broker、PeripheralTests、BufferLayout、Runtime、Settings 与 SettingsUI。
- 两种架构分别通过 8 组 CTest：Peripherals、Runtime、SettingsUi、Workbench、BufferLayout、Settings、FirstKey、CaretRegression。
- 两种架构分别通过 42 张生产 UI 状态导出和边界/交互断言。
- UI 使用临时用户目录与模拟 AI；基础测试包含真实 ConPTY / DPAPI，不调用在线 AI，不改动日常词库和系统剪贴板。
- 一次开发中测试在原有 3 秒等待内未满足模拟回复断言，随后未复现；保留诊断并将异步 UI 等待上限调整到 10 秒。最终两架构检查通过。
- `git diff --check` 通过。已有构建目录别名的 MSB8028 警告仍存在。

```powershell
cmake --build --preset windows-x64-release --target RimesBroker RimesPeripheralTests RimesBufferLayoutTests RimesSettingsUITests RimesRuntimeTests RimesSettingsTests
ctest --preset windows-x64-release -R '^(RimesPeripherals|RimesRuntime|RimesSettingsUi|RimesWorkbench|RimesBufferLayout|RimesSettings|RimesFirstKey|RimesCaretRegression)$' --output-on-failure
./out/build/windows-x64/Release/RimesPeripheralTests.exe --ui ./out/peripherals-design-preview
# x86 使用对应 preset / 路径；两种 UI 测试串行执行。
```

## 尚未对等的部分

- Capsule 仍使用可激活原生窗口；macOS 从宿主借用 Rime 键盘、底栏不激活与精确目标快捷插入尚未移植。当前选择内容后暂存到 Buffer，再重新绑定发送。
- Mailbox 仍缺少完整逐消息组件（独立动作、整块 user 背景、完整 Markdown/表格/链接）；当前提供可选择的消息文本、身份、标题和代码段。
- RTFD、结构化密码/TOTP、iCloud、远端推送、完整终端 VT 等功能差距详见 `platforms/windows/native/PERIPHERALS.md`。
- 真机 Codex 首个 `you'` 并击、实际物理口令、真实服务和多屏焦点仍待现场验收。未把隔离测试通过等同于已证明 Codex 原现场问题消失。

当前修改没有安装到日常输入法，未提交或推送。安装版本与开发源码分开记录。
