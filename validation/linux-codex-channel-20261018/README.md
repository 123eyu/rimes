# ChatGPT 连接器双端对照

实际 AppKit / GTK 设置页各 10 张 2× 截图：未安装、未登录、可用、等待授权、授权失败 × 墨竹/翡翠，逻辑尺寸 980×680。渲染使用独立配置和公开状态，不读取个人账号或发起模型请求。

[墨竹并排与差异](comparison/night/index.html) · [翡翠并排与差异](comparison/day/index.html) · [来源与验证摘要](provenance.json) · [功能验证](../../platforms/linux/runtime/CODEX-VALIDATION.md)。比较前将内嵌 ICC 转为 sRGB，保留原始分辨率。

已对齐三等分连接器卡、650 宽容器、626×46 状态行、图标底板、单选圆点、授权按钮和五种登录状态。字体度量、字重、替代图标与部分边框仍有差异，报告均为 `requires_review`。

`linux/buffer` 包含五主题的图片完成行、模型/推理深度/Skill 控件及 Mailbox 查看按钮。当前 macOS Buffer 渲染受系统保护态遮蔽，因此没有将占位截图当作原生对照，也没有解除保护。Buffer 和通知浮层的有效双端截图仍归入 #179，不能据此宣称像素一致。

功能日志只含公开测试内容。官方 Linux CLI 的验证是无提示词的隔离探测；文本、登录和图片流程使用本地协议测试进程，不代表真实账号的外部生成已经验收。
