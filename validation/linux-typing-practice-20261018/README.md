# 文章练习双端对照

[32 组并排图与差异图](comparison/index.html) · [288 项几何读数](geometry.json) · [来源与验证](provenance.json) · [使用说明](../../platforms/linux/typing-practice/README.md)

实际 AppKit 与 GTK 控件，中文/英文 × 准备/进行/完成/历史 × night/day/quiet/rasta。公开文章、聚合、比较身份和历史数据一致；页面宽 698，准备/进行高 831、完成高 961、历史高 620，全部按 2 倍缩放输出。macOS 图片带有的 ICC 信息在生成对照时转换到 sRGB，原始图不变。

整页、四指标卡、原文与编辑区、进度和趋势图的 288 个读数检查通过，允许三列等分卡片及进度宽度的半逻辑像素取整差。字体度量、CJK 换行、字重、图标、下拉控件和完整交互状态仍有差异。所有图片保留 `requires_review`，不能用尺寸匹配或相似度宣称像素级验收完成。

独立功能测试覆盖真实 GTK IM module 与 Fcitx、Fly/Yoyo 批次、原生成文、编辑器交互、后台成绩和 deb 生命周期。Xvfb 是容器显示，不是 GNOME/KDE、多屏或 Wayland 真实桌面的验收结果；后者继续归入 #179。
