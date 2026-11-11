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
}

/// Wording for the input-source menu while secure input pauses composition.
/// A holder that is neither this process nor the application in front is the
/// case a user cannot see for themselves: some other application left secure
/// input on, so every ordinary field silently receives raw keys.
enum SecureInputNoticeRules {
    static let headline = "⚠️ 安全输入已开启 — 中文输入暂停"
    static let foregroundDetail = "当前应用正在保护密码输入，离开密码框后自动恢复"
    static let unknownHolderDetail = "未能识别开启它的应用；若并未输入密码，请逐个退出后台应用排查"

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
        let name = holder.name ?? holder.bundleID
            ?? "进程 \(holder.processIdentifier)"
        return "由后台的「\(name)」开启；退出它或离开其密码框后恢复"
    }
}

enum SecureInputDiagnostics {
    private static let rejectionLogInterval: TimeInterval = 10
    private static var lastRejectionLogUptime: TimeInterval?

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
            name: application?.localizedName
        )
    }

    /// Menu lines for the current state, or nil while secure input is off.
    static func currentMenuNotice() -> [String]? {
        guard IsSecureEventInputEnabled() else { return nil }
        return [
            SecureInputNoticeRules.headline,
            SecureInputNoticeRules.detail(
                holder: currentHolder(),
                frontmostProcessIdentifier: NSWorkspace.shared
                    .frontmostApplication?.processIdentifier,
                ownProcessIdentifier: ProcessInfo.processInfo.processIdentifier
            ),
        ]
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

/// Secure input can briefly toggle during loginwindow activation. Its identity
/// is an independent boundary: no Rime key, focus lease or client write belongs
/// to the password field, even while the system flag is transitioning.
enum ProtectedInput {
    static func blocksIME(secureInput: Bool, clientBundleID: String?,
                          frontmostBundleID: String?) -> Bool {
        secureInput || clientBundleID == "com.apple.loginwindow"
            || frontmostBundleID == "com.apple.loginwindow"
    }

    static func isActive(clientBundleID: String? = nil) -> Bool {
        blocksIME(secureInput: IsSecureEventInputEnabled(),
                  clientBundleID: clientBundleID,
                  frontmostBundleID: NSWorkspace.shared.frontmostApplication?.bundleIdentifier)
    }

    static func isActive(client: IMKTextInput?) -> Bool {
        // Do not call a client at all once system protection is known active.
        if isActive() { return true }
        // Stale IMK callbacks can arrive after another input source takes over.
        // Do not inspect that proxy before the normal authority guard rejects it.
        guard RimeInputSourceAuthority.currentSourceIsOwn() else { return false }
        return isActive(clientBundleID: client?.bundleIdentifier())
    }
}
