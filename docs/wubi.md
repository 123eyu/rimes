# 五笔 86 与犀术五笔

macOS、iOS、Android、Windows、Linux 均内置这两套方案，复用原有五笔 86 码表及学习词库。

| 方案 | 用法 |
|---|---|
| 五笔 86 | 正常输入五笔码；忘记编码时，先按 `z` 再输入全拼，如 `zni` 查“你”、`znihao` 查“你好”。拼音候选后以括号提示五笔码，如“你好 (wqvb)”。 |
| 犀术五笔 | 直接输入五笔码或全拼，例如 `wq`、`ni` 均可找到“你”，`nihao` 可找到“你好”。无需切换输入模式；相同字母输入的完整五笔匹配优先。需要只查拼音时也可加 `z`。 |

候选选择、翻页、退格与原方案一致。括号内的编码是注释，不随汉字上屏。混输遵循 Rime 常见的五笔·拼音方案方式：普通字母同时送五笔与全拼检索，并非自动识别一串任意拼接的五笔码和拼音。

两套方案均支持**四码唯一自动上屏**：输入四个五笔字母后，候选只有一个且确为五笔词条时，直接上屏；重码时保留候选。拼音候选和 `z` 引导的查字不会触发自动上屏，长拼音也不会被四码限制截断。

macOS、iOS、Android、Windows 的候选注释使用较小字号。Linux 使用 Fcitx 原生候选面板：5.1.9 及以后版本提供独立注释样式，实际字号由面板主题决定；较旧的 Fcitx 保留同行编码提示。

macOS、iOS、Android、Windows 在设置的输入方案列表中选择；Linux 在 Fcitx 的 RIMES 方案菜单中选择。第三方方案仍通过原来的导入入口管理，本次不会替换已导入的方案、重置已选方案或清空学习记录。

五笔码表来自 [rime/rime-wubi](https://github.com/rime/rime-wubi)，混输触发方式参考其 [wubi_pinyin](https://github.com/rime/rime-wubi/blob/master/wubi_pinyin.schema.yaml)。码表、来源及 LGPL-3.0-only 许可继续保留，详见[来源说明](../rime-data/licenses/rime-wubi-SOURCE.md)。
