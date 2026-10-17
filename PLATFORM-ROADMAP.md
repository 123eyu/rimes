# Windows 与 Android 开发路线

> **2026-10-17 Linux 范围更新**：维护者已授权 Linux 完整版对齐 macOS 的内置扩展、Buffer、Capsule、Mailbox 及核心插件。按 [30 项执行计划](platforms/linux/PARITY.md) 与 [GitHub 里程碑](https://github.com/scholay/xsime/milestone/1) 逐项实现和验收。以下“Linux 仅维护、不扩展 AI/翻译”等为历史阶段边界，由本条取代。Linux 保持 Fcitx5 原生投递和独立桌面验收，不复刻 Apple 专属 API。

2026-09-29 起：**Windows 对标 macOS，Android 对标 iOS；Linux 收敛到维护。**
沿用单一 `main` 集成、短开发分支、各平台独立构建与发布。行为与数据格式对齐，宿主接口按各
平台实现；不把 Apple 专属 API、权限或桌面能力直接当作 Android/Windows 可用能力。

本轮从本地整合提交 `b02fe9ccaae34bef927e8f7e8f78c2441d1e91c3` 开始，工作分支为
`codex/windows-android-foundation`。原工作区的 iOS/共享核心未提交改动仍保留在原处。
下一次对齐 Apple 端基线时，需要明确纳入哪些后续已验证提交。

## 对齐范围与顺序

| 阶段 | Windows → macOS | Android → iOS | 验收标准 |
|---|---|---|---|
| 0：工程与可运行基础 | 复用 TSF/Broker；补齐产品 Lua、词库和 OpenCC 数据 | 原生 App/IME、独立包名、英文输入与临时 Buffer | 独立构建、测试、可复验的产物；清楚区分实机结果 |
| 1：日常输入 | 五套方案、并击、候选、焦点恢复；x64/x86 运行包与安装/卸载 | librime/JNI、拼音/自然码/五笔/英文、候选和组字、并击与触控 | 真实宿主连续输入、方案切换、重启恢复；不改坏用户词库 |
| 2：完整 Default Buffer | 原生工作台、块编辑、快捷键、Return/Esc、精确目标投递 | 双行布局、块/光标/选区编辑、滑动、长按删除、空格手势、震动 | 跨框、失焦、隐藏、保护态不串投；交付失败不消费 |
| 3：AI 与翻译 | 渠道插件、Provider/模型设置、取消与流式；本机翻译适配 | iOS 对等 Provider、设备凭据存储、流式、取消与翻译适配 | 源修订/目标变化使旧结果失效；网络与凭据授权；结果手动确认 |
| 4：平台完整体验 | Capsule、Mailbox、设置、数据迁移、签名安装与升级 | 主 App 配置/方案导入导出、联想、主题/横屏、无障碍与分发 | 新装/升级/回滚、隐私、设备矩阵与独立发布验收 |

Android 不额外继承 iOS 当前未提供的 Capsule、Mailbox、剪贴板监控或桌面 CLI 渠道。
Windows/macOS 的交互目标相同，但 TSF、进程边界、安装机制必须采用 Windows 原生实现。
翻译服务的选择另行做能力和隐私评估；不能承诺跨平台复用 Apple Translation。

## 当前已落地

- Windows 已有 TSF/Broker、候选/组字、重连和双架构基础。本轮增加
  `prepare-native-data.py`：复用审核过的数据策略，只复制完整的 RIMES Lua/词库闭包，
  加入显式提供的 OpenCC 运行时配置/字典与许可证，生成逐文件 SHA-256 清单。
  打包不读用户目录、不覆盖已有安装；传输后可重新校验。
  实测还修复了 Session 0 测试模式的握手冲突，保留端点显式开关和进程/用户身份校验。
- Android 从零建立原生工程、Gradle wrapper、独立开发包和设置/试打页面。
  英文、数字、符号通过 `InputConnection` 输入；临时 Buffer 支持按块/全部插入、清空和删除。
  编辑目标/修订绑定，密码与私密字段禁用；关闭键盘或换字段清除草稿。
  Android 16 真机已安装 `0.1.0-dev.2` 并完成基础输入验证；修复了导航栏遮挡底排按键、
  试打页被键盘遮住，以及横屏 Buffer 挤掉按键的问题。
- 尚未实现的功能以上表和 [Android README](platforms/android/README.md) 为准。
  **当前仅完成第一批基础代码，不代表两个平台已完成对标。**

## 2026-09-29 本轮验证

| 项目 | 实际证据 | 仍缺少 |
|---|---|---|
| Windows | x64/x86 Release 均编译通过，各 8 项 CTest、完整数据包的默认方案引擎冒烟、Fake TSF + 真实 Broker 输入测试均通过；打包 9 项及既有策略 9 项测试通过；59 文件通过远端哈希复验 | 所有方案逐项输入、真实桌面宿主、安装/升级、完整 Buffer/Capsule/Mailbox |
| Android | Debug APK 构建、9 项 JVM Buffer 测试、Lint 通过；真机英文/数字/符号、删除/选区删除、换行、Buffer 逐块/全部插入、换框/隐藏/换键盘清理、密码框禁用通过；系统设置搜索框跨应用输入和搜索回车通过；横屏布局修复后复验通过 | 中文引擎/触控与完整 iOS 功能；私密标志/数字字段、旧系统、手势导航和更多宿主兼容性 |
| 设备连接 | Young 通过已有受信主机密钥和机器名核对，已用于本轮构建；Android 16 手机重新连接后安装成功；测试完成已恢复原搜狗输入法与竖屏锁定，开发版保留 | Windows 图形桌面验收；Android 设备与宿主矩阵 |

原生 Windows 验证提交为 `1b2a86a0ddb87d6e0743874df6f797eccc31d364`，
Android APK 源码提交为 `55e2ec7b22471afd647436d7c68d5e90e50d6507`。
细节见 [Windows 验证记录](platforms/windows/native/VALIDATION.md) 和
[Android 验证记录](platforms/android/VALIDATION.md)。Android Lint 有 Gradle 新版本、图标和
备份规则三条提示，无错误；远端 CI 未运行。未 push、发布或修改现有 macOS/iOS 安装。

## Linux 维护边界

保留已合入的 Fcitx5 IME、Default Buffer、Markdown Note Capsule；只处理阻断使用、
数据安全、构建/安装回归和必要的依赖维护。暂不扩展 Linux 的 AI、翻译、Mailbox 或 Capsule
类型。现存 #43 部署时退出较慢、#44 拖动后的极短点击窗口按维护缺陷跟踪，不扩展产品范围。

## 每轮交付记录

逐项记录源提交、构建配置、测试命令、产物哈希、设备/宿主、通过项及未验证项。
代码、构建、安装、实机可用、签名与公开发布分别验收，不用完成百分比代替这些证据。
优先完成阶段 1 的可用输入闭环，再进入完整 Buffer 和高级功能。
