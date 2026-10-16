import Cocoa
import Carbon
import InputMethodKit

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
