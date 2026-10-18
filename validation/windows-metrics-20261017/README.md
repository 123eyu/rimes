# Windows 内置扩展、插件橱窗与安装验证

2026-10-17，已安装 `1.1.5-win-metrics.20261017`。

## 实现

内置扩展包含并击、打字测速、统计；Buffer、Capsule、Mailbox 保持同级宿主。插件页按 macOS 的 Mailbox、Capsule、内置扩展、Buffer 顺序展示三列卡片，可查看全部或分类筛选。卡片提供详情、设置、启停，以及可安装插件的安装和卸载。状态来自真实插件注册记录，停用内置扩展后侧栏同步隐藏。

测速使用 macOS 八篇原文、字符对齐和计数口径；提供文章跟打、实时反馈、有效速度、纠错/回退、并击、过程与最终正确率、成绩与输入环境分组。统计提供每日、历史和共用的测速成绩页，支持趋势、键位分布、日历、日期选择和独立记录开关。详见 [实现说明](../../platforms/windows/native/METRICS.md)。

原生窗口验证额外修正了首次打开设置的 150% DPI 裁切、切换文章残留状态、同文选择替换漏计、日期控件主题和禁用扩展后的键盘导航。

## 验证结果

| 验证 | 结果 |
| --- | --- |
| x64 相关 CTest：FirstKey、Metrics、Peripherals、Runtime、OfficialPlugins、Workbench、ChordProfile、SettingsUi | 8/8 通过 |
| x86 同一组 CTest | 8/8 通过 |
| x64/x86 原生 UI 控件、输入排除、同文替换、文章切换、插件启停、记录重载 | 均通过 |
| 四套主题、10 个分页、顶部与滚动位置 | 每架构导出 80 张 PNG，本目录保留代表页面 |
| x64/x86 独立 Broker + 真正 librime + TSF 首组并击、中文提交、取消组字 | 均通过 |
| 官方插件包校验测试 | 7/7 通过（Python UTF-8 模式） |
| 官方源码导入测试 | 5/5 通过 |
| 插件锁文件、目录同步、八篇文章投影 | 通过 |
| 安装清单、两种 TSF DLL、运行中的 Broker 摘要 | 与构建产物一致 |

UI 图片和隔离测试使用虚构的统计记录；没有读取或向 Codex 等用户输入框输入内容。真实 Codex 首字行为仍需在完全重启 Codex 后人工复测。

## 安装信息

- 安装目录：`C:\Program Files\RIMES\versions\1.1.5-win-metrics.20261017-823c910b600c-86d0b257d601`
- Broker：已从上述目录启动，核验时只有一个实例。
- 备份：`C:\Users\young\AppData\Local\RIMES\backups\before-metrics-20261017-145434`
- 设置文件摘要不变，`my_combo` 并击方案、中文模式、自启动和用户词库保留。
- 包内 `LOCAL-SOURCE.json` 与官方插件源码归档记录此次工作树构建。
- 官方插件依赖：`1dc29a44fc807363726d20e97eb33753a58c549d`；本地分支 `codex/windows-metrics-20261017`。依赖的两个提交尚未推送；发布主仓库引用前须先发布相应依赖提交。主仓库未提交已有工作树修改。

安装器仍保留之前的 `requiresSignOut` 提示，表示已有宿主可能继续映射旧 TSF DLL。新版设置与插件已随 Broker 重启生效；Codex 中测试首字前需完全退出并重新打开 Codex。此次没有注销 Windows，也没有强行关闭用户的应用。

## 代表页面

- [插件全部](0-4-0-top.png)、[插件全部滚动位置](0-4-0-scroll.png)、[内置扩展](1-4-4-top.png)
- [打字测速](1-9-0-top.png)、[统计历史](0-10-1-top.png)、[每日统计](2-10-0-top.png)、[测速成绩](1-10-2-top.png)
