import AppKit

/// An input-method process has no main menu, so the Edit menu's key
/// equivalents never reach text fields in its own windows: Command-V in a
/// Settings field did nothing. Map those chords to the action an Edit menu
/// would send and hand it to the window's own text responder.
enum StandardEditShortcutRules {
    static func action(characters: String?,
                       modifierFlags: NSEvent.ModifierFlags) -> Selector? {
        let modifiers = modifierFlags
            .intersection([.command, .control, .option, .shift])
        switch (characters?.lowercased(), modifiers) {
        case ("x", [.command]): return #selector(NSText.cut(_:))
        case ("c", [.command]): return #selector(NSText.copy(_:))
        case ("v", [.command]): return #selector(NSText.paste(_:))
        case ("a", [.command]): return #selector(NSText.selectAll(_:))
        case ("z", [.command]): return Selector(("undo:"))
        case ("z", [.command, .shift]): return Selector(("redo:"))
        default: return nil
        }
    }

    /// Only a text responder receives these, so buttons, tables and custom
    /// views keep whatever the chord already means to them.
    static func perform(_ event: NSEvent, in window: NSWindow) -> Bool {
        guard event.type == .keyDown,
              let action = action(
                characters: event.charactersIgnoringModifiers,
                modifierFlags: event.modifierFlags
              ),
              let responder = window.firstResponder as? NSText else {
            return false
        }
        return responder.tryToPerform(action, with: window)
    }

    /// Settings and the sheets attached to it host ordinary AppKit fields.
    /// The workbench, Capsule and the clipboard rail route these chords
    /// themselves and are left alone.
    static func hostsOrdinaryTextFields(_ window: NSWindow) -> Bool {
        window.delegate is SettingsWindowController
            || window.sheetParent?.delegate is SettingsWindowController
    }
}

/// Zero-size view that joins its window's key-equivalent pass, which is where
/// an Edit menu would have answered.
private final class EditShortcutRelayView: NSView {
    override func performKeyEquivalent(with event: NSEvent) -> Bool {
        guard let window else { return false }
        return StandardEditShortcutRules.perform(event, in: window)
    }
}

/// Attaches the relay from outside `SettingsWindow.swift`: the Linux settings
/// contracts pin that file's digest, so its window class stays as it is.
/// The key-equivalent pass runs after local event monitors, which keeps the
/// shortcut recorder's capture ahead of these chords.
enum StandardEditShortcutRelay {
    private static var observer: NSObjectProtocol?

    static func install() {
        guard observer == nil else { return }
        observer = NotificationCenter.default.addObserver(
            forName: NSWindow.didBecomeKeyNotification,
            object: nil,
            queue: .main
        ) { notification in
            guard let window = notification.object as? NSWindow,
                  StandardEditShortcutRules.hostsOrdinaryTextFields(window) else {
                return
            }
            attach(to: window)
        }
    }

    /// Idempotent: one relay per window.
    static func attach(to window: NSWindow) {
        guard let contentView = window.contentView,
              !contentView.subviews.contains(where: { $0 is EditShortcutRelayView }) else {
            return
        }
        contentView.addSubview(EditShortcutRelayView(frame: .zero))
    }
}
