# XSIME 命名与兼容规范

| 用途 | 名称 |
| --- | --- |
| 简体中文显示名 | 犀术IME |
| 繁体中文显示名 | 犀術IME |
| 英文及其他语言默认显示名 | XSIME |
| 项目与架构名称 | XSIME |
| 仓库 | `scholay-xsime/xsime` |
| 官网 | `https://xsime.com` |

品牌标语为“心有灵犀，落笔见术”（繁体“心有靈犀，落筆見術”）。标语中的“灵犀”保留原文。Rime / librime 是上游引擎名称，保持原名、版权和来源说明。RIMES、灵犀输入法、Lingxi IME 只用于兼容标识或历史迁移说明。

## 展示名称与兼容身份分开

品牌调整不能改变以下内容：

- macOS Bundle ID、输入源 ID、IMK 控制器类、`RIMES.app` 和 `RIMES` 可执行文件名。
- iOS Bundle ID、App Groups、Keychain Groups；Android applicationId 与长期签名。
- Windows CLSID、注册表键、协议和安装所有权标记；Linux 包名和 addon ID。
- 用户数据路径、偏好键、凭据、词库、方案 ID（包括 `lingxi_wubi`）与插件 ID。
- 已发布的资产名、版本、摘要、Release 与原始提交。

macOS 连接名使用 `com.scholay.inputmethod.isaac_Connection`，遵循既有 Bundle ID 加 `_Connection` 的约定；以 [Info.plist](../../Info.plist) 和运行时入口共同校验。

不要全仓无差别替换 `rimes`、`RIMES` 或 `lingxi`。固定版本的官方插件需在其源码仓库修改并重新锁定，不能直接改本体中的生成副本。第三方名称和词典中的自然语言词条保持原义。

## 仓库与更新兼容

更新器使用固定仓库身份，并对官方新旧仓库 URL、包身份、版本和签名进行校验。旧版本内置更新器可能拒绝仓库重定向；受影响用户可从官方 Release 手动升级一次，保留 `~/Library/RIMES` 等用户目录。

本体仓库位于 `scholay-xsime/xsime`，官方插件位于 `scholay-xsime/xsime-plugins`。五端仍在本体的单一 `main` 集成；Organization 不改变客户端、插件与用户数据的职责边界。

不重新建立 `scholay/rimes`、`scholay/xsime` 或 `scholay/rimes-plugins` 占用旧地址，这会破坏 GitHub 仓库与下载重定向。已安装客户端的严格 URL 校验仍依赖旧地址，因此正式签名更新清单和插件目录继续使用旧的 `/releases/download/` 路径；发布 API、CI 仓库检查、源码链接和新客户端的更新清单入口使用新地址。不得把下载兼容路径当成遗漏而统一替换，亦不得改写已发布清单、资产或签名。

GitHub Pages 不随仓库转移重定向。`scholay/scholay.github.io` 兼容站保留 `/rimes/ios/` 及 `/xsime/`、`/xsime/ios/` 的公开页面；新项目 Pages 地址为 `https://scholay-xsime.github.io/xsime/`，产品官网继续使用 `https://xsime.com`。已发布的商店帮助、隐私路径、安装包身份和上游版权链接按原用途保留。

商店、TestFlight 和备案名称按各渠道实际流程更新。源码中的展示名称不能作为商店已更新的依据，也不能改写旧发布记录中的事实。
