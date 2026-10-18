# Mailbox AI 对话双端对照

[并排图与差异图](comparison/index.html) · [功能与数据验证](../../platforms/linux/mailbox/CHAT-VALIDATION.md) · [来源摘要](provenance.json)

16 组实际 AppKit/GTK 渲染：已有对话、新对话、生成中、失败 × night/day × 1000×700 或 680×500，均为 2×、Asia/Tokyo 时区和同一份公开数据。`native-mailbox.json` 来自生产 AppKit 工作区；`native-chat.json` 来自实际原生请求规划器。原始截图未缩放或润饰；比较前仅按内嵌 ICC 转为 sRGB。

本批修正了 Linux 列表摘要换行、新建对话按钮未显示、发送箭头缺失和模型选择框过宽。生成中正文使用相同的内存预览，未写入持久化会话。

全部对照仍为 `requires_review`：字体粗细和度量、图标、搜索/筛选控件、选择框、禁用按钮样式仍有差异；Linux 还显示明确的停止、重试和删除动作，当前 macOS 基线没有相同控件。这些动作的位置与显隐须在 [#179](https://github.com/scholay/xsime/issues/179) 中统一验收，不能把本报告称为像素一致或实体桌面验收通过。
