# 提案：共享按键手势核心与跨端一致性样例

状态：草案，待维护者评审。2026-11-11 基于 `d8988b9c` 的源码阅读写成；未在 Windows、Linux 或 Android 设备上复现任何问题。

## 问题

“单独轻点 Shift 切换中英文”在各端是同一条产品规则，但没有共享实现，也没有共享样例。

| 平台 | 现状 | 共享代码 |
|---|---|---|
| macOS | [`ShiftModifierGesture`](../../Sources/RimeBuffer/RIMESController.swift) 判定：500 ms 内松开、期间未作修饰键使用、会话与方案未变，则向 Rime 重放一次 Shift；另有全局快捷键 tombstone 抑制 | 无 |
| Windows | [`BrokerClient.cpp`](../../platforms/windows/native/src/tsf/BrokerClient.cpp) 在宿主进程内判定（500 ms、另一侧 Shift、重复、命令修饰键、是否亲眼看到松开、连接是否支持修饰键快照），经 Broker 到 [`ProcessShiftTap`](../../platforms/windows/native/src/engine/rime_engine.cpp) 重放 | 无 |
| Linux | Fcitx5 addon 中未找到对应手势；看起来是把原始按键交给 librime 的 `ascii_composer` 自行判定（未逐行确认） | 无 |
| Android / iOS | Shift 是大写键（`ShiftState.java`、`toggleShift`），与中英切换不是同一概念 | 无 |

后果：

- macOS 与 Windows 的阈值相同，但取消条件各写各的，任何一端修一个边界都不会让另一端的测试失败。
- Windows 的判定跨宿主 DLL、Broker、引擎三层。宿主没有把 Shift 松开交给 TSF 时会直接走“放行”分支，不切换。这与 [#288](https://github.com/scholay-xsime/xsime/issues/288) 以及微信中的反馈相符，但目前只是读代码得到的假设。
- 同类规则还有“已处理按下、等待松开”的记账：Linux 的 [#305](https://github.com/scholay-xsime/xsime/issues/305) 与 macOS 控制器里的 Buffer 回车/剪贴板快捷键记账是同一类问题。

## 目标与非目标

目标：

1. 手势判定规则只有一份可执行的定义，五端对它负责。
2. 一端改变规则时，其他端的自动检查会失败，直到对齐。
3. 平台层只做“原生事件 → 中性事件”的翻译和“决策 → 原生动作”的执行。

非目标：

- 不统一宿主输入契约、焦点与投递安全边界；这些仍按[五端架构](../architecture/system.md)由各端负责。
- 不决定切换的效果。方案自己的 `ascii_composer` 切换样式（提交编码、行内英文、无操作等）继续由 Rime 决定；核心只回答“这是不是一次合格的单独轻点”。
- 不改移动端 Shift 的大写语义。
- 不在本提案内解决 Windows 每键跨进程的性能问题；那需要 Windows 上的实测。

## 方案

### 1. 中性事件与决策

核心是一个无 I/O、无时钟、无平台类型的状态机。时间由调用方传入。

输入事件：

| 事件 | 字段 |
|---|---|
| `shift_down` | 左/右、时间戳、是否重复、按下时是否已有其他修饰键 |
| `shift_up` | 左/右、时间戳、是否由本端亲自观察到 |
| `other_key_down` | 时间戳 |
| `modifier_used` | 时间戳（Shift 参与了组合键或全局快捷键） |
| `focus_changed` / `session_changed` / `schema_changed` | 时间戳 |

输出决策：`toggle(左/右)`、`pass_through`、`discard`，并附带原因码，供各端写入不含正文的诊断日志。

### 2. 位置与形态

沿用 `Shared/InputHabits`、`Shared/CandidateMatrix` 的做法：

```text
Shared/KeyGestures/
  shift_tap.hpp          # header-only C++17 状态机
  tests.cpp              # 逐条执行 fixtures
  fixtures/shift-tap.tsv # 五端共用的样例
  README.md              # 事件模型、原因码、样例格式
```

### 3. 各端接入

| 平台 | 接入方式 |
|---|---|
| Windows | `BrokerClient.cpp` 改为喂事件、取决策；删除本地判定。x64/x86 同一实现 |
| Linux | 先用样例刻画 `ascii_composer` 的现有行为；若与样例一致则只加一致性测试，不改运行路径；不一致再决定是否由 addon 接管判定 |
| macOS | Swift 保留 `ShiftModifierGesture`，新增读取同一份 TSV 的检查。当前没有测试 target，先挂在现有 smoke 命令上 |
| Android / iOS | 本手势不适用。样例机制本身可复用：已有先例 `punctuation-layout.tsv` 由 iOS 与 Android 测试共同读取 |

### 4. 样例格式（草案）

一行一个场景，制表符分隔：编号、事件序列、期望决策。事件写作 `类型:参数@毫秒`。

```text
tap-basic        D:L@0 U:L@120                    toggle:L
tap-too-long     D:L@0 U:L@500                    discard
tap-chord        D:L@0 K@40 U:L@120               discard
tap-both-shifts  D:L@0 D:R@30 U:R@80 U:L@120      discard
tap-repeat       D:L@0 D:L@30:repeat U:L@120      discard
tap-focus-lost   D:L@0 F@60 U:L@120               discard
tap-unobserved   D:L@0 U:L@120:unobserved         pass_through
tap-hotkey       D:L@0 M@50 U:L@120               discard
```

第一批样例必须从现有 macOS 与 Windows 行为中刻画出来，而不是凭设计写；两端结论不同的场景单列，交维护者裁决。

## 分阶段

| 阶段 | 内容 | 完成标志 |
|---|---|---|
| 0 | 刻画：从 macOS、Windows 现有代码整理样例与分歧清单 | 分歧清单经维护者确认 |
| 1 | `Shared/KeyGestures` 核心与 C++ 测试，接入 Windows 与 Linux 的 CI | 确切提交的远端 CI 通过 |
| 2 | Windows 改用核心 | 真实 Windows 宿主（含微信）验收，#288 有明确结论 |
| 3 | macOS、Linux 一致性检查 | 两端读取同一 TSV 并通过 |
| 4 | 第二个手势：“等待松开”记账（#305 及 macOS 对应路径） | 同上流程 |

每个阶段一个可审查的 PR；阶段 2 之前不改变任何一端的运行行为。

## 风险与未决问题

- **微信等宿主收不到松开事件时该怎么办**：核心只能如实返回 `pass_through`。是否允许 Windows 用其他证据（例如按键状态轮询）补判，是安全边界问题，需要单独决定。
- **Linux 是否接管判定**：接管后才能与其他端完全一致，但会偏离“交给 librime”的现状，并影响其他 Fcitx5 用户习惯。
- **macOS 没有测试 target**：一致性检查暂时只能挂在 smoke 上，受[维护说明](../development/maintenance.md)中“产品构建与测试走远端 CI”的约束。
- **500 ms 是否应可配置**：目前两端都是常量。若要成为产品偏好，需按配置三层规则补字段。
- **全局快捷键 tombstone** 是 macOS 的 Carbon/IMK 时序问题，不进入核心；它以 `modifier_used` 事件的形式体现在样例里。

## 本提案没有验证的内容

- 没有在 Windows 上复现 #288，也没有确认微信的按键投递行为。
- 没有逐行确认 Linux addon 的 Shift 路径。
- 没有测量 Windows 的按键延迟；本仓库现有的按键耗时探针只覆盖 macOS，且尚未提交。
