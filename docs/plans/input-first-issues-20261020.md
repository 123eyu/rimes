# 输入优先版本：开放 issue 分流快照

核对日期：2026-10-20（日本时间）。来源为 scholay/xsime 的全部开放 issue，共 88 项；重点输入缺陷读取了现有讨论，其他事项按公开正文、标签和路线图初分。未逐项重新复现，未据此修改 GitHub 状态。

主方案见[输入优先开发准备](input-first-20261020.md)。这里的 A—E 是本轮建议，与旧 `parity: P0/P1/P2` 标签不是同一套优先级。Linux 原 30 项实现与验收证据继续保留；本轮不强行清空其里程碑。

后续范围确认（2026-10-20）：五端同步开展，词库与短语新增功能暂缓。#245 与 #249 的新增词库/短语能力移出本轮；#130 的现有数据保护和输入稳定性回归保留。以下表格保留原调研快照，实施以本条范围调整为准。

| 分组 | 数量 | 本轮处理 |
|---|---:|---|
| A | 8 | 基础输入：先排查和核对验收 |
| B | 12 | 本轮优先开发或核验既有实现 |
| C | 15 | 输入相关候选与配套：不作为本轮必交 |
| D | 13 | 持续验收与主题总单：按相关子项推进 |
| E | 40 | 保留原规划或明确暂缓：不阻塞本轮 |

所有项在本快照中均为 **OPEN**。已实现、已验收、已发行必须逐项读回；下表“后移”只是不纳入这版，不是关闭或否定需求。主题总单与执行子单分开，避免重复计工作量。

## A · 基础输入：先排查和核对验收

| Issue | 本轮处理与证据要求 |
|---|---|
| [#5 【兼容性】调查 RIMES 与 Typinator 等文本扩展工具的共用问题](https://github.com/scholay/xsime/issues/5) | 核对与文本扩展工具共用的最短复现；只有确认影响普通输入时列为发布阻断。 |
| [#43 linux(ime): fcitx5 takes minutes to exit if stopped mid librime deploy](https://github.com/scholay/xsime/issues/43) | PR #146 已包含在 v1.1.7；原部署中退出/恢复场景仍须匹配桌面验收，更新过时状态。 |
| [#108 [Bug] [MacOS] 锁屏界面无法输入字母](https://github.com/scholay/xsime/issues/108) | PR #146 已包含在 v1.1.7；锁屏和睡眠唤醒分别核对实际验收，不绕过保护态。 |
| [#130 [Bug][macOS] 用户词典 LevelDB 加载失败时静默降级，无任何用户可见提示(附排查过程与建议)](https://github.com/scholay/xsime/issues/130) | 已存在故障可见性修复；核对真实故障提示与恢复，不把提示修复等同所有存储格式兼容。 |
| [#132 [Bug][macOS] 1.1.3：Shift 标点绕过 Rime，中文模式下问号/省略号仍输出英文](https://github.com/scholay/xsime/issues/132) | 已有 Shift 标点路由修复；核对中文/英文标点与真实宿主验收，满足后收口。 |
| [#133 【Windows】系统搜索栏无法使用灵犀输入法输入，需补齐常用应用兼容性回归](https://github.com/scholay/xsime/issues/133) | 已有 LPAC 管道修复且已进入 v1.1.7；实际开始菜单/任务栏搜索证据与夹具测试分别记录。 |
| [#184 [Bug] 输入法在 iPhone 13 Pro 上使用时卡顿](https://github.com/scholay/xsime/issues/184) | 先分组测 App 状态、方案、普通/并击与持续输入，不预判卡顿根因。 |
| [#261 [Bug][Linux] 验证与既有 Fcitx5 输入方案的冲突](https://github.com/scholay/xsime/issues/261) | 新增待复现；核对 Fcitx5 共存、注册、快捷键、数据目录与退出恢复。 |

## B · 本轮优先开发或核验既有实现

| Issue | 本轮处理与证据要求 |
|---|---|
| [#98 [Feature] 希望能增加五笔拼音混输](https://github.com/scholay/xsime/issues/98) | 内置混输方案已有实现；对照用户目标验证五端候选/上屏，证据满足后关闭，不重复开发。 |
| [#110 [Feature] 26键模式下，选择数字小键盘提供9宫格排列的模式](https://github.com/scholay/xsime/issues/110) | 提供数字九宫格，和中文九键、当前中文输入方案分别配置。 |
| [#138 [Feature] iOS / Android 支持直接上滑输入数字和标点](https://github.com/scholay/xsime/issues/138) | 直接上滑作为可选模式；保留旧长按上滑，覆盖快打误触与一次触控重复提交。 |
| [#140 [Feature] macbook 设置中更换输入方案后需要立刻生效](https://github.com/scholay/xsime/issues/140) | 已部署方案在安全组字边界立即应用；必要部署结束自动激活，失败保留原方案。 |
| [#141 [Feature] 五笔第五码后前四码不管唯一都需要上屏](https://github.com/scholay/xsime/issues/141) | 与四码唯一上屏分开实现和配置，保证第五键成为下一段首键。 |
| [#144 [Feature] 建议调整iOS输入法切换键的位置](https://github.com/scholay/xsime/issues/144) | 在两移动端统一底栏可用性；地球键遵循平台要求，不与中英切换混为一项。 |
| [#207 [Feature][Android] 优化Android端底栏布局、加宽空格键并支持调节键盘高度](https://github.com/scholay/xsime/issues/207) | 保留空格加宽与高度持久化两项验收；不等待 AI 或两段式空格设计。 |
| [#237 [移动端][设计] 键盘底栏、空格长度及两段式交互](https://github.com/scholay/xsime/issues/237) | 本版只取普通键盘、空格、尺寸和手势；与 AI 相关的两段式交互保留后续设计。 |
| [#241 [输入方案][五端] 链接、鼠标移入与本地选取导入](https://github.com/scholay/xsime/issues/241) | 复用现有导入器，先完善本地选取/明确拖放和切换；链接入口可后移，悬停不自动导入。 |
| [#242 [并击][PC] 内置扩展开启时在输入方案列表常显](https://github.com/scholay/xsime/issues/242) | 启用并击时可发现并持久保留入口；不强制替换用户当前普通方案。 |
| [#243 [输入方案][五端] 按导入文件和方案能力提供配置项](https://github.com/scholay/xsime/issues/243) | 本轮配置主任务：能力映射、默认来源、独立方案偏好、应用与回滚。 |
| [#245 [词库][PC] 统一词库导入与失败诊断](https://github.com/scholay/xsime/issues/245) | 区分导入独立方案和向当前兼容方案加词，补预览/诊断/回滚；不覆盖活跃 userdb。 |

## C · 输入相关候选与配套：不作为本轮必交

| Issue | 本轮处理与证据要求 |
|---|---|
| [#7 [Buffer][五端] 语音输入插件](https://github.com/scholay/xsime/issues/7) | 语音是后续输入专项；先定义结果确认与权限，不作为基础输入改进的依赖。 |
| [#76 [Feature] 手机版没有剪切板根本没法用啊](https://github.com/scholay/xsime/issues/76) | 可评估显式粘贴入口；完整剪贴板历史和后台收集不随本轮自动开启。 |
| [#99 [Feature] 希望实现类似微信输入法和搜狗输入法的候选词铺开界面](https://github.com/scholay/xsime/issues/99) | 保留展开候选需求；先明确候选身份、分页与返回语义，再决定是否纳入本版。 |
| [#109 [Feature] 希望可以提供语音输入能力](https://github.com/scholay/xsime/issues/109) | 语音需求来源，关联 #7；保留原反馈，不重复新建实现单。 |
| [#128 [Feature] 建议出下卸载方法](https://github.com/scholay/xsime/issues/128) | 核对现有各端卸载说明与数据保留，文档缺口可随版补齐。 |
| [#134 [Feature] 九键增加自定义布局功能](https://github.com/scholay/xsime/issues/134) | 九键自定义行/键/手势需要布局模型和校验；安全模板先行，深度编辑为后续批次。 |
| [#139 [Feature] macbook 安装新版本后，必需退出登陆才能使用。需要优化安装后可直接使用。](https://github.com/scholay/xsime/issues/139) | 确认现有注册/激活流程；减少不必要注销是目标，不保证绕过系统必须重新登录的情况。 |
| [#142 [Feature][macOS] 支持跟随系统外观自适应主题](https://github.com/scholay/xsime/issues/142) | 候选可读性和跟随系统外观可纳入；不因此扩大为全工作台主题重构。 |
| [#147 [Bug][Windows] 暗色主题下系统输入法图标为黑色，难以辨认](https://github.com/scholay/xsime/issues/147) | 系统输入状态图标可读性，核对真实暗色环境；未验证不关闭。 |
| [#249 [个人词库][设计] 输入统计、词库沉淀与错词淘汰联动](https://github.com/scholay/xsime/issues/249) | 本版只取用户主动维护/纠错；统计推断词级语料和自动淘汰另行设计。 |
| [#253 [品牌][五端] 状态栏、App 与设置背景图标对齐](https://github.com/scholay/xsime/issues/253) | 输入状态图标的必要可读性可补；完整五端品牌视觉刷新后移。 |
| [#254 [品牌][五端] 清理旧名称、官网和联系／群聊入口](https://github.com/scholay/xsime/issues/254) | 用户可见旧名称和错误帮助地址可顺带修；兼容 ID、数据路径与历史证据保留。 |
| [#259 [候选][五端][设计] 按笔画数量、音调与结构辅助选重](https://github.com/scholay/xsime/issues/259) | 笔画/声调/结构依赖元数据和方案适配，先评估；不与基础候选设置捆绑。 |
| [#260 [Buffer][五端] 手写输入插件](https://github.com/scholay/xsime/issues/260) | 手写作为后续输入专项，先确定识别来源、平台设备与候选确认。 |
| [#262 [Docs][五端] 更新 rimes-project-skills 名称、技能说明与关联文档](https://github.com/scholay/xsime/issues/262) | 开发技能改名属于配套文档；不影响普通输入的部分不作为发版门槛。 |

## D · 持续验收与主题总单：按相关子项推进

| Issue | 本轮处理与证据要求 |
|---|---|
| [#3 【移动端】iOS / Android 稳定性与真机验收](https://github.com/scholay/xsime/issues/3) | 移动平台维护总单；逐项关联普通输入、触控、性能和升级证据，不整体机械关闭。 |
| [#8 【Windows Native】安装、输入与稳定性验收](https://github.com/scholay/xsime/issues/8) | Windows 维护总单；无 Buffer 输入优先，真实宿主与安装/卸载作为受影响回归。 |
| [#159 [Linux][P0][09/30] 保护态、锁屏睡眠与会话生命周期](https://github.com/scholay/xsime/issues/159) | 保护态和输入生命周期仍是基础回归；不因本轮裁剪而跳过。 |
| [#160 [Linux][P0][10/30] 全局快捷键与跨输入法唤起](https://github.com/scholay/xsime/issues/160) | 保留系统热键与普通输入兼容回归；新的跨模块快捷操作后移。 |
| [#161 [Linux][P0][11/30] 桌面窗口、主题与多屏交互](https://github.com/scholay/xsime/issues/161) | 验收候选定位/焦点/多屏相关部分；Buffer/Mailbox 新形态不在本轮扩展。 |
| [#176 [Linux][P1][26/30] 并击扩展与自定义键位](https://github.com/scholay/xsime/issues/176) | 普通输入并击和用户键位继续回归，与 #242 关联；意识流联动仍暂缓。 |
| [#177 [Linux][P1][27/30] 日常输入统计与隐私聚合](https://github.com/scholay/xsime/issues/177) | 已有本地聚合统计做性能/隐私回归；不自动升级为词级正文采集。 |
| [#179 [Linux][P2][29/30] Linux 桌面、宿主和跨模块验收](https://github.com/scholay/xsime/issues/179) | Linux 实际宿主验收仍有独立意义；本轮先验收普通输入及受影响功能。 |
| [#180 [Linux][P2][30/30] deb 生命周期、同版本发布集成与文档](https://github.com/scholay/xsime/issues/180) | 维持 deb 新装/升级/卸载与数据保留门槛；不把容器测试当真实桌面通过。 |
| [#229 [Roadmap][A] 日常输入与 Buffer 交互](https://github.com/scholay/xsime/issues/229) | 只提升普通输入与基础兼容子项；Buffer 新流程继续保留。 |
| [#230 [Roadmap][B] 输入方案与插件管理](https://github.com/scholay/xsime/issues/230) | 优先方案选择、能力配置、导入和普通并击；AI/语音/手写另分批。 |
| [#231 [Roadmap][C] 个人词库与资料积累](https://github.com/scholay/xsime/issues/231) | 优先短语和兼容词库；Capsule/RSS/云词库产品扩围后移。 |
| [#233 [Roadmap][E] 五端品牌与视觉一致性](https://github.com/scholay/xsime/issues/233) | 只处理影响识别、使用和帮助可达性的必要项；全视觉改造后移。 |

## E · 保留原规划或明确暂缓：不阻塞本轮

| Issue | 本轮处理与证据要求 |
|---|---|
| [#113 [Bug] 内网机器安装出错。](https://github.com/scholay/xsime/issues/113) | 既有 Windows 签名策略限制保持 deferred；未新增可信签名/原环境通过证据。 |
| [#154 [Linux][P0][04/30] Buffer 双轨工作区与异步任务状态](https://github.com/scholay/xsime/issues/154) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#155 [Linux][P0][05/30] Linux 本地翻译引擎和资源管理](https://github.com/scholay/xsime/issues/155) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#156 [Linux][P0][06/30] 实时增量翻译与连续输入](https://github.com/scholay/xsime/issues/156) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#157 [Linux][P0][07/30] 派生结果投递、源消费与自动发送](https://github.com/scholay/xsime/issues/157) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#158 [Linux][P0][08/30] AI API 插件与流式请求](https://github.com/scholay/xsime/issues/158) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#162 [Linux][P1][12/30] Capsule 五模块宿主、管理页和底栏](https://github.com/scholay/xsime/issues/162) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#163 [Linux][P1][13/30] Capsule 结构化笔记与编辑](https://github.com/scholay/xsime/issues/163) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#164 [Linux][P1][14/30] Capsule 临时剪贴板历史](https://github.com/scholay/xsime/issues/164) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#165 [Linux][P1][15/30] Capsule 资源、文献和文件预览](https://github.com/scholay/xsime/issues/165) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#166 [Linux][P1][16/30] Capsule 截图、录屏与捕获归档](https://github.com/scholay/xsime/issues/166) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#167 [Linux][P1][17/30] Capsule 密码库、结构化秘密与 TOTP](https://github.com/scholay/xsime/issues/167) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#168 [Linux][P1][18/30] Mailbox 窗口、模块宿主和持久化](https://github.com/scholay/xsime/issues/168) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#169 [Linux][P1][19/30] Mailbox 收件、附件与 Capsule/Buffer 流转](https://github.com/scholay/xsime/issues/169) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#170 [Linux][P1][20/30] 本地 MCP/HTTP 网关与收件审核](https://github.com/scholay/xsime/issues/170) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#171 [Linux][P1][21/30] ChatGPT/Codex CLI 渠道插件](https://github.com/scholay/xsime/issues/171) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#172 [Linux][P1][22/30] Claude Code CLI 渠道插件](https://github.com/scholay/xsime/issues/172) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#173 [Linux][P1][23/30] Mailbox AI 对话工作区](https://github.com/scholay/xsime/issues/173) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#174 [Linux][P1][24/30] Mailbox PTY 终端与手动成果收取](https://github.com/scholay/xsime/issues/174) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#175 [Linux][P1][25/30][暂缓] 意识流输入插件与独立 AI 渠道候选](https://github.com/scholay/xsime/issues/175) | 维护者已明确暂缓意识流输入；保持暂缓，不能因本次输入本位方向自动恢复。 |
| [#178 [Linux][P1][28/30] 打字测速与文章练习](https://github.com/scholay/xsime/issues/178) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#227 [Feature] 请求Windows毛玻璃主题](https://github.com/scholay/xsime/issues/227) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#232 [Roadmap][D] AI 任务与 Mailbox](https://github.com/scholay/xsime/issues/232) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#234 [Roadmap][F] 跨端传输与个人云服务](https://github.com/scholay/xsime/issues/234) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#235 [Buffer][PC] 剪贴板纯文本 hint 与点击粘贴](https://github.com/scholay/xsime/issues/235) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#236 [Buffer][PC] 首次唤起位置与响应修复](https://github.com/scholay/xsime/issues/236) | Buffer 首次唤起缺陷保留；若复现证实牵连普通输入卡顿/焦点，则提升到 A。 |
| [#238 [Buffer][五端][设计] 主动／被动发送与最后一块上屏后关闭规则](https://github.com/scholay/xsime/issues/238) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#239 [Buffer][移动端] 四个固定快捷入口改为可配置插件](https://github.com/scholay/xsime/issues/239) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#240 [Buffer][PC] 设置图标、功能与插件配置统一入口](https://github.com/scholay/xsime/issues/240) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#244 [Mailbox][PC] 插件统一管理与配置](https://github.com/scholay/xsime/issues/244) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#246 [调研] 个人联网词库场景与公开接口方案](https://github.com/scholay/xsime/issues/246) | 联网词库接口研究保留；不为普通中文输入新增联网依赖。 |
| [#247 [Capsule][设计] 宿主产品定位与个人词库场景](https://github.com/scholay/xsime/issues/247) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#248 [打字练习][PC] 以导入电子书为主要练习内容](https://github.com/scholay/xsime/issues/248) | 电子书练习属于工具扩展，普通输入和配置完成后再排。 |
| [#250 [个人词库][设计] RSS 与本地资料入库的候选触发](https://github.com/scholay/xsime/issues/250) | RSS/本地资料自动成为候选需独立数据与触发设计，不纳入本轮。 |
| [#251 [AI][五端] Buffer 长按提交 Mailbox 并等待完整结果](https://github.com/scholay/xsime/issues/251) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#252 [Mailbox][PC][设计] 右下角常驻小屏幕形态](https://github.com/scholay/xsime/issues/252) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#255 [跨端][五端] Buffer + Mailbox 文本与密码端到端加密传输](https://github.com/scholay/xsime/issues/255) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#256 [跨端][协议] 内容中继、接续发送与去重](https://github.com/scholay/xsime/issues/256) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#257 [服务] 官方账号鉴权、中继、云存储与开源 Docker 自部署](https://github.com/scholay/xsime/issues/257) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |
| [#258 [AI][五端][设计] 可手工配置的自定义 Buffer 插件统一范式](https://github.com/scholay/xsime/issues/258) | 保留原路线与既有实现/验收记录；这版不扩展该工作流。若影响普通输入稳定性，再单独提级处理。 |

## 状态校准顺序

1. 先核对 #108/#130/#132/#133/#43 中过时的 PR #146 状态：GitHub 已确认合并，提交同时包含在 main 与 v1.1.7。保留原报告和讨论，增补当前事实，不抹掉历史。
2. 对 #98 先验证现有五笔拼音混输；对 #141 单独实现/验收第五码顶字，不能以四码唯一上屏代替。
3. #184/#261 先完成复现与根因分诊；“没有复现”本身不是已解决。
4. 匹配维护者已有的明确验收与具体构建；足以支持结论的项目可收口，其余标清待复现/待验收/待发布。
5. 从 B 选择可验证闭环进入本版里程碑；C 保留候选，D 关联子项，E 留在原路线。关闭 issue 不等于安装包已公开更新，发行事实单独记录。
