# Windows 首键处理修复验证 — 2026-10-17

## 并击场景补充

用户随后澄清使用并击输入，整组 `you` 留成英文。此前顺序输入首键测试不能覆盖该报告。检查本机设置确认当前方案为 `my_combo`、中文模式；未修改设置。

发现 Windows 与 macOS 的实际差异：Broker 已在物理按键等待结算时返回 `kPendingChord`，但 TSF 客户端仅用于按键路由，没有把该状态带给宿主组字处理；此时 Rime 尚无 preedit，首组按键处于无宿主组字标记的窗口。macOS `CompositionSession.swift` 已对 `stagedChordGuardActive` 建立 U+200B 组字保护。

补充修复将 `pending_chord` 带入 TSF 快照：在普通直输的首组待结算期间建立一次宿主专用的 U+200B marked text，松开后用真实 preedit 替换。已有 preedit 优先显示；占位符不进入引擎、提交文字或候选。Escape、失焦和撤销的异步编辑清除保护。

新增 `--chord` 专项采用真实 NamedPipeBrokerClient、Broker、librime 和内置并击映射，所有 `D/F/K/L` 先按下，再逆序松开，对应 `dfkl` → `you'` → `有`。修复前测试退出 1，缺少首组 guard 和异步 guard 的断言失败；修复后 x64 / x86 均退出 0，重新聚焦后的首组、取消、失焦、延迟回调撤销通过。

**模拟宿主在旧实现下仍能最终提交“有”；此次复现的是缺少并击组字保护，不是已复现 Codex 将整组显示为英文。真实 Codex 验证仍待安装新版后完成。**

补充后的两架构各 7 项 CTest 全部通过（此前 5 项，加 `RimesChordProfile`、`RimesBrokerProtocol`）；普通顺序输入的真实 Broker/librime 测试也重新通过。专项入口为 `platforms/windows/tests/Invoke-RimesImeE2E.ps1 -Architecture x64 -Chord`，x86 同理；`-Chord` 与 `-AppContainer` 使用不同输入 fixture，应分开运行。专项把并击间隔设为 300 ms，以免测试机器调度延迟拆散人为模拟的键组，不修改产品默认设置。

日志：`chord-baseline.log`、`chord-x64-tests.log`、`chord-x86-tests.log`、`chord-x64-typing.log`、`chord-x86-typing.log`、`sequential-after-chord-x64.log`、`sequential-after-chord-x86.log`。

## 此前的顺序首键修复

已修复一条可导致首键直接进入宿主的 TSF 路径：`OnTestKeyDown` / `OnTestKeyUp` 原先也申请同步文档读锁；宿主在探测阶段拒绝该锁时，服务会撤销上下文并返回不接管。正常按键分发因此不会再进入对应的真实按键回调。

改动将探测阶段改为按当前输入模式和候选状态判断。真正的按键回调仍须取得读锁并验证可编辑状态和输入作用域，才能向 Broker 发送按键；未通过验证的按键不会延后重放。无可用引擎、英文模式及受保护宿主的行为保留。

这个时序已在模拟宿主中复现并修复，尚不能认定它就是用户在 Codex 中遇到的唯一原因。[Microsoft 的 RequestEditSession 契约](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfcontext-requesteditsession)说明同步请求可能被拒绝；[OnTestKeyDown 契约](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfkeyeventsink-ontestkeydown)用于预测按键是否被接管。

## 验证结果

- 修复前先加入 `RimesFirstKeyTests` 并执行：构建成功，测试退出 1；首键接管、首字母组字、上下文保留及首词中文提交断言失败。
- 修复后 x64 / x86 Release 构建成功。
- 两架构各 5 项 CTest 通过：`RimesFirstKey`、`RimesCaretRegression`、`RimesCandidateUI`、`RimesTsfRuntime`、`RimesBrokerClientContract`。
- `RimesFirstKey` 覆盖首次输入与重新聚焦、探测阶段不可获得读锁、`you` → `有`、英文模式、引擎不可用、只读状态及探测之后保护状态变化。
- 修正 `RimesTsfE2E` 的测试宿主：探测拒绝后不再无条件调用真实按键回调；按键释放也按预测结果分发。
- 两架构真实 Broker + librime 1.17.0 + 模拟 TSF 宿主端到端测试退出 0，包含首次探测拒绝读锁、首字母立即建立组字、`nihao` → `你好`、重新聚焦，以及既有候选、Shift、异步编辑撤销测试。
- `git diff --check` 通过。

端到端测试使用 `RIMES.E2E.Broker` 独立端点、新建 shared/user/log/appdata 目录，以及现有测试运行时；未接入日用 Broker，未修改用户词库和设置。测试 Broker 正常关闭。构建有已有 MSBuild 中间目录路径别名警告，无编译或链接错误。

本次源文件和测试之外的既有工作区改动均保留。没有提交、发布、注册或安装输入法。

## 验证边界

读取正在运行的 Codex 宿主模块确认，仍加载 `1.1.5-win-fix.20261016-823c910b600c-14195a98de75/x64/RimesTsf.dll`。新 DLL 尚未装入该进程。未在真实 Codex 输入框键入测试内容；不能把模拟宿主通过视为 Codex、其他 Electron 客户端或所有有键盘钩子的输入框均已通过。

## 产物

| 架构 | RimesTsf.dll SHA-256 |
| --- | --- |
| x64 | `a91eb447b0045dae1656f203af3d6e607c6939c60244398254ff8b462a82d917` |
| x86 | `6723c6c794d17c56995112ec1c065073e4e3174f7d41398f4a09c606bd605fec` |

DLL 在 `platforms/windows/native/out/build/windows-{x64,x86}/Release/`。
本次日志副本见同目录 `x64-tests.log`、`x86-tests.log`、`x64-typing.log`、`x86-typing.log`。
