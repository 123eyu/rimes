# 万象全拼九键 TestFlight 发布 — 2026-10-09

**iOS 1.1.2 (51) 已上传、处理完成，并关联现有 RIMES Community 组提交外部测试审核。回执为 Waiting for Review；这表示审核排队，不表示群友已能安装 build 51。已保留 Automatically notify testers，审核通过后自动通知现有测试组。** 提交时该组有 381 位测试者，中英文 What to Test 均已保存。

本次仅操作 TestFlight，没有替换或提交正式 App Store 版本，没有修改测试者成员或公开链接设置。公开邀请仍为 https://testflight.apple.com/join/Kdj9RB4q 。新构建标识为 c86ce306-00dd-415d-9fae-c2700391e8b9。

本包来自 ea24bb7 基础上的本轮已测试工作树。最初 1.1.1 (51) 上传被 Apple 拒绝：1.1.1 已获批、版本上传通道关闭。重试只改为 1.1.2/51，没有改变通过回归的功能代码。旧 build 50 来自此前源码，不能作为本次九键修复包。

独立归档、App Store 签名导出、IPA 校验及共享 App Group／Keychain 权限校验通过。完整源码与资源校验表、归档、IPA、日志保存在 platforms/ios/build/testflight-wanxiang-1.1.2-51-20261009/（忽略目录）；首次失败记录保留在 first-attempt-1.1.1-51.json。源文件校验表确认归档前后没有变化。

万象导入包需另行更新，TestFlight 更新不会自动替换用户已导入的 ZIP。新版 ZIP：/Users/isaac/Downloads/灵犀-万象全拼-v18.1.1-26键与9键.zip；SHA-256 为 f17b5f3e69987f1f40c05e87561976978682cd29e5206ed164db47c7bc6b3a65。升级到 build 51 后，导入 ZIP、勾选万象全拼、部署并启用，再选择「9键·全拼」。九键实际包、候选上屏、Buffer、选拼音、删除及切回 26 键验证见 ../ios-wanxiang-ninekey-20261009/。没有宣称该发布包已完成真机验收。

[提交回执](testflight-51-waiting-review.jpg)；机器可读发布证据见 release-evidence.json。
