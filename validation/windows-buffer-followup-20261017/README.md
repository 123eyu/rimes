# Windows Buffer 快捷键、边缘与翻译修复 · 2026-10-17

本轮工作保留既有首页、测速页防闪烁和本机持久化数据。

## 更改

- Buffer → 快捷键增加上一个 / 下一个缓冲插件，默认 Alt+Shift+↑ / ↓。支持原生录入、手工修改、Esc 取消、Delete 恢复单项默认、恢复全部默认、冲突检查与保存重开。
- Broker 执行、组字收束、TSF 按键预测共享配置。仅在 Buffer 捕获且精确目标有效时切换，沿已启用插件顺序循环，保留原文，不发送；长按只切换一次，修饰键提前释放仍消费匹配松键。
- 四项快捷键保持独立。旧文件缺少新增字段时补默认；如默认被原有自定义唤出/发送占用，则保留原绑定并为新增项选择无冲突修饰键。
- Buffer 改为预乘 alpha 离屏绘制后整帧呈现，取消二值 HWND 圆角区域裁切。目标应用图标按屏幕 DPI 提取，并在同一帧合成，防止等待翻译刷新时闪烁。
- 候选窗口同样使用平滑透明外缘；设置、插件和周边共用的圆角按钮/边框改为抗锯齿绘制。GDI+ 生命周期由原生 UI 对象持有，避免 DLL 静态析构中启动/关闭。
- 本地中英翻译复用私有 Python 工作进程与最多两组方向模型。每次仍重新翻译本次原文，不缓存正文或结果历史，不改模型、beam size 或质量设置。进程保留模型权重，取消、超时、坏协议、Broker 退出会退休进程；所有子进程进入 kill-on-close Job，stdio 仅继承三个允许的句柄。

## 验证边界

原生测试使用自有窗口、隔离设置目录与固定测试句；未向用户编辑器合成输入，未调用 AI 连接器或上传文本。截图是生产绘制器和原生控件的可复现 fixture，不是用户桌面截图，也不是 macOS 真机对比。

图形测试覆盖 96/120/144/192/240 DPI、四套主题、透明角点和部分透明像素、预乘 alpha、反复改变等待状态时关联图标像素一致、整帧呈现不激活窗口。设置测试覆盖四项控件在最小/默认窗口不重叠，并保留原有候选点击/焦点测试。

本机固定句“今天我们去公园散步。”的原生完整路径：首次约 3000 ms，后续 188 / 156 ms。构建期间的辅助 Python 对照：每次重载约 4819 / 11942 ms，复用后 199 / 206 ms。样本仅解释重复模型加载开销，不代表任意文章或设备的性能保证，首次加载仍需时间。

本次修改 TSF DLL，因此安装后的设置、Buffer 绘制和翻译由新 Broker 立即生效；已经加载旧 DLL 的应用需要重新启动，注销后所有应用统一使用新快捷键路由和候选外缘。安装脚本不会自动注销或关闭用户应用。

其余 Windows 后台与 macOS 的广泛页面对齐审查保持待办。

## 最终结果

- x64 / x86：Surface、Runtime、CodexConnector（含本地复用进程）、CandidateWindow、Settings、SettingsUi、BrokerClientContract，全部通过。
- 最后窗口尺寸/DPI 失效处理修改后，两种架构的 SettingsUi 再验证通过。原生测速/插件页面测试两种架构均 PASS，EN_UPDATE/定时器引发的主动重绘数为 0。
- 本机安装版本 `1.1.5-win-buffer-followup.20261017`，源快照 `92630f3288f0ed8ac9c927d789aa818b6e0620b79b1f47c7f70bc03bde860432`。
- 新 Broker 唯一运行；Broker、两架构 TSF、翻译脚本与此次构建/源码一致；设置与测速成绩保留。详细收据见 `installed-result.json` 和 `post-install-check.json`。
- 仍需注销一次，使已有应用统一加载新 TSF DLL。

透明合成实现依据 Microsoft 的 [UpdateLayeredWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-updatelayeredwindow) 与 [Direct2D 像素格式和 alpha 模式](https://learn.microsoft.com/en-us/windows/win32/direct2d/supported-pixel-formats-and-alpha-modes) 文档。
