# Windows 搜索与沙箱输入兼容性修复（#133）

关联 [issue #133](https://github.com/scholay/rimes/issues/133)。本次完成源码修复、
本地构建及自动化回归；尚未完成真实 Windows 搜索栏和常用应用的安装后验收。
不应仅据此记录关闭 issue 或宣称全部应用兼容。

## 原因与修改

原实现仅注册 keyboard/display 类别；TSF 未提供候选 UIElement 协商，
候选窗也没有宿主 owner / IME light-dismiss 事件。Broker 管道只有桌面用户
权限，客户端验证还依赖读取服务端进程令牌。本机 SearchHost 和开始菜单宿主
的进程令牌均显示 `TokenIsAppContainer=1`，这条桌面通信路径不能直接使用。

- 注册并验证 `IMMERSIVESUPPORT` / `UIELEMENTENABLED`，按原事务规则回滚，
  在最后一个架构卸载时清理共享类别。
- 实现候选 Begin/Update/End 和 `ITfCandidateListUIElementBehavior`。宿主可
  接管显示、分页、选择、确认、取消；无 Win32 光标时仍提供候选。失焦、
  组合结束、重入及旧回调会撤销候选权限。跨预览页选词同步引擎页码。
- 原生候选窗关联宿主，发送 IME show/change/hide 事件；宿主销毁窗口后清理
  HWND，下一上下文可重新创建，保持不激活窗口。
- 单独增加 `.input` 管道：只允许普通 Rime 输入和候选修订校验。它不会注册
  Buffer 目标，也不提供设置、AI、投递或其他工作台控制。
- 沙箱管道的 package ACE 为 `FILE_GENERIC_READ | FILE_WRITE_DATA`
  (`0x12008b`)，不含创建实例、改 DACL 或改 owner；低完整性标签只作用于该
  管道。Broker 验证真实客户端 SID / 会话 / PID；沙箱客户端验证管道显式
  owner、内核返回的服务端会话及 PID，并核对 Hello。桌面验证保持原实现。
- 沙箱中不会启动外部 Broker；使用安装时配置的自启动或已运行的 Broker。
  桌面首次使用启动路径保留。未实现的搜索建议集成、系统托盘模式图标和
  input-mode compartment 能力没有新增声明。

设计依据：[Microsoft IME requirements](https://learn.microsoft.com/en-us/windows/apps/develop/input/input-method-editor-requirements)
和 [TSF UI-less mode](https://learn.microsoft.com/en-us/windows/win32/tsf/uiless-mode-overview)。
普通组字提交与集成搜索建议属于不同能力；本次只实现前者及通用候选 UI 协商。

## 本地验证

环境：Windows 11 Pro build 22631，VS 2022 Build Tools / MSVC 19.44，
Windows SDK 10.0.26100.0，CMake 4.4.4，Release，x64 + Win32。
修改基线为 `a5f0c55`。使用固定 librime 1.17.0 与隔离测试词库、用户目录、
Broker 身份和恢复测试 CLSID；没有替换日常安装或更改默认输入法。

| 检查 | 结果 |
| --- | --- |
| x64 / x86 全量 Release 构建 | 均通过；恢复测试 DLL 有既有 LNK4070 名称警告 |
| x64 CTest | 28/28；最后窗口生命周期修改后相关 5 项再通过 |
| x86 CTest（最终代码） | 28/28 |
| x64 / x86 TSF + 真实 Broker + librime 端到端 | 均通过 |
| x64 / x86 真实、无 capability 的 AppContainer 子进程 | 均通过 `nihao` → `你好` 组字和提交 |
| AppContainer 权限隔离 | 桌面管道、改 ACL/owner、新建全局管道及追加服务实例被拒；错误 owner/会话被拒 |
| 受限协议 | 不发布工作台能力；拒绝设置及其他工作台命令，不改变运行时状态；保留候选 guard |
| UI-less 候选 | 无光标、宿主隐藏原生窗、分页/选词/提交、失焦、Begin 失败和 Begin/Update 重入通过 |
| 候选窗口 | owner、窗口销毁重建、不激活、show/change/hide 事件通过 |
| 注册恢复 | x64/x86 注册、错误归属拒绝、缺失 DLL 卸载、保留另一架构、最后清理均通过 |
| 生产注册及语言列表 | 恢复测试前后相同 |

AppContainer 是 Windows 实际创建的受限进程，不是权限 mock；TSF 文档、线程
管理器仍是测试 fixture。沙箱子进程运行真实 TextService / BrokerClient，连接
真实隔离 Broker 和 librime。测试暂存目录只给当次 package SID 读/执行权限，
结束后删除暂存可执行文件和 profile。没有关闭或绕过系统应用策略。

两套 Windows CI 工作流已加入 sandbox probe/launcher 工件和 `-AppContainer`
参数。这里只记录本地结果；未推送或执行本次修改的远程 CI。

可重复命令（分别使用 x64 和 x86 构建目录；两套端到端测试顺序运行）：

```powershell
cmake -S platforms/windows/native -B <build> -G "Visual Studio 17 2022" -A <x64-or-Win32> -DRIMES_BUILD_TESTS=ON
cmake --build <build> --config Release --parallel 6
ctest --test-dir <build> -C Release --output-on-failure
./platforms/windows/tests/Invoke-RimesImeE2E.ps1 -Architecture <x64-or-x86> -ArtifactDirectory <build>/Release -WorkDirectory <fresh-directory> -AppContainer
# 独立恢复测试身份，需要管理员终端
./platforms/windows/tests/Test-RegistrarRecovery.ps1 -ArtifactX64 <x64-build>/Release -ArtifactX86 <x86-build>/Release -ReportDirectory <fresh-directory>
```

CMake 在本机包含中文的源路径下出现过工具自身崩溃，因此构建使用指向本仓库的
ASCII junction；没有修改源码路径或依赖内容。机器可读结果、构件哈希见
[results.json](results.json)，CTest 日志见同目录 `ctest-*.txt`。

## 真实宿主验收缺口

桌面工具拒绝启动 SearchHost，原文为 `product policy blocks this app`。
没有用其他自动化路径绕过该限制，因此真实搜索栏结果为 **blocked**，不是通过。
这次也未把自动化 fixture 结果冒充为记事本、浏览器、Office、聊天或终端应用
的输入验收。完整宿主矩阵已补到
[MANUAL-TEST.md](../../platforms/windows/native/MANUAL-TEST.md)。

安装新版本后仍需验证 Windows 搜索/开始菜单/设置、资源管理器、记事本、
Edge/Chrome/Firefox、Electron、QQ/微信、Office、终端和 UWP/WinUI：记录版本、
架构、签名、schema，逐项检查组字、候选、选词、退格、取消、切换和失焦。
集成搜索建议、触摸键盘布局、完整可访问性与签名发布不在本次自动化通过范围。
