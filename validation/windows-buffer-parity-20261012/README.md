# Windows Buffer 与 macOS 差异诊断（历史基线）

本文保留修复前 `6b90e48` 的诊断记录；其中源码位置和隔离探针只适用于该基线。用户已确认本轮范围并要求实施，修复及验收状态见 [implementation.md](implementation.md)。用户纠正 B09：实际是 Alt+Shift+Enter **完全无法发送暂存内容，只有点击有效**，不能仅归类为逐块发送行为差异。

日期：2026-10-12（Asia/Shanghai）。源码基线：`6b90e48`。场景：浏览器、Codex 和其他 Electron 应用。

结论：Windows 已复用了部分 macOS 配色、尺寸和轨道外观，但输入编辑、候选定位、插件呈现和结果生命周期尚未等价。只调整主题、圆角或间距不能解决本轮发现的问题。

## 证据边界

- 只读核对了运行进程：`C:\Program Files\RIMES\versions\1.1.2-6b90e48d67d7-1f671ec5343f\x64\RimesBroker.exe`。版本目录标记与当前提交一致；没有做已安装全部文件与源码的二进制对应校验。
- 对照的是当前 macOS **源码**，不是一台现场运行的 Mac。仓库历史说明仍含 78/112pt 等旧值；当前两端基础高度实际都是 73/105，不能把旧文档数值当成待修缺陷。
- 当前工具不能操作和截图 Windows 原生 Buffer，未进行浏览器/Codex 的现场输入，也未宣称视觉像素验收。下列体验步骤由用户核对。
- 用当前 `model.cpp` 和 `buffer_layout.hpp` 新编译了隔离诊断程序，使用合成内容复现 5 个条件。程序不连接日用 Broker，不读取剪贴板、设置或凭据，不调用 AI，也不向任何宿主输入。
- 新编译并运行现有 `model_tests.cpp`：通过。因此，现有测试通过并不能证明 macOS 行为已经对齐。
- 初始诊断阶段只新增本目录的资料；随后的产品改动、测试及尚待实机验证的边界记录在 implementation.md。

## 发现清单

P1 表示内容正确性或主要输入流程问题；P2 表示交互/功能差异。此处是建议优先级，待用户核对频率和影响后定稿。

### B01 · 翻译未就绪时，“发送”会回退为发送原文（P1，隔离复现）

Windows `Model::Send()` 仅凭结果队列是否非空选择结果或原文，不检查当前是否处于翻译模式。UI 在有原文、已绑定且未忙碌时启用纸飞机，点击也直接调用发送。因此，未带句末标点的原文尚未自动翻译时，可以被直接发进宿主。

macOS 的 `BufferDeliveryContentRouter` 先选定当前派生工作区；选择翻译后，投递来源不会因为译文为空而回退到普通原文。

- Windows：[model.cpp:233](../../platforms/windows/native/src/workbench/model.cpp)、[window.cpp:205、587](../../platforms/windows/native/src/workbench/window.cpp)。
- macOS：[BufferDeliveryCoordinator.swift:111](../../Sources/RimeBuffer/BufferDeliveryCoordinator.swift)。
- 验证：选择翻译，输入一段不带句末标点的短句；译文为空时点击纸飞机，记录进入宿主的是原文、译文，还是没有发送。在临时文本框中检查，不需要发送聊天消息。
- 预期方向：主动作依据当前工作区及生成状态决定“翻译/等待/发送”，结果未就绪不能静默改发原文。

### B02 · 尾部编辑会清空待发译文，部分已发送原文还会被重新翻译（P1，隔离复现）

Windows 把源文关联存到一次译文的**最后一个**输出块上，只有最后块收到投递确认才移除对应原文。`Backspace()` 则无条件清空全部结果及已翻译前缀。诊断程序复现：一条原文生成两块译文 → 第一块发送确认 → 新增无关尾字再删除 → 第二块译文消失 → 下次请求重新包含原来的整句。重复生成已发送内容的前提已经成立；实际重复上屏还取决于后续生成与发送。

macOS 在某原文单元的第一个子块投递成功后就退休精确原文范围，并把剩余译文保存为独立待发内容。尾部编辑不会作废这部分内容。

- Windows：[model.cpp:140、214、260](../../platforms/windows/native/src/workbench/model.cpp)。
- macOS：[AppleTranslationPlugin.swift:1205](../../Sources/RimeBuffer/AppleTranslationPlugin.swift)。
- 验证：先获得译文，再在原文末尾输入一个字并退格，观察已完成译文是否消失。若一个原文单元确实产生多个译文块，再只发送首块后重复操作，观察是否重译已发送部分；不必强求真实模型每次都返回多块。
- 预期方向：按精确源文单元维护结果归属，首个子块确认即退休原文，剩余输出独立保留。

### B03 · Buffer 组字时，候选框仍跟宿主光标（P1，源码确认，实机待核对）

Windows 捕获时仍进入 `UpdateCandidateWindow()`，该函数查询 TSF 宿主 caret，未改用 Buffer 逻辑插入点。macOS 则从 Buffer 内联组字获取 anchor，使用 `.bufferCaret` 显示独立候选窗。

- Windows：[TextService.cpp:1128、1209](../../platforms/windows/native/src/tsf/TextService.cpp)。
- macOS：[RIMESController.swift:7325](../../Sources/RimeBuffer/RIMESController.swift)。
- 验证：在浏览器或 Codex 输入框唤出 Buffer，将面板拖离宿主输入框，输入尚未选词的拼音，观察候选跟在宿主还是 Buffer 拼音旁。
- 预期方向：捕获期间使用经过同一会话验证的 Buffer 逻辑 caret；直输期间仍使用宿主 caret。

### B04 · 长输入不会自动把新文字和组字光标滚入视野（P1，源码确认，实机待核对）

Windows 两条轨道的滚动值初始为 0，更新时只将原值限制在合法范围。内容不断增长不会把 0 改成尾部位置，只有鼠标滚轮主动改变滚动值。绘制又会裁切正文，所以输入超过可视宽度后，最新文字及尾部光标可能在视口外。

macOS 在内容刷新时调用 `scrollChipsToInsertionPoint()`，围绕实际插入点或 preedit caret 调整可见范围。

- Windows：[window.cpp:114、666、908](../../platforms/windows/native/src/workbench/window.cpp)。
- macOS：[BufferInlineView.swift:2080、2912](../../Sources/RimeBuffer/BufferInlineView.swift)。
- 验证：先缩窄 Buffer，再连续输入到超过一屏，不滚轮；检查能否持续看见最后一个字和当前拼音。
- 预期方向：输入/插入点变化时保证逻辑 caret 可见，同时避免无关状态更新打断用户浏览旧内容。

### B05 · 块间编辑及 Buffer 的常用编辑快捷键缺失（P1，源码确认，宿主表现待核对）

Windows Model 只有尾部追加、尾部删除，没有 `insertionIndex` 或全选状态。正文点击只负责重新绑定。`BeforeKey()` 遇到未配置为发送动作的 Ctrl/Alt/Win 组合键就放行；没有 Buffer 的 Ctrl+A、Ctrl+V、Ctrl+C 路由。因此不能把“显示了输入光标”理解成具备 macOS 的逻辑编辑能力。

macOS 支持普通 Buffer 块间左右移动、首尾跳转、向前删除、全选和剪贴板导入。生成结果复制还有独立的快捷键路径。它也不是任意块内自由编辑器，下一轮不应擅自把目标扩成多行富文本编辑器。

- Windows：[model.hpp:39](../../platforms/windows/native/src/workbench/model.hpp)、[runtime.cpp:101](../../platforms/windows/native/src/workbench/runtime.cpp)、[buffer_layout.hpp:287](../../platforms/windows/native/src/ui/buffer_layout.hpp)。
- macOS：[RIMESController.swift:4247、4288、5436](../../Sources/RimeBuffer/RIMESController.swift)、[BufferModel.swift:650、949](../../Sources/RimeBuffer/BufferModel.swift)。
- 验证 A：普通输入中先形成三块已选词内容，按左键后再输入，看能否插入中间。
- 验证 B：Buffer 捕获时分别 Ctrl+A、Ctrl+V，记录操作发生在 Buffer 还是宿主；用临时测试文字，避免混入原有草稿。
- 预期方向：先定义捕获期间按键归属，再补模型与 UI，不能仅把 Ctrl 组合键全部吞掉。

### B06 · AI 生成始终双轨，没有 macOS 的单轨切换和明确返回编辑（P2，布局隔离复现）

Windows 只要选“生成”就令 `BufferNeedsResultRail()` 为真；即使空内容也创建上下两轨并升高到 105 DIP。macOS 的 AI 生成使用 `singleExchange`：待生成时显示原文，等待和结果显示时切换为同一个单轨视口，并按状态提供返回编辑。实时翻译才采用按内容展开的源/目标双轨。

- Windows：[buffer_layout.hpp:107、152](../../platforms/windows/native/src/ui/buffer_layout.hpp)。
- macOS：[BufferWindowController.swift:128、169、229](../../Sources/RimeBuffer/BufferWindowController.swift)。
- 验证：空 Buffer 切“生成”，再经历有原文、生成中、生成完成三种状态；记录轨道数、窗口高度以及返回原文编辑的入口。
- 预期方向：以工作区状态决定轨道，不用“插件模式 != 输入”直接等同双轨。

### B07 · “复制”没有“复制完整结果并关闭”的语义（P2，源码确认）

Windows 从一次 Snapshot 取结果；若结果为空则取原文，写入剪贴板后结束。它不关闭/暂停 Buffer，没有生成结果完整性和二次身份校验，也没有与按钮等价的 Ctrl+C 路径。生成还在进行时，仅因为原文存在就可以启用复制。

macOS 对生成结果冻结并复验工作区、generation 和 block 身份，成功复制后暂停并隐藏，不消费原文、不调用上屏。

- Windows：[window.cpp:204、458、482](../../platforms/windows/native/src/workbench/window.cpp)。
- macOS：[BufferWindowController.swift:2792、2807](../../Sources/RimeBuffer/BufferWindowController.swift)。
- 验证：生成完整结果后点击复制，看 Buffer 是否隐藏；在临时文本框粘贴检查内容。另核对生成中点击复制拿到的究竟是什么，以及 Ctrl+C 是否等价。
- 预期方向：把“复制原文”和“复制完整生成结果并关闭”的条件与含义明确分开；完整结果复制须有一致的按钮/快捷键事务。

### B08 · 工具栏的功能与关联反馈尚未对齐（P2，源码确认）

Windows 的菜单硬编码为“输入／生成／翻译”，其状态主要是“已绑定／暂停”等文字和“更多”菜单。当前界面没有 macOS 对应的目标 App 真实图标与关联信标、已启用 Buffer 插件选择模型、按插件变化的紧凑配置入口、自动发送时长/开关，以及 AI 返回编辑入口。

macOS 已有这些独立控件和工作区状态。Windows 的意识流和电音演奏也没有对应 Buffer 模式。这里既有交互缺失，也有未移植能力，不能一律归入“换皮”。

- Windows：[window.cpp:343、369](../../platforms/windows/native/src/workbench/window.cpp)、[buffer_paint.hpp:248、262](../../platforms/windows/native/src/ui/buffer_paint.hpp)、[provider.hpp:13](../../platforms/windows/native/src/workbench/provider.hpp)。
- macOS：[BufferWindowController.swift:2116、2144、2146、2085](../../Sources/RimeBuffer/BufferWindowController.swift)。
- 验证：核对你在 Mac 常用、Windows 找不到的具体控件；尤其是“当前究竟关联哪个输入框”的反馈是否足够。
- 范围建议：本轮先补与核心输入、发送和返回编辑直接有关的控件。意识流、音乐、新连接器等完整移植单独估算，不因菜单缺项就一次性全部实现。

### B09 · Enter 手感不同，属于需要明确决定的产品差异（P2，源码确认）

Windows 默认用 Alt+Shift+Enter 发送，普通/Shift Enter 只结算组字，无组字时被消费，不发暂存内容；发送键可配置。macOS 的 Buffer Enter 会按状态结算组字、请求生成，或短按发送下一块/长按发送全部。

Windows 的 1.2 秒发送长按和组字期间防误发已经存在，不能误报为完全缺失。当前默认快捷键也可能被旧配置覆盖，本轮未读取个人设置。

- Windows：[provider.hpp:21](../../platforms/windows/native/src/workbench/provider.hpp)、[runtime.cpp:101、165](../../platforms/windows/native/src/workbench/runtime.cpp)。
- macOS：[RIMESController.swift:4063](../../Sources/RimeBuffer/RIMESController.swift)。
- 验证：无组字时按普通 Enter，再按你当前配置的发送键；反馈你希望 Windows 沿用 Mac 手感还是保留组合键。
- 范围建议：确认期望后再决定默认值与迁移策略，不静默覆盖已有自定义快捷键。

### B10 · 主动作说明和忙碌反馈与真实动作不一致（P2，源码确认）

Windows 在 AI 结果为空时，主按钮实际请求生成，图标也可以是星光；但 tooltip 仍固定描述“发送下一块”。忙碌指示只是绘制一个静态圆环，不是 macOS 的动态进度指示。用户难以从同一按钮稳定判断“生成、等待还是发送”。

- Windows：[window.cpp:461、587](../../platforms/windows/native/src/workbench/window.cpp)、[buffer_paint.hpp:371、383](../../platforms/windows/native/src/ui/buffer_paint.hpp)。
- 验证：AI 有原文但未生成时悬停主按钮，再触发生成，记录提示和等待反馈是否符合真实状态。
- 预期方向：一个状态决定动作、图标、提示、禁用原因与键盘行为，避免各自判断。

## 浏览器 / Codex 实机验证顺序

各做一轮：一个浏览器临时文本框、Codex 中未提交的测试草稿。不要实际发送聊天消息。

1. 普通 Buffer：唤出、拖动，输入拼音，确认候选位置（B03）。
2. 输入超过一轨宽度，观察尾部；尝试左右插入、全选、粘贴（B04、B05）。
3. 原文无组字时按 Enter 与已配置的发送键，记录差异（B09）。
4. AI 生成：观察空/等待/结果的轨道、返回编辑、tooltip，再复制结果（B06、B07、B10）。没有已配置的 AI 服务可标“暂未测”。
5. 翻译：未成句时检查纸飞机；有译文后编辑无关尾字（B01、B02）。分块部分发送只在能稳定产生该条件时验证。
6. 切到另一个输入框，再点击 Buffer，检查关联反馈和恢复捕获（B08）。额外记录：点回同一输入框的同一光标位置是否会退出捕获；这依赖真实宿主事件，尚未判定为已确认缺陷。

推荐反馈格式：`B03：浏览器有，Codex 无；B04：都有；B09：希望普通 Enter 发送`。可以只回复编号和“有 / 无 / 未测”，再标最影响使用的三项。

## 下一次开发任务候选（用户验证后定稿）

**名称：Windows Buffer 核心输入、结果投递与界面对齐 macOS。**

第一部分优先解决 B01/B02：以当前工作区决定投递来源，修复翻译单元退休与剩余结果保留，增加“部分 ACK 后编辑尾部”的回归用例。

第二部分解决 B03/B04/B05：Buffer 逻辑 caret 与候选位置一致，输入可视性稳定，补块间编辑及明确的剪贴板键路由；浏览器/Codex 都要用真实输入框验收。

第三部分解决 B06/B07/B10，并选取 B08 中的核心工具栏：AI 单轨切换、返回编辑、完整结果复制并关闭、统一按钮状态和真实关联反馈。B09 按用户验证后的键位偏好决定，保留自定义配置。

暂不把意识流、音乐、全套插件/连接器移植和其他平台的新能力全部塞入这一次任务。目标是先让普通输入、生成、翻译三个已存在流程完整且行为一致。

完成门槛：两种宿主的上述用例有逐项结果；多 DPI/窗口拖动后候选与轨道不脱离；焦点切换、发送失败、迟到回调与部分投递不串目标、不重复消费；自动化覆盖真实缺陷序列；UI 截图来自本次构建的真实运行状态。状态机测试或预览画面不能代替宿主输入验收。

## 隔离诊断记录

诊断源：[probe.cpp](probe.cpp)。MSVC 19.44.35224，x64，`/std:c++20 /EHsc /utf-8 /W4 /WX`；链接当前 `platforms/windows/native/src/workbench/model.cpp`，包含当前 UI layout 头文件。构建产物位于系统临时目录 `rimes-buffer-parity-20261012`，不进入发布包。

```text
REPRODUCED: empty AI mode creates two rails (105 DIP)
REPRODUCED: translation mode without a result sends untranslated source
REPRODUCED: first translated child ACK leaves its original source unretired
REPRODUCED: editing an unrelated tail erases remaining accepted translation
REPRODUCED: next translation request includes source whose first child was delivered
exit: 0 (5 个待修行为均复现，不表示产品通过验收)

Workbench model tests passed
exit: 0 (当前源码的现有模型测试)
```

已有视觉移植说明 [MACOS-VISUAL-PARITY.md](../../platforms/windows/native/MACOS-VISUAL-PARITY.md) 明确要求保留当时的 Runtime 模型和既有能力，主要移植配色、尺寸和绘制。这解释了为什么“已有 Mac 风格对齐记录”与本轮发现的功能/交互差异可以同时成立。后续应以逐项行为验收取代笼统的“macOS-aligned”判断。
