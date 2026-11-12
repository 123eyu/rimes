import Cocoa
import Carbon
import IOKit
import InputMethodKit

/// The application holding system secure input. Diagnostic only: it explains
/// a pass-through to the user and never changes what `ProtectedInput` blocks.
struct SecureInputHolder: Equatable {
    let processIdentifier: pid_t
    let bundleID: String?
    let name: String?
    /// A Dock application. Accessory and background processes (a password
    /// manager's mini window, SecurityAgent) never count as "elsewhere".
    var isRegularApplication = false
}

/// Secure input held by some other application in the background protects
/// that application's field, not the one being typed in. Composition may then
/// continue, but only on positive identification: every doubt keeps the pause.
/// Buffer, clipboard, plug-in and capture surfaces ignore this rule and stay
/// off under any secure input.
enum SecureInputScopeRules {
    /// The holder-only half of the test; the field's own bundle comes after.
    static func holderIsElsewhere(holder: SecureInputHolder?,
                                  frontmostProcessIdentifier: pid_t?,
                                  frontmostBundleID: String?,
                                  ownProcessIdentifier: pid_t,
                                  ownBundleID: String) -> Bool {
        guard let holder,
              holder.isRegularApplication,
              let holderBundleID = holder.bundleID,
              let frontmostProcessIdentifier,
              let frontmostBundleID,
              holder.processIdentifier > 0,
              holder.processIdentifier != frontmostProcessIdentifier,
              holder.processIdentifier != ownProcessIdentifier,
              holderBundleID != frontmostBundleID,
              holderBundleID != ownBundleID,
              holderBundleID != ProtectedInput.loginWindowBundleID,
              frontmostBundleID != ProtectedInput.loginWindowBundleID else {
            return false
        }
        return true
    }

    /// Whether secure input still protects the field `clientBundleID` names.
    /// An unread client, or a field of the holder itself, stays protected.
    static func protectsField(secureInput: Bool,
                              holder: SecureInputHolder?,
                              frontmostProcessIdentifier: pid_t?,
                              frontmostBundleID: String?,
                              clientBundleID: String?,
                              ownProcessIdentifier: pid_t,
                              ownBundleID: String) -> Bool {
        guard secureInput else { return false }
        guard let clientBundleID,
              clientBundleID != ProtectedInput.loginWindowBundleID,
              clientBundleID != holder?.bundleID,
              holderIsElsewhere(
                holder: holder,
                frontmostProcessIdentifier: frontmostProcessIdentifier,
                frontmostBundleID: frontmostBundleID,
                ownProcessIdentifier: ownProcessIdentifier,
                ownBundleID: ownBundleID
              ) else { return true }
        return false
    }
}

/// Wording for the input-source menu while secure input pauses composition.
/// A holder that is neither this process nor the application in front is the
/// case a user cannot see for themselves: some other application left secure
/// input on, so every ordinary field silently receives raw keys.
enum SecureInputNoticeRules {
    static let headline = "⚠️ 安全输入已开启 — 中文输入暂停"
    static let backgroundHeadline = "⚠️ 后台应用开启了安全输入"
    static let foregroundDetail = "当前应用正在保护密码输入，离开密码框后自动恢复"
    static let unknownHolderDetail = "未能识别开启它的应用；若并未输入密码，请逐个退出后台应用排查"

    static func displayName(_ holder: SecureInputHolder) -> String {
        holder.name ?? holder.bundleID ?? "进程 \(holder.processIdentifier)"
    }

    /// Both menu lines. A holder that the scope rule places elsewhere leaves
    /// composition running, so the menu says what is paused instead.
    static func lines(holder: SecureInputHolder?,
                      frontmostProcessIdentifier: pid_t?,
                      holderIsElsewhere: Bool,
                      ownProcessIdentifier: pid_t) -> [String] {
        if holderIsElsewhere, let holder {
            return [
                backgroundHeadline,
                "由「\(displayName(holder))」开启；中文输入照常，Buffer、剪贴板等功能暂停，退出它或离开其密码框后恢复",
            ]
        }
        return [
            headline,
            detail(
                holder: holder,
                frontmostProcessIdentifier: frontmostProcessIdentifier,
                ownProcessIdentifier: ownProcessIdentifier
            ),
        ]
    }

    static func holderIsInBackground(holder: SecureInputHolder?,
                                     frontmostProcessIdentifier: pid_t?,
                                     ownProcessIdentifier: pid_t) -> Bool {
        guard let holder, let frontmostProcessIdentifier else { return false }
        return holder.processIdentifier != ownProcessIdentifier
            && holder.processIdentifier != frontmostProcessIdentifier
    }

    static func detail(holder: SecureInputHolder?,
                       frontmostProcessIdentifier: pid_t?,
                       ownProcessIdentifier: pid_t) -> String {
        guard let holder else { return unknownHolderDetail }
        guard holderIsInBackground(
            holder: holder,
            frontmostProcessIdentifier: frontmostProcessIdentifier,
            ownProcessIdentifier: ownProcessIdentifier
        ) else { return foregroundDetail }
        return "由后台的「\(displayName(holder))」开启；退出它或离开其密码框后恢复"
    }
}

enum SecureInputDiagnostics {
    private static let rejectionLogInterval: TimeInterval = 10
    private static var lastRejectionLogUptime: TimeInterval?
    private static var lastElsewhereLogUptime: TimeInterval?

    /// The console session record names the process that enabled secure input.
    /// WindowServer attributes a helper process to its responsible application.
    static func holderProcessIdentifier() -> pid_t? {
        let root = IORegistryGetRootEntry(kIOMainPortDefault)
        guard root != 0 else { return nil }
        defer { IOObjectRelease(root) }
        guard let sessions = IORegistryEntryCreateCFProperty(
            root,
            "IOConsoleUsers" as CFString,
            kCFAllocatorDefault,
            0
        )?.takeRetainedValue() as? [[String: Any]] else { return nil }
        for session in sessions {
            if let holder = session["kCGSSessionSecureInputPID"] as? NSNumber {
                return holder.int32Value
            }
        }
        return nil
    }

    static func currentHolder() -> SecureInputHolder? {
        guard IsSecureEventInputEnabled(),
              let processIdentifier = holderProcessIdentifier() else { return nil }
        let application = NSRunningApplication(
            processIdentifier: processIdentifier
        )
        return SecureInputHolder(
            processIdentifier: processIdentifier,
            bundleID: application?.bundleIdentifier,
            name: application?.localizedName,
            isRegularApplication: application?.activationPolicy == .regular
        )
    }

    /// Menu lines for the current state, or nil while secure input is off.
    static func currentMenuNotice() -> [String]? {
        guard IsSecureEventInputEnabled() else { return nil }
        let holder = currentHolder()
        let frontmost = NSWorkspace.shared.frontmostApplication
        let ownProcessIdentifier = ProcessInfo.processInfo.processIdentifier
        return SecureInputNoticeRules.lines(
            holder: holder,
            frontmostProcessIdentifier: frontmost?.processIdentifier,
            holderIsElsewhere: SecureInputScopeRules.holderIsElsewhere(
                holder: holder,
                frontmostProcessIdentifier: frontmost?.processIdentifier,
                frontmostBundleID: frontmost?.bundleIdentifier,
                ownProcessIdentifier: ownProcessIdentifier,
                ownBundleID: SecureInputScope.ownBundleID
            ),
            ownProcessIdentifier: ownProcessIdentifier
        )
    }

    /// Composition continuing under secure input is the new, looser case, so
    /// it leaves a trace too: bundle identifiers only, once per interval.
    static func noteCompositionWhileHeldElsewhere(holder: SecureInputHolder?,
                                                  clientBundleID: String?) {
        guard Thread.isMainThread else { return }
        let now = ProcessInfo.processInfo.systemUptime
        if let last = lastElsewhereLogUptime,
           now - last >= 0,
           now - last < rejectionLogInterval {
            return
        }
        lastElsewhereLogUptime = now
        let holderPID = holder.map { String($0.processIdentifier) } ?? "unknown"
        IMELog.write(
            "protected input: secure input held in background; composing "
                + "holder=\(holderPID) bundle=\(holder?.bundleID ?? "unknown") "
                + "client=\(clientBundleID ?? "unknown")"
        )
    }

    /// A callback declined under protection used to leave no trace, so "only
    /// English comes out" could not be explained from the log. Record who
    /// holds secure input, at most once per interval. Main thread only.
    static func noteProtectedRejection() {
        guard IsSecureEventInputEnabled() else { return }
        let now = ProcessInfo.processInfo.systemUptime
        if let last = lastRejectionLogUptime,
           now - last >= 0,
           now - last < rejectionLogInterval {
            return
        }
        lastRejectionLogUptime = now
        let holder = currentHolder()
        let frontmost = NSWorkspace.shared.frontmostApplication
        let background = SecureInputNoticeRules.holderIsInBackground(
            holder: holder,
            frontmostProcessIdentifier: frontmost?.processIdentifier,
            ownProcessIdentifier: ProcessInfo.processInfo.processIdentifier
        )
        let holderPID = holder.map { String($0.processIdentifier) } ?? "unknown"
        IMELog.write(
            "protected input: callback declined; secure input holder=\(holderPID) "
                + "bundle=\(holder?.bundleID ?? "unknown") "
                + "frontmost=\(frontmost?.bundleIdentifier ?? "unknown") "
                + "background=\(background)"
        )
    }
}

/// Runtime side of `SecureInputScopeRules`, read fresh on every call: a cached
/// "elsewhere" answer could outlive the moment the app in front takes over.
enum SecureInputScope {
    static var ownBundleID: String {
        Bundle.main.bundleIdentifier ?? RimesIdentity.bundleIdentifier
    }

    static func holderIsElsewhere() -> Bool {
        let frontmost = NSWorkspace.shared.frontmostApplication
        return SecureInputScopeRules.holderIsElsewhere(
            holder: SecureInputDiagnostics.currentHolder(),
            frontmostProcessIdentifier: frontmost?.processIdentifier,
            frontmostBundleID: frontmost?.bundleIdentifier,
            ownProcessIdentifier: ProcessInfo.processInfo.processIdentifier,
            ownBundleID: ownBundleID
        )
    }

    static func isHeldElsewhere(forClientBundleID clientBundleID: String?) -> Bool {
        let holder = SecureInputDiagnostics.currentHolder()
        let frontmost = NSWorkspace.shared.frontmostApplication
        let protected = SecureInputScopeRules.protectsField(
            secureInput: true,
            holder: holder,
            frontmostProcessIdentifier: frontmost?.processIdentifier,
            frontmostBundleID: frontmost?.bundleIdentifier,
            clientBundleID: clientBundleID,
            ownProcessIdentifier: ProcessInfo.processInfo.processIdentifier,
            ownBundleID: ownBundleID
        )
        if !protected {
            SecureInputDiagnostics.noteCompositionWhileHeldElsewhere(
                holder: holder,
                clientBundleID: clientBundleID
            )
        }
        return !protected
    }
}

/// Secure input can briefly toggle during loginwindow activation. Its identity
/// is an independent boundary: no Rime key, focus lease or client write belongs
/// to the password field, even while the system flag is transitioning.
/// System secure input otherwise protects every field, unless
/// `SecureInputScopeRules` places its holder in another background app.
enum ProtectedInput {
    static let loginWindowBundleID = "com.apple.loginwindow"

    static func blocksIME(secureInput: Bool, clientBundleID: String?,
                          frontmostBundleID: String?,
                          secureInputHeldElsewhere: Bool = false) -> Bool {
        (secureInput && !secureInputHeldElsewhere)
            || clientBundleID == loginWindowBundleID
            || frontmostBundleID == loginWindowBundleID
    }

    static func isActive(clientBundleID: String? = nil) -> Bool {
        let secureInput = IsSecureEventInputEnabled()
        return blocksIME(
            secureInput: secureInput,
            clientBundleID: clientBundleID,
            frontmostBundleID: NSWorkspace.shared.frontmostApplication?.bundleIdentifier,
            secureInputHeldElsewhere: secureInput
                && SecureInputScope.isHeldElsewhere(
                    forClientBundleID: clientBundleID
                )
        )
    }

    static func isActive(client: IMKTextInput?) -> Bool {
        // Do not call a client at all once protection is known active: the
        // loginwindow is in front, or no background holder explains secure input.
        if NSWorkspace.shared.frontmostApplication?.bundleIdentifier
            == loginWindowBundleID {
            return true
        }
        if IsSecureEventInputEnabled(), !SecureInputScope.holderIsElsewhere() {
            return true
        }
        // Stale IMK callbacks can arrive after another input source takes over.
        // Do not inspect that proxy before the normal authority guard rejects it.
        guard RimeInputSourceAuthority.currentSourceIsOwn() else { return false }
        return isActive(clientBundleID: client?.bundleIdentifier())
    }
}
