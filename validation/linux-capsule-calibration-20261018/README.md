# Capsule 表单视觉校准（2026-10-18）

对应 #179，基于 #213 的源码继续校准；不是完整像素验收，也不是已发布安装包。

`macos/` 和 `linux/` 为两端生产控件的实际渲染：相同四条公开笔记、四个主题、940×660 逻辑尺寸、2× 缩放、第一条选中、窗口未激活。每张 PNG 旁的 JSON 记录实际控件位置、窗口状态、缩放和明暗外观。AppKit 同时记录 frame 与 alignment rect，避免把原生按钮的外绘制边距误算成布局偏移。Linux 使用 Xfwm4；裸 Xvfb 不遵守不获取焦点的提示，不能作为相同窗口状态的证据。

本轮修正：

- 表单标签行高按实际 AppKit 测量值设置，标题、摘要、笔记类型和正文的顶边均对齐；正文原先向下偏移 18 个逻辑像素，现为 0。
- 面板边框改为内部绘制，避免 GTK 边框挤占表单布局；正文使用 8 像素上下边距、13 像素左右边距。
- 输入框、搜索框和下拉框使用原生主题的合成填色，下拉按钮补主题色指示器。新建按钮为 57×20，保存按钮为 40×20；页头按钮居中，避免被拉伸到整行高度。
- 预览入口显式选中第一条，并输出坐标；四主题渲染脚本拒绝不同尺寸、缩放、明暗外观、选中状态或窗口激活状态的结果。macOS 预览现在像产品窗口一样指定 `RimeUI.appKitAppearance`，修正浅色主题继承系统深色外观、原生控件文字变白的问题。

验证：Xcode Swift debug 构建与 Capsule window/module/rail 三项 smoke 通过；Linux manager、password UI、capture controller、note editor 四项 CTest 通过。Manager 测试包含 940×660 → 620×430 → 940×660 调整、未保存草稿、冲突、插件授权、富文本等既有回归。日志见 `macos-checks.log`、`linux-checks.log`。

逐控件前后偏差见 `controls-diff.json`，色彩管理后的并排图和放大差异图见 [对照页](compare/index.html)。四组仍为 `requires_review`：字体/中文字形、正文行距、内联按钮、部分图标、半像素布局及底部操作位置仍有差异。Linux 的密码迁移入口仍在底部，macOS 在卡片菜单中；本轮未删除该功能。字体是否改为双端统一的可分发开源字体，等待维护者选择。

复现 Linux 渲染（需 GTK、Xvfb、Xfwm4、xprop 和 dbus-run-session）：

```sh
python3 platforms/linux/validation/render_capsule_manager.py \
  --binary platforms/linux/ime/build/rimes-capsule --output /tmp/capsule-manager-render
```

macOS 使用 Xcode 构建后的隔离预览 app，设置独立的 `RIMEBUFFER_USER_DIR`、`RIMEBUFFER_LOCAL_DATA_ROOT`、`RIMEBUFFER_PLUGIN_ROOT` 和对应的 `RIMEBUFFER_APPEARANCE_MODE`，运行 `capsule-manager-preview <theme>.png`。仅写示例资料，不操作真实词库、笔记或系统输入法安装。
