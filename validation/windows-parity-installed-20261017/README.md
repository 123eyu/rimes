# Windows 交互对齐与本机安装（2026-10-17）

本轮基于 `823c910` 和工作区已有改动，完成 Capsule 非激活底栏与 Mailbox 逐消息组件。
本机版本为 `1.1.5-win-parity.20261017`；精确目录、源码清单摘要、Broker 进程与安装状态见
`installed-result.json`，不把相同 Git 基线等同于没有本地改动。

## 本轮完成

- Capsule 底栏不激活窗口，点击卡片/筛选保留宿主焦点；借用同一精确 TSF 会话进行 Rime 搜索，
  方向键选卡片、Tab 筛选、Ctrl+A/C/V/F/N、Esc 返回。松开唤出快捷键不会撤销搜索接管。
- Enter 快速插入复用原有 TSF 精确上下文校验、单调请求序号和宿主确认；确认成功才关闭底栏，
  不经剪贴板或合成按键，不消耗 Buffer。换输入框、保护、关闭及改路由均撤销借用权限。
- 密码验证接收物理字母批次，不把翻译后的文本当口令。管理/编辑窗口仍正常取得焦点；
  没有可借用的输入框时，点击搜索可进入原生编辑，普通卡片 Enter 只复制关闭。
- Capsule 可见图片卡片后台生成有界缩略图，缓存最多 32 项。保留前轮 940×208 底栏、
  940×660 管理、460×400 编辑、原位详情和主题尺寸。
- Mailbox 每条消息独立显示身份/时间、原生可选择正文；用户消息整块背景，流式更新只重排变化的正文，
  保留选区和阅读位置，接近底部且没有选区时跟随新增消息。
- 接收消息独立复制/加入 Buffer/存入 Capsule；图片打开/保存；待处理收件忽略。
  Markdown 标题、粗斜体、代码、列表/勾选项、引用、制表位表格、HTTP(S)/mailto 链接及 JSON 格式化。
  格式化只影响显示，复制和持久化仍保留原文。
- 保留此前实时翻译、Buffer 尺寸与 Codex 并击首组 guard 修正，一并进入本次安装。

## 验证与边界

两架构分别构建并验证 9 组 CTest：Peripherals、Runtime、SettingsUi、Workbench、BufferLayout、
Settings、FirstKey、CaretRegression、BrokerClientContract；生产 UI 各导出 44 个状态，包含
深浅主题、680/1000 DIP、底栏/原位详情/编辑、覆盖抽屉、Markdown/图片。

两架构另用真实 librime 和隔离命名管道验证首组 D/F/K/L 并击映射 `you'`，按空格提交“有”，
以及重新聚焦、取消和延迟写入撤权。测试使用临时用户目录，不调用在线 AI，不写系统剪贴板。
并击 E2E 使用 300 ms 分组时间窗以排除测试线程调度干扰；日常用户的时间窗配置保持原值。
源码 `git diff --check` 通过；现有构建目录别名产生的 MSB8028 警告保留。

图像来自 Windows 生产控件离屏渲染，macOS 基准是仓库当前 Swift 实现；不是 macOS 真机截图。
真实 Codex 的“首组全部留为英文”仍以用户安装后现场复测为准。

## 安装与生效

安装使用当前仓库的版本目录部署/校验脚本，保留原版本和用户数据，并先备份词库、设置和已有周边数据。
运行脚本使用 Windows PowerShell 5.1；PowerShell 7 缺少安装器使用的 MutexSecurity 构造重载，
首次尝试在取得维护锁前停止，未切换注册。

**安装器标记需要注销 Windows 再登录。** 两种架构注册和文件校验成功、新 Broker 已运行，并不表示
所有已经打开的 TSF 宿主均已卸载旧 DLL。重新登录后再测试 Codex 并击首字和 Capsule 焦点交互。

RTFD、结构化密码/TOTP、文献引用、跨设备同步、远端推送、完整 VT 终端等功能差距见
[`PERIPHERALS.md`](../../platforms/windows/native/PERIPHERALS.md)；本轮不是所有 macOS 功能对等声明。
