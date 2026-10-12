# 万象 v18.1.1 Base 跨平台基础兼容包

产物：`build/input-schemes/delivery/wanxiang-v18.1.1/lingxi-wanxiang-v18.1.1-base-portable.zip`，33,402,176 bytes（31.9 MiB）。

SHA-256：`91041724bfca27080184abad84506a423a5dc751759e5cc734e7d5f8c7ae08be`。

来源是官方 v18.1.1 的 Base 发布资源，校验值匹配 GitHub 发布记录；源提交固定为
`26a9efeb18e604c091f4d3890d00bec723fe755f`。发布资源包含源码归档缺少的 OpenCC 二进制数据。
发布 ZIP 未附 LICENSE，本包从同一固定提交补入 CC BY 4.0 完整文本；manifest 记录逐文件来源和改动。

## 验证结果

| 检查 | 结果 |
|---|---|
| 最终 ZIP CRC、体积、95 项文件与引用检查 | 通过 |
| 当前 iOS `RimeSchemeImportService` inspect / stage | 通过，推荐 1 项主方案及 5 项依赖 |
| iOS 引擎主机切片部署、加载 | 通过，无未处理 Lua 错误；部署 9.9 秒，峰值 1322 MiB，部署后 205 MiB |
| iOS 引擎主机切片输入探针 | 17/17 通过 |
| macOS 随包 librime 1.16.0 + librime-lua 部署、加载 | 通过，无 error/fatal 级日志 |
| macOS 引擎输入探针 | 17/17 通过 |
| 打包与验证工具单元测试 | 38 个通过 |
| iPhone / Windows / Android / Linux 实机 | 未验证 |

探针覆盖：你好候选及空格提交、中国、通繁/港繁/台繁的“经济→經濟”、Unicode、金额、
计算器、日期、版本、部件反查、表情、英文、英译中和符号入口。
macOS 库没有 iOS 的 Lua 错误记录 API，因此桌面验证以错误级日志和实际输出为证据；
不能将其空 `luaError` 字段单独当作成功凭据。

主机数据不等于 iPhone 内存或耗时，尤其尚未确认键盘扩展首次加载的资源占用。
未修改、安装或切换任何日常输入法配置。其他平台灵犀尚无外部 ZIP 导入入口；
跨平台指可供支持外部方案和 librime-lua 的 Rime 客户端使用同一套方案数据。

## 兼容调整

- 修复超级提示 `build/tips`、候选替换 `build/replacer` 在移动沙箱被拒绝后触发的
  `userdb.lua:71: C stack overflow`；缓存使用隔离用户目录内的单一资源名。
- 原生数据库创建失败时返回 nil，避免将空数据库包装成递归索引对象。
- 同步更改 Lua 的纠错源词表路径为打包后路径。桌面交叉验证揭示并确认修复了
  `[super_comment]: 加载失败 dicts/cuoyin.dict.yaml`，最终日志无此错误。
- 词库按导入限制分拆，补齐 OpenCC，生成 Lua 子模块入口和许可/说明。
- 采用固定全拼共同功能集，关闭缺失模型对应的 octagram 上下文建议，移除依赖
  `charset.reverse.bin` 的过滤，以及需要桌面重新部署的 `/zrm`、`/flypy` 等切换入口。
- 仍保留 Lua 的个人上下文学习和普通用户词频，不将它们与 octagram 模型混称。

扩展探索中，直接逐键发送 `/sym.arrow.r.double` 会被上游补全改写为
`/sym.arrowrow.r.double` 并返回“无匹配”；`/sym` 入口已验证，未据此声称完整符号导航已验收。

## 复现

```sh
build/input-schemes/venv/bin/python scripts/input-schemes/scheme_build.py build --only wanxiang
build/input-schemes/venv/bin/python scripts/input-schemes/scheme_build.py verify --only wanxiang --verbose
build/input-schemes/venv/bin/python validation/wanxiang-v18.1.1-20261009/verify-native.py
build/input-schemes/venv/bin/python -m unittest discover -s scripts/input-schemes -p 'test_*.py'
build/input-schemes/venv/bin/python scripts/input-schemes/scheme_build.py index
```

首次配置 Python 环境参照 `Catalog/input-schemes/README.md`。引擎验证需要仓库已有的
`Vendor/ios-build/host/rime` 和 `Vendor/rime/Frameworks`。产物、工具和引擎哈希见
`artifact.json`；完整输出见 `ios-host-verification.json`、`macos-engine-verification.json`。

## 用户反馈的展开大小/压缩比提示（2026-10-09）

使用最终交付 ZIP（SHA-256 仍为 `91041724bfca27080184abad84506a423a5dc751759e5cc734e7d5f8c7ae08be`）
重新运行当前 importer 及历史提交 `796b725`、`958c030` 的 importer，三次均通过 inspect。
95 个条目共展开 91,458,710 bytes，最大单文件 25,165,950 bytes，最高条目压缩比约 7.52 倍。

官方原包 `rime-wanxiang-base.zip` 在这三次对照中均精确复现
“ZIP 超过展开大小或压缩比限制。”：`dicts/jichu.dict.yaml` 为 45,048,310 bytes，
超过 33,554,432 bytes 的单文件上限。交付包已将该词典拆为两份。

用户实际选择的 ZIP 和应用版本尚未确认，因此这里只确认原包可复现、交付包不可复现，
不将用户这次报错定性为误选原包，也不称真机故障已修复。
完整检查输出见 `size-rejection-diagnostic.json`。
