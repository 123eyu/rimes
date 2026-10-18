# Mailbox 终端双端对照

[并排图与差异图](diff/index.html) · [功能、数据和安装验证](../../platforms/linux/mailbox/TERMINAL-VALIDATION.md) · [来源摘要](provenance.json)

20 组实际 AppKit/SwiftTerm 与 GTK/VTE 渲染：空会话、运行中、文件预览、已收取、退出状态 7 × night/day × 1000×700 和 680×500，均为 2×、Asia/Tokyo 时区。两端运行同一条公开 Shell 命令，并实际预览、收取同一份公开文本；收件关联当前会话 UUID。

macOS 先捕获实际 AppKit 工作区；SwiftTerm 子视图会清除父级缓存中的终端区域，因此单独捕获实际 SwiftTerm 视图，再按其原生 frame 和背景合成。没有重画终端文字或重建字形。Linux 直接捕获实际 GTK/VTE 控件。比较前仅按内嵌 ICC 转为 sRGB，保留原始图片。

本批修复宽窄切换时隐藏面板仍占空间的问题；窄窗口中的终端重新获得整个内容宽度，空页提示不会撑大窗口。浅色主题下空页仍保留 macOS 的独立深色终端背景。真实 GTK 回归同时检查窗口和 VTE 尺寸，以及 PTY 的 SIGWINCH。

20 组对照均为 `requires_review`。仍可见字体字重和度量、下拉框、分段按钮、图标、滚动条和终端光标差异；颜色、间距和全部交互状态继续归入 [#179](https://github.com/scholay/xsime/issues/179) 统一验收。这些截图不能代表 GNOME/KDE 实体桌面、中文 IM 组合输入或真实 Codex/Claude 账号运行已通过。
