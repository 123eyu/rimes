# Android 1.1.5 发布与维护

产品版本由 `VERSION` 提供，本轮构建目标为 **1.1.5**，`versionCode=19`。显示名称为犀术IME / 犀術IME / XSIME，官网为 `https://xsime.com`。构建号递增，已发布的版本与文件不覆盖。正式身份仍为 `org.scholay.rimes.android`；开发包使用独立的 `.debug` 身份。构建目标不代表已公开发布。

## 本地构建

普通 `:app:assembleRelease :app:bundleRelease` 在没有签名材料时生成未签名产物，仅供检查。正式签名由以下四个环境变量提供，不能写进源码或命令参数：

- `RIMES_ANDROID_KEYSTORE`：长期保管的正式 keystore 的绝对路径。
- `RIMES_ANDROID_STORE_PASSWORD`、`RIMES_ANDROID_KEY_ALIAS`、`RIMES_ANDROID_KEY_PASSWORD`。

配置完成后执行 `bash scripts/build-release.sh`。脚本运行核心测试、Release lint、APK/AAB 构建、APK 签名验证及 16 KB 对齐验证，在 `dist/1.1.5/` 保存产物与 SHA256SUMS。没有签名时直接停止，不回退到 debug keystore，不安装或上传。

## 当前交付证据

- [1.1.4 code 18 已公开](https://github.com/scholay/xsime/releases/tag/android-v1.1.4)。正式 APK／AAB 沿用长期签名，签名与 16 KB 对齐检查通过；公开资产已无登录下载并核对 SHA-256。发布页包含构建来源与验证范围的 `BUILD-INFO.json`。
- 五笔 86 的 `z` 全拼查字、犀术五笔混输、编码注释与四码唯一自动上屏已通过 JNI／候选按钮自动测试。正式签名 APK 另在 Android 15 AOSP 模拟器中用独立输入宿主逐键验收，确认注释不随正文提交，第四码唯一时不需要空格即可上屏。本轮未进行小米真机升级或长期日用验收。
- 使用独立 `android-v1.1.4` 标签并指定 `--latest=false`，安装包同时收录于全平台发布页。Android patch 可独立递增，必须保持与根版本相同 major/minor 且不低于根 patch。

### 历史验收与覆盖边界

- 1.1.2/code16 的独立自动检查、正式包升级与真机结果见[验收记录](validation/2026-10-09-1.1.2.md)。

- [1.1.1 code 15 已公开](https://github.com/scholay/xsime/releases/tag/android-v1.1.1)。小米 Android 16 从 code12 保留数据升级通过；正式 APK 的实际候选上屏、Buffer 开关、密码框隔离、主动剪贴板收录与确认上屏均通过。12 项独立存储 contract 补测通过。公开 APK/AAB 与元数据已无登录下载读回，精确 SHA-256 见[此次发布记录](../../validation/release-refresh-20261007.md)。

- [1.1.0 code 12 已公开](https://github.com/scholay/xsime/releases/tag/android-v1.1.0)，使用与 1.0.0 相同的长期生产密钥签名，签名、对齐、核心测试和 Release lint 均通过；公开证书 SHA-256 为 `e6228d5b065f1f8c2f1e1162b80ea1a9245ce59a091bb6e403e47936c3820c54`。
- code 10 的正式包已在小米真机验收拼音上屏、密码/私密字段隔离及独立 API 地址、模型与密钥入口。code 12 仅更新设置首页 Hero、中英文品牌文案、官网与邮箱链接和构建号，保留数据升级成功，版本元数据与 Hero 实际界面均已核对。
- DeepSeek 的真实网络与键盘请求测试在开发包完成；不将这份证据写成 code 12 的新付费请求。详见 [1.1.0 插件与 DeepSeek 验收](validation/2026-10-04-1.1.0-plugins-deepseek.md) 和 [跨平台交付记录](../../validation/release-1.1.0/delivery.md)。
- 开发包与正式包 applicationId 不同。发布不会删除开发包或用户词库，也不声称二者可原地继承数据。
- Android 8.0、更多厂商与长期日用覆盖仍需后续验证。发布资产需带对应版本、code、源码提交、SHA256SUMS 和签名信息。
