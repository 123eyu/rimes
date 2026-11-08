# 空明码：预编辑「平时」后接右手 b+空格 输出「平时不」而非「平时被」

- 日期：2026-11-08
- 环境：XSIME / fcitx5（Linux）、librime 1.17.0
- 方案：`kongmingmas`，激活包 `~/.local/share/rimes/schemes/packages/75f1c5dc30f81cbd685ed329027693c3/`

## 结论

不是词库编码或方案配置错误，也不是 librime 的 bug。原因是 XSIME Linux 前端在候选可见时把空格当作「候选选重键」吞掉，空格没有进入 librime 的 `chord_composer`，于是「n + 空格」和弦退化成只剩 n 的单键和弦：

| 物理按键 | 逻辑码 | 命中字 |
| --- | --- | --- |
| n（右手，对应逻辑 b） | `=b` | 不 |
| n + 空格 | `=_b` | 被 |
| b（左手） | `b=` | 部 |
| b + 空格 | `b_=` | 并 |

因此：空格先把待选的「平时」上屏，随后待定的 n 松开结算成「不」，最终得到「平时不」。

## 证据链

### 1. 方案映射

`kongmingmas.schema.yaml`：

- 第 134–135 行：左手加后缀 `=`、右手加前缀 `=`。
- 第 208 行：`xform/[bn]-/b/`，右手的物理 n 转成逻辑 `b`。
- 第 216–217 行：空格并击单字规则 `$1=` → `$1_=`、`=$1` → `=_$1`。
- 第 115 行：`speller/auto_select: true`，唯一码会立即上屏。

### 2. 词库编码

`kongmingmas.dict.yaml`：

- `不` → `=b`
- `被` → `=_b`（另有 `bP`、`bP,=`、`ip,.`）
- `平时` → `plss`（另有 `p7`），且 `plss` 还是「品是」「平是」的重码，故停留在候选区。

### 3. librime 实测

`/tmp/rimeprobe` 用 librime 1.17.0 + 该方案包直接调用 API：

```
n+空格  -> preedit "=_b"  preview 被
n       -> preedit "=b"   preview 不
b+空格  -> preedit "b_="  preview 并
```

说明 librime 的 `chord_composer` 本身能正确处理空格和弦；问题出在 XSIME 前端没有把空格送进引擎。

### 4. 前端丢弃空格的调用链（Linux / fcitx5）

1. `platforms/linux/chord/core/chord_core.cpp:104-110` `IsChordSchema()` 只认 `my_combo`、`rimes_chord_*` 与 `nativeProfiles` 白名单；kongmingmas 不在其中。
2. `platforms/linux/ime/src/chord/chord_service.cpp:125` `Effective()` 对导入方案直接返回 false（`!imported`）。
3. `platforms/linux/ime/src/fcitx/rimes_ime.cpp:583` 每个按键先走 `state->prepareChord()`，对 kongmingmas 恒为 false。
4. `platforms/linux/ime/src/fcitx/rimes_state.cpp:120` `HandleCandidateKey()` 在 `:151` 的 `ProcessKey()` 之前执行。
5. `rimes_state.cpp:523`：候选可见时 `navigation_` 非空。
6. `rimes_state.cpp:532`：空格进入候选键分支；`:533` `filterAndAccept()` 吞键；`:550` `selectCandidate()` 选重上屏；`:518-520` 连 keyup 也被吞掉。
7. 空格被吞后，librime 只收到 n；n 松开时 `chord_composer` 结算为 `=b`，配合 `auto_select` 立即上屏「不」。

### 5. macOS 路径

`Sources/RimeBuffer/RIMESController.swift:3926-3937`、`:3958` 存在同类的候选窗空格拦截；未在本机验证。

## 修复建议

方向 A（前端，改动最小）：`HandleCandidateKey()` 对「当前方案声明 `chord_composer` 且 alphabet 含空格」的情况放行空格，交给 `ProcessKey()`，由 librime 自行区分：没有待定键时仍按上屏/选重处理；有待定键时并入和弦。

方向 B（方案识别）：把 kongmingmas 这类导入并击方案纳入 chord 方案判定，并在 `prepareChord` 的 chord_key 条件（`rimes_state.cpp:324-326`）中允许空格。

需要注意：放行空格只应针对并击方案，普通方案的空格选重行为不能改变；建议补一条 e2e，覆盖「`plss` 候选可见 → n+空格 → 期望『平时被』」。

## 修复（方向 A，已实现）

- [rime_engine.hpp](../../platforms/linux/ime/src/engine/rime_engine.hpp)：新增 `SchemaSpaceChord(schema_id)`。
- [rime_engine.cpp](../../platforms/linux/ime/src/engine/rime_engine.cpp)：通过 librime `schema_open` 读取 `chord_composer/alphabet`，判断是否含空格；按方案缓存，部署完成后清空缓存。
- [rimes_state.cpp](../../platforms/linux/ime/src/fcitx/rimes_state.cpp)：`HandleCandidateKey()` 对「空格和弦方案」的空格直接放行，回落给 `ProcessKey`，由 librime 的 `chord_composer` 决定并入和弦还是正常上屏/选重。
- 验证：`schema_open("kongmingmas")` 返回 `alphabet=[qazwsxedcrfvtgbyhnujmik,ol.p;/'7890 ]`（含空格）；`rime_ice` 无 `chord_composer`，返回 false。Linux 插件编译通过并安装到 `~/.local/lib/fcitx5/rimes.so`。
- 生效方式：重启 fcitx5（`fcitx5 -r`）后，用 `plss` + n+空格 验证应输出「平时被」。

## 验证结果

- 2026-11-08 在真实 fcitx5 图形界面复现验证：`plss` 打出「平时」后，按右手 n（逻辑 b）+ 空格，输出「平时被」，符合预期。

## 未确认

- 若用户端实际为 macOS，拦截位置不同，但机制等价（未核实）。
