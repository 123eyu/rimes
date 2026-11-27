# iOS 构建与发布

版本及工程配置来自 [`platforms/ios/project.yml`](../../../platforms/ios/project.yml)，Xcode 项目由 XcodeGen 生成。主 App 与键盘扩展共用 `MARKETING_VERSION` 和 `CURRENT_PROJECT_VERSION`；商店更新说明放在 [`AppStore/release-notes/`](../../../platforms/ios/AppStore/release-notes/)。实际发布状态以 Apple 和公开资产读回为准。

## 检查与未签名归档

[`ios.yml`](../../../.github/workflows/ios.yml) 在匹配的 PR、main 推送或手动运行时检查发布配置、构建模拟器与设备目标，并运行 Shared 和 hosted iOS 测试。产品构建与自动测试在远端 CI 执行。

需要交给 Xcode 云签名的归档时，在 main 手动运行该工作流，设置 `archive=true` 和未使用的 `build_number`。默认 `archive_purpose=app-store`，工作流校验分发审计并保留 `ios-cloud-archive-<commit>-<build>` 及 `archive-provenance.json`。该路径不上传 Apple，也不生成可直接安装的发行包。

仅为完成实体设备验收而需要归档时，可选择 `archive_purpose=device-qa`。它仍执行资源校验和归档完整性检查，但不要求尚待验收的分发审计先通过；产物名为 `ios-device-qa-archive-<commit>-<build>`，回执也标记为 `device-qa`。这是未签名的内部测试输入，不代表可以上架；正式上传和提交仍须通过分发审计及签名验证。

下载后核对源码、版本、构建号与 ZIP SHA-256，保留原归档。签名前从仓库根目录检查：

```sh
python3 platforms/ios/scripts/verify-release-ipa.py --check-unsigned-app <archive>/Products/Applications/RIMES.app
```

需要使用现有 Apple 云签名账号时，在归档副本上准备匹配的开发签名，再用 Xcode Organizer 的 Custom → App Store Connect → Export 导出；关闭自动管理版本号，保留主 App、扩展和共享组身份。导出的 App Store IPA 用同一工具检查：

```sh
python3 platforms/ios/scripts/verify-release-ipa.py <ipa> <version> <build>
```

检查同时覆盖签名、App Store profiles、版本、隐私声明、键盘内手写模型摘要及两目标的 App Group/Keychain。既有标识为 `org.scholay.rimes.ios`、`org.scholay.rimes.ios.keyboard`、`group.org.scholay.rimes.ios` 和 `585J2TL9U6.org.scholay.rimes.ios.shared`。运行时 Info.plist 与签名中的共享组必须一致，重新签名不会修复错误的运行时配置。

## 自动签名与 App Store 提交

[`ios-release.yml`](../../../.github/workflows/ios-release.yml) 由 `ios-vMAJOR.MINOR.PATCH` 标签触发。标签必须来自 main 历史并匹配 iOS `MARKETING_VERSION`；iOS 版本与根版本保持相同 major/minor，patch 不倒退。三语更新说明必须非空、每项不超过 4000 字符。构建号由 `(GITHUB_RUN_NUMBER + 100).GITHUB_RUN_ATTEMPT.0` 生成。

`ios-app-store` 环境需要以下 Secrets：

| Secret | 用途 |
|---|---|
| `ASC_KEY_ID`、`ASC_ISSUER_ID`、`ASC_PRIVATE_KEY_BASE64` | 具备发布权限的 App Store Connect API key |
| `IOS_DISTRIBUTION_P12_BASE64`、`IOS_DISTRIBUTION_P12_PASSWORD` | Apple Distribution 证书及私钥 |
| `IOS_APP_PROFILE_BASE64` | 主 App 的 App Store profile |
| `IOS_KEYBOARD_PROFILE_BASE64` | 键盘扩展的 App Store profile |

Profiles 必须匹配代码中的团队、bundle 与共享组，并在有效期内。凭据只通过发布环境配置，不写入仓库或日志；本指南不声明账号当前已配齐这些凭据。

工作流完成构建和测试并检查分发审计后调用 [`fastlane/Fastfile`](../../../platforms/ios/fastlane/Fastfile)：使用临时 keychain 签名，验证 IPA，上传并等待 Apple 处理，再提交 App Review，设置审核通过后自动发布。提交时同时上传仓库中三语商店描述、三语更新说明和审核说明；截图、价格、地区、审核联系人与 App Privacy 问卷保留已有值，提交前需单独核对。它不发起外部 TestFlight 审核、不分发外部测试者，也不替换正在审核的版本或其他版本草稿。提交回执写入 `platforms/ios/build/ci-release/submission.json` 并进入 CI summary。

上传、Apple 处理、外部 TestFlight 审核、App Review 和商店可用是独立状态。处理超时后先读回 Apple 状态；重跑会产生新的构建号。清理签名材料的工作流仅用于一次性 GitHub 托管 runner。

如果 **Add for Review** 提示开发者名称与 ICP 备案主体不匹配，打开 **App Information**，在 **Provider Name Not Recognized by MIIT** 提示中点击 **Confirm Information**。该链接会在 **App Compliance** 打开备案归属确认窗口。核对现有备案号和主体关系并确认后，回版本页继续 **Add for Review → Submit for Review**。

## 本机 API key 上传

没有配齐 `ios-app-store` Secrets 时，维护者可在本机用 App Store Connect API key 上传，不经浏览器或 Xcode Organizer：

```sh
ASC_KEY_ID=<key id> ASC_ISSUER_ID=<issuer id> ASC_KEY_PATH=<仓库外的 AuthKey_<id>.p8> \
  platforms/ios/scripts/upload-app-store.sh --build <新构建号> [--submit]
```

[`upload-app-store.sh`](../../../platforms/ios/scripts/upload-app-store.sh) 先检查：已跟踪文件无未提交改动、HEAD 已在 `origin/main`、iOS 版本规则与三语更新说明、分发审计、构建号格式，以及 key 位于仓库外且权限为 600。`--check-only` 只做这些检查。之后依次运行 `build.sh` 与 hosted 模拟器测试（`--skip-tests` 可跳过测试），用该 key 让 `xcodebuild` 自动签名归档并以云签名导出 App Store Connect IPA，经 `verify-release-ipa.py` 校验后，用 `altool --validate-app` 和 `altool --upload-package --wait` 上传同一个已校验的 IPA 并等待 Apple 处理。产物和不含凭据的 `receipt.json` 位于 `platforms/ios/build/local-release/<version>-<build>/`。

- 云签名需要 key 具备证书管理权限，通常为 Admin 角色。key 只在 App Store Connect 的 Users and Access → Integrations 生成，且只能下载一次；脚本只把路径交给 Apple 工具，不复制、不打印、不写入回执。
- 构建号由维护者指定，必须是该 App 未用过的号；Apple 按数字段比较，CI 的 `(run+100).attempt.0` 高于本机常用的单段号。
- `--submit` 在处理完成后调用 Fastlane `submit_local_upload`，与 CI 共用同一套保护：不打断审核中或待发布的版本，不替换其他版本草稿；附三语更新说明并提交审核、批准后自动发布，回执为同目录的 `submission.json`。Fastfile 锁定的 Fastlane 需要 Ruby 3.x（系统 Ruby 2.6 不可用）。
- 不带 `--submit` 时只完成上传与处理，审核提交另行进行。上传、处理、审核与上架仍是独立状态，以 Apple 读回为准。

## GitHub 社区 IPA

从原始未签名 CI 归档打包 `Payload/RIMES.app`，包含键盘扩展并保留执行权限和符号链接。附带 SHA-256、源码/CI 来源和自行签名说明；公开文件的可执行内容应与原归档一致。不要发布开发签名副本、设备 profiles 或凭据。

社区 IPA 需要用户签名，主 App 和扩展都必须有可用 profiles 以及共享 App Group/Keychain 权限。其发布不代表 Apple 已批准商店或 TestFlight 版本。商店资料入口见 [AppStore README](../../../platforms/ios/AppStore/README.md)，资源许可与隐私要求见[资源说明](resources.md)和[隐私说明](privacy.md)。
