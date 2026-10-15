import Cocoa

enum MailboxWindowToggleAction: Equatable {
    case show
    case close
}

enum MailboxWindowVisibilityRules {
    static func action(isVisible: Bool) -> MailboxWindowToggleAction {
        isVisible ? .close : .show
    }
}

/// Standalone, key-capable Mailbox window. Its lifecycle is independent from
/// Buffer capture and the workbench window.
final class MailboxWindowController: NSObject, NSWindowDelegate {
    static let shared = MailboxWindowController()

    private static let frameAutosaveName = "RIMES.MailboxWindow"

    private var window: NSWindow?
    private var contentController: MailboxWorkspaceViewController?
    private var appearanceObserver: NSObjectProtocol?

    deinit {
        if let appearanceObserver {
            NotificationCenter.default.removeObserver(appearanceObserver)
        }
    }

    static func refreshIfOpen() {
        guard shared.window?.isVisible == true else { return }
        shared.contentController?.reloadFromStore()
    }

    static var isVisible: Bool { shared.window?.isVisible == true }

    static var isKeyAndVisible: Bool {
        shared.window?.isVisible == true && shared.window?.isKeyWindow == true
    }

    /// The configured global Mailbox shortcut is a true visibility toggle.
    /// Closing deliberately reuses the normal window lifecycle so it neither
    /// restores Buffer capture nor keeps a stale Mailbox editing focus alive.
    @discardableResult
    func toggleVisibility() -> MailboxWindowToggleAction {
        let action = MailboxWindowVisibilityRules.action(
            isVisible: window?.isVisible == true
        )
        switch action {
        case .show:
            show()
        case .close:
            window?.close()
        }
        return action
    }

    func show(selecting threadID: UUID? = nil) {
        // Mailbox is a standalone key window; showing it does not require the
        // current input source to belong to RIMES.
        if window == nil { build() }
        applyAppearance()
        contentController?.show(selecting: threadID)
        if let window {
            StandaloneWindowFocusCoordinator.shared.windowWillPresent(window)
        }
        NSApp.activate(ignoringOtherApps: true)
        window?.makeKeyAndOrderFront(nil)
        DispatchQueue.main.async { [weak self] in
            self?.contentController?.windowBecameKey()
        }
    }

    func windowDidBecomeKey(_ notification: Notification) {
        contentController?.windowBecameKey()
    }

    func windowWillClose(_ notification: Notification) {
        // Closing Mailbox is intentionally terminal for this window only. It
        // must not restore Buffer or reuse a stale host-focus token.
        InboundToast.shared.hide()
        guard let closingWindow = notification.object as? NSWindow,
              closingWindow === window else { return }
        StandaloneWindowFocusCoordinator.shared.windowWillClose(closingWindow)
    }

    private func build() {
        let win = NSWindow(
            contentRect: NSRect(origin: .zero, size: MailboxUI.defaultSize),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false
        )
        win.title = "\(ProductBrand.name) Mailbox"
        win.isReleasedWhenClosed = false
        win.contentMinSize = MailboxUI.minimumSize
        win.appearance = RimeUI.appKitAppearance
        win.animationBehavior = .documentWindow
        win.delegate = self

        let contentController = MailboxWorkspaceViewController()
        win.contentViewController = contentController
        win.setContentSize(MailboxUI.defaultSize)

        let restored = win.setFrameUsingName(Self.frameAutosaveName)
        if let size = win.contentView?.bounds.size,
           size.width < MailboxUI.minimumSize.width || size.height < MailboxUI.minimumSize.height {
            win.setContentSize(NSSize(width: max(size.width, MailboxUI.minimumSize.width),
                                      height: max(size.height, MailboxUI.minimumSize.height)))
        }
        _ = win.setFrameAutosaveName(Self.frameAutosaveName)
        if !restored { win.center() }

        window = win
        self.contentController = contentController
        appearanceObserver = NotificationCenter.default.addObserver(
            forName: .rimeAppearanceDidChange,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            self?.applyAppearance()
        }
    }

    private func applyAppearance() {
        window?.appearance = RimeUI.appKitAppearance
        contentController?.applyAppearance()
    }
}

func runMailboxWindowSmokeTest() -> Bool {
    let own = StandaloneWindowFocusIdentity(
        bundleID: RimesIdentity.legacyBundleIdentifier,
        processIdentifier: 900
    )
    let external = StandaloneWindowFocusIdentity(
        bundleID: "com.example.Editor",
        processIdentifier: 101
    )
    let sameBundleOtherProcess = StandaloneWindowFocusIdentity(
        bundleID: own.bundleID,
        processIdentifier: 901
    )
    let canRestore = StandaloneWindowFocusReturnRules.shouldRestore(
        closeCompleted: true,
        remainingTrackedWindowCount: 0,
        frontmost: own,
        own: own,
        returnTarget: external,
        returnTargetIsRunning: true,
        returnTargetIdentityMatches: true,
        hasOtherVisibleKeyCapableOwnWindow: false
    )
    guard MailboxWindowVisibilityRules.action(isVisible: false) == .show,
          MailboxWindowVisibilityRules.action(isVisible: true) == .close,
          StandaloneWindowFocusReturnRules.isExternalReturnTarget(
            external,
            own: own
          ),
          !StandaloneWindowFocusReturnRules.isExternalReturnTarget(
            own,
            own: own
          ),
          !StandaloneWindowFocusReturnRules.isExternalReturnTarget(
            sameBundleOtherProcess,
            own: own
          ),
          canRestore,
          !StandaloneWindowFocusReturnRules.shouldRestore(
            closeCompleted: false,
            remainingTrackedWindowCount: 0,
            frontmost: own,
            own: own,
            returnTarget: external,
            returnTargetIsRunning: true,
            returnTargetIdentityMatches: true,
            hasOtherVisibleKeyCapableOwnWindow: false
          ),
          !StandaloneWindowFocusReturnRules.shouldRestore(
            closeCompleted: true,
            remainingTrackedWindowCount: 1,
            frontmost: own,
            own: own,
            returnTarget: external,
            returnTargetIsRunning: true,
            returnTargetIdentityMatches: true,
            hasOtherVisibleKeyCapableOwnWindow: false
          ),
          !StandaloneWindowFocusReturnRules.shouldRestore(
            closeCompleted: true,
            remainingTrackedWindowCount: 0,
            frontmost: external,
            own: own,
            returnTarget: external,
            returnTargetIsRunning: true,
            returnTargetIdentityMatches: true,
            hasOtherVisibleKeyCapableOwnWindow: false
          ),
          !StandaloneWindowFocusReturnRules.shouldRestore(
            closeCompleted: true,
            remainingTrackedWindowCount: 0,
            frontmost: own,
            own: own,
            returnTarget: external,
            returnTargetIsRunning: true,
            returnTargetIdentityMatches: true,
            hasOtherVisibleKeyCapableOwnWindow: true
          ),
          !StandaloneWindowFocusReturnRules.shouldRestore(
            closeCompleted: true,
            remainingTrackedWindowCount: 0,
            frontmost: own,
            own: own,
            returnTarget: external,
            returnTargetIsRunning: false,
            returnTargetIdentityMatches: true,
            hasOtherVisibleKeyCapableOwnWindow: false
          ) else {
        fputs("mailbox-window-smoke: lifecycle/focus-return mismatch\n", stderr)
        return false
    }
    print("mailbox-window-smoke: ok")
    return true
}
