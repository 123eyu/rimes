# 日常统计双端对照

[并排图与像素差异](comparison/index.html) · [布局读数](geometry.json) · [来源与验证](provenance.json) · [实现说明](../../platforms/linux/statistics/README.md)

8 组实际 AppKit 与 GTK 渲染：每日、历史 × night/day/quiet/rasta。使用同一组公开的原生统计数据、2026-10-18 日期、Asia/Tokyo 时区和 2 倍缩放。每日页为 698 × 738 逻辑像素，历史页为 698 × 1162。参考图直接来自当前 macOS 产品控件；Linux 直接捕获 GTK 控件，按图片的 ICC 信息转换到 sRGB 后生成差异图。

整页、四指标卡片、趋势、键盘热力图、历史日历和最近会话图的位置与尺寸全部匹配，原始几何读数保存在各图片的 `.geometry.json`。这只确认布局，不等于像素验收：字体、字重、图标、日期选择器、复选框及其余控件细节仍有差异，完整悬停、选择、错误、禁用和滚动状态尚需扩充。所有对照保留 `requires_review`，继续在 [#179](https://github.com/scholay/xsime/issues/179) 跟踪。

独立 GTK 交互回归已验证日期/图表、采集选项、离线读取、清空取消、私有导出和明确修复。实际 Fcitx 输入及包生命周期日志另存 `logs/`。日志仅包含测试样例；不包含个人输入文本、真实账户或词库。
