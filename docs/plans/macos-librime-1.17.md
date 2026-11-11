# 计划：macOS librime 1.16.0 → 1.17.0

状态：已实现 `scripts/fetch-rime.sh` 与发布参考文档的修改，未提交。2026-11-11 基于 `d8988b9c` 与 librime 官方 1.17.0 Release 资产的实际检查写成。

实施中发现：官方 dylib 只有 arm64 切片带链接器 ad-hoc 签名，x86_64 切片无签名；因此脚本不检查来源签名，改为复制后对四个 dylib 重新 ad-hoc 签名（下文“实施步骤”第 1 条中的 `Signature=adhoc` 检查已取消）。

已验证（本机 Apple silicon，调试构建，按发布流程组装 SharedSupport）：`smoke`、`user-lexicon-bridge-smoke`、`chord-keymap-engine-smoke`、`chord-ziranma-engine-smoke`、`yoyo-engine-smoke`、`external-schema-engine-smoke`、`candidate-pagination-smoke`、`scheme-import-smoke` 全部通过；部署后的 `installation.yaml` 记录 `rime_version: 1.17.0`。真实学习词库：把由 1.16.0 写入的 `rime_ice.userdb` 复制到临时目录，用官方 1.17.0 `rime_dict_manager` 导出 34,784 条；导入到新库再导出仍为 34,784 条，词与编码无差异。尚未验证：Intel 机器、正式签名与公证、覆盖安装后的实际输入。

## 目标

macOS 与 iOS、Windows、Android 使用同一 librime 1.17.0（`33e7814`），不再等待 Squirrel 发版。用户学习词库、已导入方案与 Octagram 模型保持不变。

## 现状与检查结论

| 项目 | 当前（Squirrel 1.1.2 pkg） | 官方 `rime-33e7814-macOS-universal.tar.bz2` |
|---|---|---|
| librime | 1.16.0 | 1.17.0（`33e7814`） |
| 插件 | lua / octagram / predict | 同三个；lua `ec52e48`、octagram `dfcc151`、predict `920bd41` |
| 架构 | arm64 + x86_64 | arm64 + x86_64 |
| 外部依赖 | 仅 libSystem、libc++ | 相同（静态链接，`@rpath/librime.1.dylib`） |
| 最低系统 | 13.0 | 11.0 / 10.15（低于本项目 13.0，无影响） |
| LevelDB | 无 Snappy | 无 Snappy（userdb 格式兼容） |
| 签名 | Developer ID，Team `28HU5A7B46`，已公证 | **仅 ad-hoc**，无 Team ID、无公证 |
| SharedSupport | 包含默认方案、词典、OpenCC 数据 | **不包含** |
| 包大小 / SHA-256 | 25,498,033 B / `6147…8287` | 3,999,581 B / `11d8dc66…1850` |

解包后需固定的 SHA-256：

```
1adb725a081b0accc1451034f6f1bb78cbfe8252a65245f1511f4b3f2038c487  lib/librime.1.17.0.dylib
9e5086f73b90481530bb2aa08a615044c90242eee4b98eef12fcdd1a1c391e68  lib/rime-plugins/librime-lua.dylib
5b2e07a8faa2ab68f551c9de2ce9c3a4925f533c1369a2cc598ee8c486f13a6c  lib/rime-plugins/librime-octagram.dylib
bacf19a2931fb83c4ebb9e14e231dd3d7fc110fd67526018fad0a514c68366a6  lib/rime-plugins/librime-predict.dylib
```

`librime.1.dylib` 与 `librime.dylib` 是指向 `librime.1.17.0.dylib` 的符号链接；fetch 拒绝符号链接，因此复制实体文件并命名为 `librime.1.dylib`。

## 方案：二进制取自官方 librime，数据仍取自 Squirrel 1.1.2

- **dylib**：来自 librime 官方 Release，按包长度、包 SHA-256、四个 dylib SHA-256 与 `version-info.txt` 中的提交号 fail-closed 校验。
- **SharedSupport**：继续从已审计的 Squirrel 1.1.2 pkg 提取（保留现有 Developer ID、公证与 SHA 校验）。这些是与引擎版本基本无关的 YAML、词典和 OpenCC 数据；`build_install.sh` 与 `release.yml` 本来就会删除其中的 `*.schema.yaml` 并覆盖为 `rime-data/`。
- **信任锚变化**：原先依靠 Squirrel 的 Developer ID 与公证；改为依靠固定 SHA-256 + `rime/librime` 官方 Release。最终分发仍由 RIMES 自己的签名与公证覆盖（`codesign --deep` 会替换嵌套 dylib 的 ad-hoc 签名）。此变化需维护者明确接受。

不选的方案：

- 等 Squirrel 新版：发布时间不可控，目前没有 1.17 对应版本。
- 自行从源码构建 macOS librime：可控性最高，但需要维护 boost、OpenCC、LevelDB 等工具链；可作为后续阶段，复用 iOS 的 `dependencies.lock.json` 思路。
- 使用 master 或 nightly：领先 1.17.0 共 87 个提交，2026-11-11 刚把 glog 换成 spdlog，尚未发版。

## 实施步骤

1. **`scripts/fetch-rime.sh`**
   - 新增 `LIBRIME_VERSION=1.17.0`、`LIBRIME_COMMIT=33e7814`、资产名、字节数、SHA-256 与四个 dylib 哈希。
   - 下载限定 GitHub HTTPS 主机，沿用现有重试和缓存（`Vendor/.cache`）规则。
   - 校验 `version-info.txt` 的四个提交号；校验 `rime-plugins/` 恰好三个文件；逐个 `lipo -verify_arch` arm64 和 x86_64。
   - Squirrel 部分只保留 SharedSupport 来源；删除对其 dylib 哈希与 Team ID 的要求，改为对 librime dylib 要求 `Signature=adhoc`。
   - 结束输出改为 `完成（librime 1.17.0 + Squirrel 1.1.2 数据）`。
2. **`build_install.sh`、`.github/workflows/ci.yml`、`release.yml`**：路径不变，无需修改复制逻辑；确认发布签名步骤对嵌套 dylib 的 Team ID 检查仍以 RIMES 身份为准。
3. **文档**：更新 [docs/release/reference.md](../release/reference.md) 第三节（来源、信任锚、升级流程）；在 `THIRD_PARTY` 或 NOTICE 中记录 librime 1.17.0 与三个插件提交；在 CHANGELOG 中写明 macOS 引擎升级。
4. **`CRimeBridge.cpp`**：无需改 API。但找不到 bundle 内 librime 时仍会回退到系统 Squirrel（1.16），应在日志或诊断中显示实际加载的版本，便于排查。

## 验证

- **自动**：`bash -n`；`fetch-rime.sh --force` 两次（第二次走缓存）；篡改一个字节时必须失败；`release.yml` 已有的 `smoke`、`scheme-import-smoke`、`user-lexicon-bridge-smoke`；`WubiEngineSmoke`、`YoyoEngineSmoke`、并击与自然码 smoke；`swift test`。
- **用户数据**：用 1.16.0 产生的现有 userdb（先备份）启动 1.17.0，确认学习词仍进入候选；导出/导入 TSV 往返条目数一致；`LOG` 无新错误。
- **方案**：rime-ice、wanxiang、wubi86 与含 Lua 的方案重新部署并实际输入；关注 1.17 改动处：排除词（原崩溃点）、数字分隔符后的空格、`charset_filter` 对符号的过滤、方向键跨分隔符移动、多句候选数量。
- **安装**：按本地 pkg 安装流程覆盖系统副本，在 Intel 与 Apple silicon 上各输入一轮；确认辅助功能授权情况。
- **发布**：正式签名与公证通过，`spctl` 评估通过。

## 风险与回退

- 候选排序或多句候选变化影响用户习惯：在更新说明中提示；如反馈集中，可在默认配置中限制句子候选数。
- 回退：恢复 `fetch-rime.sh` 的上一个提交并重新 `--force`；userdb 不随引擎降级改变格式，但回退前仍建议导出 TSV 快照。

## 后续

- librime 1.18 发布后评估其 userdb 损坏恢复（#1260）与词典编译、查询性能改进，这与 [user-dictionary-recovery.md](../user-dictionary-recovery.md) 直接相关。
- 将五个平台的 librime 版本集中记录在一处，升级时一起进行。
