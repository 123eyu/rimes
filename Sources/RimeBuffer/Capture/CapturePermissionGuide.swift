import AppKit
import Carbon.HIToolbox
import ScreenCaptureKit

/// Permission evidence is scoped to one explicit attempt, never persisted as
/// authorization. A preflight failure alone is not a ScreenCaptureKit error.
@MainActor
final class CapturePermissionCheck {
    enum State: Equatable {
        case idle, checking, ready, blocked
        case failed(String)
    }
    private(set) var state = State.idle
    private var generation = UUID()
    private struct NotEffective: Error {}

    static func isPermissionFailure(_ error: Error) -> Bool {
        if error is NotEffective { return true }
        var current = error as NSError
        for _ in 0..<4 {
            if current.domain == SCStreamErrorDomain && current.code == SCStreamError.Code.userDeclined.rawValue { return true }
            guard let underlying = current.userInfo[NSUnderlyingErrorKey] as? NSError else { break }
            current = underlying
        }
        return false
    }

    static func verify(_ permission: SystemPermission) async throws {
        if permission == .screenRecording {
            // Explicit, OS-authorized capability check only. No frame is
            // captured, stored or logged, and no prior success bypasses TCC.
            _ = try await CaptureEngine.content()
        } else if SystemPermissionAudit.status(for: permission) != .granted {
            throw NotEffective()
        }
    }

    func check(_ operation: () async throws -> Void) async {
        guard state != .checking else { return }
        generation = UUID(); let token = generation
        state = .checking
        do {
            try await operation()
            guard generation == token, !Task.isCancelled else { return }
            state = .ready
        } catch {
            guard generation == token, !Task.isCancelled else { return }
            if Self.isPermissionFailure(error) { state = .blocked }
            else { state = .failed((error as NSError).localizedDescription) }
        }
    }

    func cancel() { generation = UUID(); state = .idle }
}

struct CapturePermissionPresentation {
    enum Action { case request, openSettings, verify }
    let action: Action
    let primaryTitle: String
    let message: String
    let showsVerify: Bool

    init(preflight: Bool, requested: Bool, state: CapturePermissionCheck.State, continuing: Bool) {
        if case .failed = state {
            action = .verify; primaryTitle = "重试"
            message = "暂时无法继续，请重试。"; showsVerify = false
        } else if state == .ready || (preflight && state != .blocked) {
            action = .verify; primaryTitle = continuing ? "继续" : "完成"
            message = "已开启，可以继续。"; showsVerify = false
        } else if requested || state == .blocked {
            action = .openSettings; primaryTitle = "打开系统设置"
            message = state == .blocked ? "授权还未生效，开启后再试。" : "在系统设置中打开 \(ProductBrand.name) 的开关。"
            showsVerify = true
        } else {
            action = .request; primaryTitle = "去开启"
            message = "macOS 会请你允许 \(ProductBrand.name)。"; showsVerify = false
        }
    }
}

/// One guide shared by capture and Settings. No automatic Settings launch,
/// permission reset, restart or delayed capture when the user merely returns.
@MainActor
final class CapturePermissionGuide {
    static let shared = CapturePermissionGuide()
    private var panel: CapturePanel?
    private var pending: Task<Void, Never>?
    private let check = CapturePermissionCheck()
    private var activationObserver: NSObjectProtocol?
    private var lockObserver: NSObjectProtocol?
    private var protectedObservers: [NSObjectProtocol] = []
    private var status: NSTextField?
    private var requestButton: CaptureButton?
    private var verifyButton: CaptureButton?
    private var body: NSView?
    private var help: PermissionHelpDisclosure?
    private var permission = SystemPermission.screenRecording
    private var continuation: (() -> Void)?
    private var requested = false
    private let allowsSystemActions: Bool

    init(allowsSystemActions: Bool = true) { self.allowsSystemActions = allowsSystemActions }

    @discardableResult
    func presentExisting() -> Bool {
        guard let panel else { return false }
        panel.present(center: false)
        return true
    }

    func show(_ permission: SystemPermission, retry: (() -> Void)? = nil) {
        guard !IsSecureEventInputEnabled(), !presentExisting() else { return }
        self.permission = permission; continuation = retry; requested = false
        let panel = CapturePanel(size: NSSize(width: 500, height: 280))
        panel.styleMask.remove(.resizable)
        self.panel = panel
        func label(_ text: String) -> NSTextField {
            let view = NSTextField(labelWithString: text)
            view.font = .systemFont(ofSize: 13); view.textColor = RimeUI.textSecondary
            view.preferredMaxLayoutWidth = 456
            return view
        }
        let status = label(""); self.status = status
        let request = CaptureButton("去开启") { [weak self] in self?.primaryAction() }
        request.isProminent = true; request.font = .systemFont(ofSize: 13)
        requestButton = request
        let verify = CaptureButton("我已开启") { [weak self] in self?.verify() }
        verify.font = .systemFont(ofSize: 13)
        verifyButton = verify
        let later = CaptureButton("稍后") { [weak self] in self?.dismiss() }
        later.font = .systemFont(ofSize: 13)
        let actions = CaptureUI.row([request, verify, later], spacing: 10)
        let settings = CaptureButton("打开系统设置") { [weak self] in self?.openSettings() }
        var recovery: [NSView] = []
        if permission.supportsManualApplicationAddition {
            recovery.append(PermissionApplicationCard(width: 456, allowsSystemActions: allowsSystemActions))
        } else {
            recovery.append(label("在系统设置的「\(permission.title)」中开启 \(ProductBrand.name)。"))
        }
        let restart = CaptureButton("重启 \(ProductBrand.name)…") { [weak self] in self?.restart() }
        restart.toolTip = "开关已开启但仍不能用时，重启后再试"
        recovery.append(CaptureUI.row([settings, restart]))
        let help = PermissionHelpDisclosure(details: CaptureUI.column(recovery, spacing: 12), width: 456)
        self.help = help
        help.didToggle = { [weak self, weak help] in
            self?.status?.isHidden = help?.expanded == true
            self?.resizePanel()
        }
        let title = CaptureUI.label(permission.requestTitle, size: 22)
        title.font = .systemFont(ofSize: 22, weight: .semibold)
        let body = CaptureUI.column([title, status, actions, help], spacing: 16)
        self.body = body
        let scroll = CaptureInspectorScroll(body)
        scroll.autohidesScrollers = true; scroll.scrollerStyle = .overlay
        // This panel hides its titlebar controls and supplies its own inset.
        // AppKit's automatic titlebar inset otherwise clips the last row.
        scroll.automaticallyAdjustsContentInsets = false
        let content = panel.contentView!
        CaptureUI.fill(scroll, in: content, inset: 22)
        panel.closed = { [weak self, weak panel] in
            self?.pending?.cancel(); self?.pending = nil; self?.check.cancel()
            self?.continuation = nil; self?.panel = nil
            self?.body = nil; self?.help = nil
            self?.removeObservers(); panel?.contentView = nil
        }
        activationObserver = NotificationCenter.default.addObserver(forName: NSApplication.didBecomeActiveNotification, object: nil, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.refreshStatus() }
        }
        for name in [NSWorkspace.sessionDidResignActiveNotification, NSWorkspace.willSleepNotification, NSWorkspace.willPowerOffNotification] {
            protectedObservers.append(NSWorkspace.shared.notificationCenter.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in
                MainActor.assumeIsolated { self?.dismiss() }
            })
        }
        lockObserver = DistributedNotificationCenter.default().addObserver(forName: NSNotification.Name("com.apple.screenIsLocked"), object: nil, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.dismiss() }
        }
        refreshStatus()
        resizePanel()
        panel.present()
    }

    private func resizePanel() {
        guard let panel, let body else { return }
        body.layoutSubtreeIfNeeded()
        let availableHeight = (panel.screen ?? NSScreen.main)?.visibleFrame.height ?? 700
        panel.setContentSize(NSSize(width: 500, height: min(availableHeight - 80, max(220, body.fittingSize.height + 44))))
    }

    private var presentation: CapturePermissionPresentation {
        CapturePermissionPresentation(preflight: allowsSystemActions && SystemPermissionAudit.status(for: permission) == .granted,
            requested: requested, state: check.state, continuing: continuation != nil)
    }

    private func primaryAction() {
        guard allowsSystemActions, pending == nil else { return }
        switch presentation.action {
        case .request: request()
        case .openSettings: openSettings()
        case .verify: verify()
        }
    }

    private func openSettings() {
        guard allowsSystemActions, !IsSecureEventInputEnabled(), let url = permission.settingsURL else { return }
        NSWorkspace.shared.open(url)
    }

    func dismiss() { panel?.close() }

    private func removeObservers() {
        if let activationObserver { NotificationCenter.default.removeObserver(activationObserver) }
        activationObserver = nil
        if let lockObserver { DistributedNotificationCenter.default().removeObserver(lockObserver) }
        lockObserver = nil
        protectedObservers.forEach { NSWorkspace.shared.notificationCenter.removeObserver($0) }
        protectedObservers.removeAll()
    }

    private func request() {
        guard allowsSystemActions, pending == nil, !requested, !IsSecureEventInputEnabled() else { return }
        requested = true
        let permission = permission
        pending = Task { [weak self] in
            await SystemPermissionAudit.requestOnly(permission)
            guard !Task.isCancelled, let self, self.panel != nil else { return }
            self.pending = nil; self.refreshStatus()
        }
        refreshStatus()
    }

    private func verify() {
        guard allowsSystemActions, pending == nil, !IsSecureEventInputEnabled() else { return }
        let permission = permission
        pending = Task { [weak self] in
            guard let self else { return }
            await self.check.check { try await CapturePermissionCheck.verify(permission) }
            guard !Task.isCancelled, self.panel != nil else { return }
            self.pending = nil; self.refreshStatus()
            if self.check.state == .ready, !IsSecureEventInputEnabled() {
                let action = self.continuation
                self.dismiss()
                if let action { DispatchQueue.main.async { action() } }
            }
        }
        refreshStatus()
    }

    private func refreshStatus() {
        guard panel != nil else { return }
        let state = presentation
        requestButton?.isEnabled = pending == nil
        requestButton?.title = state.primaryTitle
        requestButton?.setAccessibilityLabel(state.primaryTitle)
        verifyButton?.isHidden = !state.showsVerify
        verifyButton?.isEnabled = pending == nil
        status?.stringValue = pending != nil ? "正在等待系统回应…"
            : state.action == .request ? "用于\(permission.shortPurpose)。" : state.message
        if case .failed(let message) = check.state {
            status?.toolTip = message
        } else {
            status?.toolTip = nil
        }
        resizePanel()
    }

    private func restart() {
        guard allowsSystemActions else { return }
        let alert = NSAlert()
        alert.messageText = "重启 \(ProductBrand.name)？"
        alert.informativeText = "会关闭窗口、结束终端会话并停止录屏。请先保存未完成内容；词库和设置会保留。"
        alert.addButton(withTitle: "取消"); alert.addButton(withTitle: "重启 \(ProductBrand.name)")
        guard alert.runModal() == .alertSecondButtonReturn else { return }
        dismiss(); StatusMenu.shared.restart()
    }

    func renderForSmoke(to output: URL) throws {
        precondition(!allowsSystemActions)
        show(.screenRecording)
        defer { dismiss() }
        if ProcessInfo.processInfo.environment["RIMES_PERMISSION_HELP_PREVIEW"] == "1" {
            help?.setExpanded(true)
        }
        if ProcessInfo.processInfo.environment["RIMES_UI_REVIEW"] == "permission" {
            let end = Date().addingTimeInterval(45)
            while Date() < end { RunLoop.current.run(until: Date().addingTimeInterval(0.05)) }
        }
        guard let view = panel?.contentView else { throw CaptureError.message("permission preview unavailable") }
        view.layoutSubtreeIfNeeded()
        func validate(_ node: NSView) throws {
            guard !node.isHiddenOrHasHiddenAncestor else { return }
            if node is CaptureButton,
               !node.visibleRect.contains(node.bounds.insetBy(dx: 0.5, dy: 0.5)) {
                throw CaptureError.message("permission preview contains a clipped action")
            }
            try node.subviews.forEach(validate)
        }
        try validate(view)
        guard let bitmap = view.bitmapImageRepForCachingDisplay(in: view.bounds) else { throw CaptureError.message("permission preview allocation failed") }
        view.cacheDisplay(in: view.bounds, to: bitmap)
        guard let data = bitmap.representation(using: .png, properties: [:]) else { throw CaptureError.message("permission preview encoding failed") }
        try data.write(to: output)
    }
}
