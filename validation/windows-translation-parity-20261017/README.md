# Windows 实时翻译与 macOS 对齐验证

本次修改 Windows 原生 Buffer 实时翻译。以当前 macOS 源码为行为基准，保留 Windows TSF 投递与确认流程；未修改 macOS 实现。

## 对齐内容

| 项目 | 原 Windows 行为 | 本次行为 |
| --- | --- | --- |
| 实时触发 | 等待 800 ms 且必须有句末符号 | 停顿 300 ms 翻译未完成句；持续输入最长等待 900 ms |
| 语言方向 | 仅在设置中填写目标语言，本地服务根据汉字猜测原文语言 | 工具栏选择原文和目标语言、交换方向；请求冻结显式语言对 |
| 尾句续写 | 已翻译前缀可能被保留并把新增文字单独翻译 | 续写替换该尾句的旧结果；稳定前段保持可用 |
| 译文块 | 主要按句子分块 | 长译文按小块展示及发送，保留空格、换行与全部正文 |
| 发送生命周期 | 首块确认后退休源文，部分模式编辑会丢失剩余译文 | 保留首块确认后的剩余译文；语言切换和其他模式的编辑不得恢复已退休源文或丢弃这些译文 |
| 朗读 | 无入口 | 工具栏开关；点击译文块、发送确认时朗读该块；使用该块原有语言，保护态/关闭开关取消 |
| 空白状态 | 可能只显示不可编辑的空结果轨 | 显示可输入、可关联的原文轨 |
| 工具栏与状态 | 缺少语言控制 | 原文语言、交换、目标语言、朗读与自动发送按顺序排列；窄窗口隐藏放不下的普通状态，完整状态可在更多按钮提示中查看；无结果时等待/失败消息显示在结果轨 |
| 设置 | 只有目标语言 | 原文、目标语言均可保存；旧配置从目标语言迁移合理的默认源语言 |

参考实现：

- `Sources/RimeBuffer/AppleTranslationPlugin.swift`：300/900 ms 刷新策略、显式语言对、原文单元与待发译文生命周期。
- `Sources/RimeBuffer/TranslationSourceUnit.swift`：源文与展示小块分离、空白保留。
- `Sources/RimeBuffer/BufferWindowController.swift`：语言控件、交换与逐块朗读。
- `Sources/RimeBuffer/SemanticBlockSegmenter.swift`：约四个英文词、48 字符的小块目标。
- `ARCHITECTURE.md` 页首翻译生命周期覆盖：精确投递、已退休原文不重放、保护态不投递。

## 验证

构建使用本机 CMake 4.x、VS 2022 Build Tools、Release /W4 /WX，分别执行 `cmake --build --preset windows-x64-release --parallel 8` 和 `windows-x86-release`。

最终结果：

| 检查 | 结果 |
| --- | --- |
| x64 / x86 Release 构建 | 均退出 0；Broker SHA-256 见 `build-info.json` |
| x64 本次相关测试 | 6/6 通过，23.39 秒 |
| x86 本次相关测试 | 6/6 通过，20.81 秒 |
| x64 全量回归 | 29/32 通过；Codex 中文路径失败使用 ASCII 路径单独复跑通过 |
| x86 全量回归 | 30/32 通过 |
| 剩余两项单独复跑 | 候选窗口与方案导入仍分别退出 1，保留原始日志 |
| 真实本地翻译 | 三个正文用例、两个非法语言对用例及 `zh-Hans → en-US` 别名用例通过 |
| 生产界面离屏导出 | 退出 0；检查窄窗口、四个主题与不同 DPI |
| `git diff --check` | 通过 |

针对本次改动运行 `RimesRuntime`、`RimesWorkbench`、`RimesBufferLayout`、`RimesSettings`、`RimesConnections`、`RimesSettingsUi`。测试覆盖无句号翻译、300/900 ms 调度、编辑尾句、稳定前段、无绑定目标时的显式粘贴、语言切换后的迟到结果拒绝、语言切换使旧复制票据失效、发送中的语言冻结、首块确认后的剩余译文、逐块朗读队列、重复确认不重读、最后一块发送后关闭与保护态停止朗读。

生产绘制器离屏导出四个主题、96/144/192 DPI、520/680/760 DIP 宽度，检查语言按钮、两条内容轨、右侧操作组以及设置页。窄窗口状态文字曾被直接裁切，已改为隐藏放不下的普通状态；设置页新增输入框的边框也已补齐。精选图片见 `renders/`。

真实原生 `RimesLocalTranslationProbe.exe` 使用本机已安装的中英模型验证：

- 中文完整句 → English：`It's a nice day. We'll go for a walk in the park this afternoon.`
- 英文完整句 → Chinese：`请在关闭窗口前保存文档。`
- 中文无句号短句 `我想回家` → English：`I want to go home.`
- 同源/目标语言、不支持的源语言均失败且不返回原文作为译文。
- `zh-Hans` 和 `en-US` 配置别名在原生 provider 中规范化后同样可用。

详细命令输出随本次验收保存。所有测试用例采用隔离配置；真实模型探针只读取已准备好的模型。没有改写用户设置、词库或凭据。

## 验证边界

- Windows 本地引擎目前支持中文/英语双向翻译；其他语言使用已有 AI 连接器。Apple Translation 的语言覆盖、分词及音色不属于 Windows 引擎，不能保证译文或像素完全相同。
- 朗读使用 Windows 已安装的匹配语言语音。代码按 [Microsoft SPEAKFLAGS 文档](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ms717252(v=vs.85)) 使用异步、替换上一块、纯文本标志。已验证队列、语言及保护逻辑，未做真人听音验收。
- 本机全量测试中，候选窗口所有权断言及方案导入的非法输入断言仍失败；这两个模块不在本次修改范围。x64 Codex 连接器测试在中文路径参数下退出，用已有 ASCII 路径 `C:\work\rimes-source` 执行后通过，x86 同项通过。
- 离屏图和测试不等同于当前安装包的真实宿主打字验收。本次未替换 `C:\Program Files\RIMES` 中正在运行的日用版本，未注册输入法或发布安装包。
