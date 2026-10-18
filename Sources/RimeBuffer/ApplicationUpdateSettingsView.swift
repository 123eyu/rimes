import AppKit

/// An inline view observes the updater without stealing the status menu callback.
final class ApplicationUpdateSettingsView: NSStackView {
    private let message = NSTextField(wrappingLabelWithString: "")
    private let progress = NSProgressIndicator()
    private let check = NSButton(title: "检查更新", target: nil, action: nil)
    private let download = NSButton(title: "下载更新", target: nil, action: nil)
    private let install = NSButton(title: "安装更新…", target: nil, action: nil)
    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        orientation = .vertical; alignment = .leading; spacing = 8
        message.preferredMaxLayoutWidth = 620
        addArrangedSubview(message)
        progress.isIndeterminate = false; progress.minValue = 0; progress.maxValue = 1
        progress.widthAnchor.constraint(equalToConstant: 280).isActive = true
        addArrangedSubview(progress)
        let actions = NSStackView(views: [check, download, install]); actions.spacing = 8
        addArrangedSubview(actions)
        for button in [check, download, install] { button.target = self; button.bezelStyle = .rounded }
        check.action = #selector(checkNow); download.action = #selector(downloadNow); install.action = #selector(installNow)
        NotificationCenter.default.addObserver(self, selector: #selector(refresh), name: UpdateManager.changed, object: nil)
        refresh()
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }
    deinit { NotificationCenter.default.removeObserver(self) }
    @objc private func checkNow() { UpdateManager.shared.checkFromSettings() }
    @objc private func downloadNow() { UpdateManager.shared.downloadFromSettings() }
    @objc private func installNow() { UpdateManager.shared.promptAndInstall() }
    @objc private func refresh() {
        let updater = UpdateManager.shared
        check.isEnabled = true; download.isEnabled = false; install.isEnabled = false
        progress.isHidden = true
        switch updater.status {
        case .idle: message.stringValue = updater.completionMessage ?? "当前版本 \(updater.currentVersion)"
        case .checking: message.stringValue = "正在检查更新…"; check.isEnabled = false
        case let .available(version, _, _): message.stringValue = "发现新版本 \(version)"; download.isEnabled = true
        case .noUpdate: message.stringValue = updater.completionMessage ?? "已是最新版本 \(updater.currentVersion)"
        case .downloading:
            message.stringValue = "正在下载… \(Int(updater.progress * 100))%"
            progress.isHidden = false; progress.doubleValue = updater.progress; check.isEnabled = false
        case let .readyToInstall(version, _):
            message.stringValue = updater.installationRequested
                ? "已打开系统安装器，等待更新完成。重新启动输入法后将核对版本；若取消，可再次安装。"
                : "\(version) 下载及签名验证完成，可以安装。"
            install.isEnabled = true
        case .installing: message.stringValue = "正在核对安装包并打开系统安装器…"; check.isEnabled = false
        case let .error(error): message.stringValue = error
        }
    }
}
