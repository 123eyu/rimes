# 按键手势：阶段 0 刻画

对应[提案](../../docs/plans/shared-key-gesture-core.md)的阶段 0。这里只有样例和分歧清单，没有可执行代码，也不改变任何一端的运行行为。

所有结论来自 2026-11-11 对 `d8988b9c` 的源码阅读，**没有一行在任何平台上执行过**。裁决前请把每条当作待验证的陈述。

## 样例

[`fixtures/shift-tap-characterization.tsv`](fixtures/shift-tap-characterization.tsv)：制表符分隔，`#` 开头为注释。

| 列 | 含义 |
|---|---|
| `case` | 场景编号 |
| `events` | 事件序列，`类型:参数@毫秒` |
| `macos` / `windows` | 该端按现有代码的结果：`toggle:L`、`toggle:R`、`none`、`n/a`（该端不存在此事件）、`unknown`（未读到） |
| `note` | 说明与分歧编号；`-` 表示无说明 |

事件：`D` Shift 按下，`U` Shift 松开，`R` Shift 自动重复，`K` 其他键按下，`M` 命令修饰键按下，`F` 焦点/上下文变化，`S` 方案变化，`H` 占用 Shift 的全局快捷键。

阶段 0 的文件是两列结果的“刻画表”。裁决完成后合并成单一 `expected` 列，才成为各端测试读取的正式样例。

## 两端一致的规则

- 单独按下并在 500 ms 内松开同一侧 Shift，向 Rime 重放一次按下与松开；效果由方案的 `ascii_composer` 决定。
- 以下任一情况不切换：期间按下其他键；另一侧 Shift 按下；按下时已有或期间出现 Ctrl/Alt/Cmd/Win 等命令修饰键；焦点或输入上下文变化；Shift 参与了全局快捷键；没有观察到对应的按下。
- 阈值比较都是“小于 500 ms”。

来源：macOS [`RIMESController.swift`](../../Sources/RimeBuffer/RIMESController.swift)（`ShiftModifierGesture`、`handleFlags`、`voidShiftGesture`）；Windows [`BrokerClient.cpp`](../../platforms/windows/native/src/tsf/BrokerClient.cpp)（`HandleKey`、`CancelShiftTap`）、[`broker_connection.cpp`](../../platforms/windows/native/src/broker/broker_connection.cpp)、[`rime_engine.cpp`](../../platforms/windows/native/src/engine/rime_engine.cpp)（`ProcessShiftTap`）。

## 分歧清单（待维护者裁决）

| # | 场景 | macOS | Windows | 可能的用户表现 |
|---|---|---|---|---|
| 1 | 按住超过键盘重复延迟但不足 500 ms | 修饰键没有自动重复，仍切换 | Shift 自动重复被当作中断，不切换 | 重复延迟设得短（如 250 ms）的 Windows 用户，稍慢的轻点不切换 |
| 2 | 按下与松开之间方案或会话变化 | 比对方案 ID 与会话，丢弃 | TSF 层未见对应检查 | 切换方案的瞬间行为不同 |
| 3 | 宿主线程繁忙，回调晚到 | 用事件自带时间戳计时，不受影响 | 回调执行时才读时钟，时长被拉长 | 宿主卡顿时 Windows 上轻点失效 |
| 4 | 按下时 Broker 不可用或 2 ms 内拿不到锁 | 单进程，无此情况 | 不记录按下，松开直接放行 | 重连期间或负载高时轻点无声失效 |
| 5 | Buffer 输入暂停时轻点 | 未读到对应处理 | Broker 明确拒绝，避免提交草稿 | 工作台暂停状态下两端可能不同 |

分歧 3 和 4 与 [#288](https://github.com/scholay-xsime/xsime/issues/288) 及微信中的反馈方向一致：宿主处理按键的方式越特殊，Windows 越容易落进“放行、不切换”的分支。这仍是假设，需要在真实 Windows 宿主上验证。

每条需要的裁决：以哪一端为准，或两端都改。

## 尚未刻画

- **Linux**：Fcitx5 addon 中未找到独立的 Shift 手势，看起来把原始按键交给 librime；未逐行确认，也未列入表中。
- **Windows Broker 内部**：只读了 Shift 相关的三处；方案切换是否会使在途手势失效（分歧 2）需要再确认。
- **macOS Buffer 暂停路径**（分歧 5）。
- **显式切换入口**：Windows 另有 `kInputModeToggle`（界面触发），不经过手势判定，未纳入。
- **“等待松开”类记账**（[#305](https://github.com/scholay-xsime/xsime/issues/305) 及 macOS 对应路径）属于阶段 4。
