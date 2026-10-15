# XSIME 命名与兼容规范

2026-10-15 确认。本规范区分目标品牌、已发布状态与下一轮迁移，不声称商店或备案已经更名。

| 用途 | 名称 |
| --- | --- |
| 简体中文显示名 | 犀术IME |
| 繁体中文显示名 | 犀術IME |
| 英文及其他语言默认显示名 | XSIME |
| 项目、架构名称 | XSIME |
| 目标仓库路径 | `scholay/xsime` |
| 当前运营主体 | 个人；公司运营迁移另行办理 |

IME 表示 Input Method Editor。Rime / librime 是上游输入引擎名称，保持原名、版权及来源说明。RIMES、灵犀输入法、靈犀輸入法、Lingxi IME 仅用于历史和迁移说明，不再作为新增产品的品牌。

## 兼容标识不改

- macOS Bundle ID、输入源 ID、IMK 连接与控制器类名、现有 `RIMES.app` / `RIMES` 可执行文件名。
- iOS Bundle ID、App Groups、Keychain Groups；Android applicationId、签名。
- Windows CLSID、注册表键、协议、安装所有权标记；Linux 包名、addon ID。
- 所有用户数据路径、偏好键、凭据、词库、输入方案 ID（包括 `lingxi_wubi`）、插件 ID。
- 既有发布资产文件名、版本号、校验和、历史 Release、提交记录、备案证据。

显示名必须与上述身份分离，不能全仓库无差别替换 `rimes` / `RIMES` / `lingxi`。历史截图、词典里的自然语言词条不属于品牌替换。

## 仓库切换的实际阻碍

2026-10-15 曾将仓库改名为 `xsime` 并验证重定向，随后发现已发布 macOS 更新器的严格 URL 校验，已暂时恢复为 `scholay/rimes`。仓库简介使用新品牌，仓库 ID 保持 `1327328463`。

`Sources/RimeBuffer/UpdateManager.swift` 的 `githubRepo` 固定为 `rimes`，更新响应最终 API URL 和资产 URL 都要求精确匹配。GitHub 重定向并不能让已安装旧二进制接受新地址。用户已要求将五端代码与发布兼容统一纳入下一轮整理。

- [ ] 新版更新器仅允许同一官方 owner 下明确列出的新旧仓库 URL，并保留签名、包身份、版本和资产唯一性校验。
- [ ] 增加新旧 URL、非官方 owner、相似仓库、异常 URL 的正反向测试。
- [ ] 明确正式改名时间及旧版用户手动安装说明；即使先发布过渡版，未升级用户仍可能需要手动安装。
- [ ] 切换时同步 release workflow 的仓库门禁、发布脚本、remote、README 与下载地址；不能只改简介。
- [ ] 实测旧仓库页面、Issue、PR、Release 下载、Git 操作与应用内更新。不得重建 `scholay/rimes` 占用旧地址。

## 五端与外部系统下一轮

- [ ] macOS、iOS、Android、Windows、Linux 的启动器、键盘列表、设置、关于、可访问性名称、安装器、帮助文案使用对应语言的品牌。
- [ ] iOS 增加繁体名称资源；其他平台按现有本地化机制提供三种显示名，不假装整套界面已经翻译。
- [ ] 核查衍生功能名（如灵犀五笔），只改展示，不改 schema ID、词库和学习数据。
- [ ] Wiki、Roadmap、插件宿主介绍和图文素材统一；固定版本依赖与其校验和不盲目重写。
- [ ] App Store / TestFlight 与其他分发渠道按新版本流程更新；不新建 App、不改包身份。
- [ ] APP 工信部备案名称变更、公安联网备案及隐私说明按实际个人运营主体处理；不将网站「带派不」无关更名。
- [ ] 后续转公司运营时，另行核对商标授权/归属、备案主体、开发者账户及隐私主体。

## 旧 Pages 链接

用户已授权创建 `scholay/scholay.github.io`，在组织站点的 `/rimes/` 路径保存静态帮助/隐私页面兼容副本。这样无需重新创建 `rimes` 仓库；源仓库仍为 `rimes` 时项目站点优先，正式更名后由组织站点承接旧路径。部署成功和切换后的公网可达性须分别验证。
