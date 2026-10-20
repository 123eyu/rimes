# XSIME 五端产品路线图

2026-10-19：按维护者确认的六条产品主线组织 29 项需求，与既有稳定性、兼容性和设备验收工作共用[原有 GitHub Project](https://github.com/users/scholay/projects/1)。本页记录产品方向；具体状态、讨论和验收证据以关联 Issue 为准。

五端为 macOS、Windows、Linux、iOS、Android。桌面三端与移动两端共享产品规范，分别实现宿主适配、权限和交互。沿用单一 `main`、短分支、独立构建与验收；规划、代码完成、设备验收和公开发布分别记录。

## 六条产品主线

| 主线 | 总单 | 范围 | 当前侧重 |
|---|---|---|---|
| A · 日常输入与 Buffer 交互 | [#229](https://github.com/scholay/xsime/issues/229) | 让日常输入、Buffer 唤起、编辑、发送及关闭形成一致且可验证的体验。 | 当前重点 |
| B · 输入方案与插件管理 | [#230](https://github.com/scholay/xsime/issues/230) | 统一方案和插件的发现、启停、配置、导入与管理入口。 | 下一阶段 |
| C · 个人词库与资料积累 | [#231](https://github.com/scholay/xsime/issues/231) | 明确个人内容成为候选、积累为词库、反馈纠错和长期保存的完整关系。 | 下一阶段 |
| D · AI 任务与 Mailbox | [#232](https://github.com/scholay/xsime/issues/232) | 将用户主动提交的 AI 任务、执行状态、完整结果和后续操作统一到 Mailbox 工作流。 | 下一阶段 |
| E · 五端品牌与视觉一致性 | [#233](https://github.com/scholay/xsime/issues/233) | 统一图标资源、品牌名称、官网及联系/群聊入口，核对实际客户端和分发材料。 | 当前重点 |
| F · 跨端传输与个人云服务 | [#234](https://github.com/scholay/xsime/issues/234) | 通过 Buffer + Mailbox 提供显式的文本/密码端到端加密传输、内容中继，以及官方和自部署服务。 | 后续探索 |

## 阶段与先后关系

- **当前重点**：持续处理稳定性与兼容性，验证 Buffer 首次唤起及 Linux/Fcitx5 共存；确定移动键盘、发送/关闭规则、设置入口、方案配置、AI 插件范式和 Mailbox 形态；开展个人词库研究与品牌/文档清理。
- **下一阶段**：按上述规范交付插件化入口、方案/词库导入、并击方案选择、辅助选重、语音/手写插件、AI 长按转交、电子书练习、资料候选触发及视觉对齐。跨端相同能力分别验收，不要求一轮完成全部平台。
- **后续探索**：明确中继协议和设备身份、密钥与存储边界后，开展 Buffer + Mailbox 加密传输、官方账号/云能力及开源 Docker 自部署。

阶段表示依赖和优先级，不是发布日期。设计/调研也是可交付任务：先写出可评审规范和结论，再据此拆实现。没有承诺日期，也没有自动把旧反馈全部提升为当前重点。

## 本轮需求与 Issue

原始第 4 项分为本地词库导入和联网接口研究；后者与第 21 项合并研究。语音复用 [#7](https://github.com/scholay/xsime/issues/7)，保留 [#109](https://github.com/scholay/xsime/issues/109) 反馈。以下 29 个工作项覆盖原始 29 项需求。

| 原始需求 | 主线 | 工作项 | 平台 | 阶段 |
|---|---|---|---|---|
| 1 | A | [#235](https://github.com/scholay/xsime/issues/235) [Buffer][PC] 剪贴板纯文本 hint 与点击粘贴 | 桌面端 | 下一阶段 |
| 2 | A | [#236](https://github.com/scholay/xsime/issues/236) [Buffer][PC] 首次唤起位置与响应修复 | 桌面端 | 当前重点 |
| 5 | A | [#237](https://github.com/scholay/xsime/issues/237) [移动端][设计] 键盘底栏、空格长度及两段式交互 | 移动端 | 当前重点 |
| 11 | A | [#238](https://github.com/scholay/xsime/issues/238) [Buffer][五端][设计] 主动／被动发送与最后一块上屏后关闭规则 | 跨平台 | 当前重点 |
| 3 | B | [#239](https://github.com/scholay/xsime/issues/239) [Buffer][移动端] 四个固定快捷入口改为可配置插件 | 移动端 | 下一阶段 |
| 12 | B | [#240](https://github.com/scholay/xsime/issues/240) [Buffer][PC] 设置图标、功能与插件配置统一入口 | 桌面端 | 当前重点 |
| 13 | B | [#241](https://github.com/scholay/xsime/issues/241) [输入方案][五端] 链接、鼠标移入与本地选取导入 | 跨平台 | 下一阶段 |
| 14 | B | [#242](https://github.com/scholay/xsime/issues/242) [并击][PC] 内置扩展开启时在输入方案列表常显 | 桌面端 | 下一阶段 |
| 15 | B | [#243](https://github.com/scholay/xsime/issues/243) [输入方案][五端] 按导入文件和方案能力提供配置项 | 跨平台 | 当前重点 |
| 16 | B | [#244](https://github.com/scholay/xsime/issues/244) [Mailbox][PC] 插件统一管理与配置 | 桌面端 | 下一阶段 |
| 4（本地导入） | C | [#245](https://github.com/scholay/xsime/issues/245) [词库][PC] 统一词库导入与失败诊断 | 桌面端 | 下一阶段 |
| 4（联网接口）、21 | C | [#246](https://github.com/scholay/xsime/issues/246) [调研] 个人联网词库场景与公开接口方案 | 跨平台 | 当前重点 |
| 17 | C | [#247](https://github.com/scholay/xsime/issues/247) [Capsule][设计] 宿主产品定位与个人词库场景 | 桌面端 | 当前重点 |
| 18 | C | [#248](https://github.com/scholay/xsime/issues/248) [打字练习][PC] 以导入电子书为主要练习内容 | 桌面端 | 下一阶段 |
| 19 | C | [#249](https://github.com/scholay/xsime/issues/249) [个人词库][设计] 输入统计、词库沉淀与错词淘汰联动 | 跨平台 | 当前重点 |
| 20 | C | [#250](https://github.com/scholay/xsime/issues/250) [个人词库][设计] RSS 与本地资料入库的候选触发 | 跨平台 | 下一阶段 |
| 9 | D | [#251](https://github.com/scholay/xsime/issues/251) [AI][五端] Buffer 长按提交 Mailbox 并等待完整结果 | 跨平台 | 下一阶段 |
| 10 | D | [#252](https://github.com/scholay/xsime/issues/252) [Mailbox][PC][设计] 右下角常驻小屏幕形态 | 桌面端 | 当前重点 |
| 6 | E | [#253](https://github.com/scholay/xsime/issues/253) [品牌][五端] 状态栏、App 与设置背景图标对齐 | 跨平台 | 下一阶段 |
| 8 | E | [#254](https://github.com/scholay/xsime/issues/254) [品牌][五端] 清理旧名称、官网和联系／群聊入口 | 跨平台 | 当前重点 |
| 7 | F | [#255](https://github.com/scholay/xsime/issues/255) [跨端][五端] Buffer + Mailbox 文本与密码端到端加密传输 | 跨平台 | 后续探索 |
| 22 | F | [#256](https://github.com/scholay/xsime/issues/256) [跨端][协议] 内容中继、接续发送与去重 | 跨平台 | 后续探索 |
| 23 | F | [#257](https://github.com/scholay/xsime/issues/257) [服务] 官方账号鉴权、中继、云存储与开源 Docker 自部署 | 跨平台 | 后续探索 |
| 24 | B | [#258](https://github.com/scholay/xsime/issues/258) [AI][五端][设计] 可手工配置的自定义 Buffer 插件统一范式 | 跨平台 | 当前重点 |
| 25 | B | [#259](https://github.com/scholay/xsime/issues/259) [候选][五端][设计] 按笔画数量、音调与结构辅助选重 | 跨平台 | 下一阶段 |
| 26 | B | [#7](https://github.com/scholay/xsime/issues/7) [Buffer][五端] 语音输入插件 | 跨平台 | 下一阶段 |
| 27 | B | [#260](https://github.com/scholay/xsime/issues/260) [Buffer][五端] 手写输入插件 | 跨平台 | 下一阶段 |
| 28 | A | [#261](https://github.com/scholay/xsime/issues/261) [Bug][Linux] 验证与既有 Fcitx5 输入方案的冲突 | Linux | 当前重点 |
| 29 | E | [#262](https://github.com/scholay/xsime/issues/262) [Docs][五端] 更新 rimes-project-skills 名称、技能说明与关联文档 | 跨平台 | 当前重点 |

## 产品决策与依赖

- Buffer 承接输入、编辑与显式投递；剪贴板文本只作为空输入框的 hint，点击才粘贴，开始输入即放弃。首次唤起、主动/被动发送、最后一块上屏与关闭选项形成统一规则，见 [#235](https://github.com/scholay/xsime/issues/235)、[#236](https://github.com/scholay/xsime/issues/236)、[#238](https://github.com/scholay/xsime/issues/238)。
- 先统一 Buffer 插件范式与配置入口（[#258](https://github.com/scholay/xsime/issues/258)、[#240](https://github.com/scholay/xsime/issues/240)），再迁移移动端固定快捷入口（[#239](https://github.com/scholay/xsime/issues/239)），接入语音（[#7](https://github.com/scholay/xsime/issues/7)）与手写（[#260](https://github.com/scholay/xsime/issues/260)）。统一范式允许用户手工配置各自 AI 服务，不要求集中到官方 AI；移动端权限、后台生命周期与桌面 CLI 能力分别处理。
- AI 类型插件支持长按将任务交给 Mailbox 并等待完整结果（[#251](https://github.com/scholay/xsime/issues/251)）；Mailbox 桌面形态趋向右下角常驻小屏幕（[#252](https://github.com/scholay/xsime/issues/252)），插件管理统一（[#244](https://github.com/scholay/xsime/issues/244)）。完整返回、失败、取消与再次投递的行为在实现前定义。
- 输入方案导入（[#241](https://github.com/scholay/xsime/issues/241)）与方案能力配置（[#243](https://github.com/scholay/xsime/issues/243)）共同设计，保留用户所述“鼠标移入”入口，具体交互在设计中确认；移动端定义对应触控方式。并击扩展开启时作为常显方案（[#242](https://github.com/scholay/xsime/issues/242)）。笔画数量、音调、结构选重（[#259](https://github.com/scholay/xsime/issues/259)）以数据来源及方案适配研究为前置。
- Capsule 的宿主定位（[#247](https://github.com/scholay/xsime/issues/247)）围绕个人词库使用场景确定，与统计/沉淀/错词淘汰（[#249](https://github.com/scholay/xsime/issues/249)）、RSS 和本地资料触发（[#250](https://github.com/scholay/xsime/issues/250)）联动；联网词库的个人使用与公开接口统一研究（[#246](https://github.com/scholay/xsime/issues/246)），避免先锁定厂商或收费服务。
- 跨端中继（[#256](https://github.com/scholay/xsime/issues/256)）、端到端加密文本/密码传输（[#255](https://github.com/scholay/xsime/issues/255)）及账号/云/自部署（[#257](https://github.com/scholay/xsime/issues/257)）分开追踪并声明依赖。覆盖手机↔手机、手机↔电脑、电脑↔电脑；是否提供官方服务不改变用户可选择自部署的方向。现有本地凭据、保护态及精确目标投递边界在新设计验收前保持。
- 品牌统一依据 [BRANDING.md](BRANDING.md)，图标 [#253](https://github.com/scholay/xsime/issues/253)、旧品牌/地址/联系入口 [#254](https://github.com/scholay/xsime/issues/254)、开发技能名称和关联文档 [#262](https://github.com/scholay/xsime/issues/262) 分项验收；兼容标识、上游 Rime 名称和历史证据保留必要区分。

## 旧计划如何衔接

| 既有内容 | 本轮处理 |
|---|---|
| 移动端 [#3](https://github.com/scholay/xsime/issues/3)、Windows [#8](https://github.com/scholay/xsime/issues/8) 维护总单 | 继续承担平台稳定性/设备验收；产品设计与跨平台能力进入六条主线。 |
| 五端方案导入 [#4](https://github.com/scholay/xsime/issues/4)、方案反馈 [#140](https://github.com/scholay/xsime/issues/140)/#141 | [#4](https://github.com/scholay/xsime/issues/4) 已完成结论保留；新增入口与按方案配置由 [#241](https://github.com/scholay/xsime/issues/241)/#243 跟踪，不把新增范围算作旧功能回归。 |
| 语音 [#7](https://github.com/scholay/xsime/issues/7)、反馈 [#109](https://github.com/scholay/xsime/issues/109) | [#7](https://github.com/scholay/xsime/issues/7) 转为五端 Buffer 插件工作项并进入下一阶段；保留原提案与 [#109](https://github.com/scholay/xsime/issues/109) 来源。 |
| 已关闭或未计划内容，包括 [#6](https://github.com/scholay/xsime/issues/6) | 保留历史结论；本轮通过新 Issue 表达新范围，不批量重开。 |
| Linux 30 项 [#151](https://github.com/scholay/xsime/issues/151)—[#180](https://github.com/scholay/xsime/issues/180) | 保留编号、提交和验收证据；[#175](https://github.com/scholay/xsime/issues/175) 原暂缓结论不变。与 Buffer/Mailbox/设置/个人词库相关的界面项目补充新规范关联，设计确定后重定验收基线，避免继续复刻已改变的旧布局。 |
| Linux [#179](https://github.com/scholay/xsime/issues/179)/#180、[#207](https://github.com/scholay/xsime/issues/207) 与新 [#261](https://github.com/scholay/xsime/issues/261) | 继续记录实机/像素验收和独立缺陷；[#261](https://github.com/scholay/xsime/issues/261) 是 Fcitx5 冲突验证，尚未确认根因。 |
| 鸿蒙探索与其他未纳入的新功能反馈 | 保留原后续探索状态，不纳入本轮“五端”交付承诺。 |

## 看板与交付规则

Project 用“产品主线”表达归属，“阶段”表达顺序，“平台”表达实现/验收范围，“类型”表达工作性质。主题总单保留清单与依赖，执行卡片记录验收标准；跨主线事项只保留一个主归属，通过关联链接协作。

Status 沿用“待梳理 → 可认领 → 进行中 → 待验收 → 已完成”。只有实际验证支持时才推进状态；历史已完成卡片与 Issue 关闭状态对齐。缺陷先复现再修复；设计和调研以结论交付，不以功能上线作为其唯一完成条件。

每轮交付记录源提交、构建配置、测试命令、产物摘要、设备/宿主、通过项及未验证项。平台和能力清单不能替代验证证据；发布事实见 [MAINTENANCE.md](MAINTENANCE.md) 与版本发布记录。

## 历史：2026-09-29 平台基础与 2026-10-17 Linux 扩围

以下按原文保留当时的范围、环境和证据；其中“当前”“本轮”“尚未实现”均指记录日期，不表示今日产品现状。产品计划以上述 2026-10-19 决策为准，运行时事实以源码、各平台验证记录及 [SYSTEM-ARCHITECTURE.md](SYSTEM-ARCHITECTURE.md) 为准。

<details>
<summary>展开历史平台路线与验证记录</summary>

### Windows 与 Android 开发路线

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

</details>
