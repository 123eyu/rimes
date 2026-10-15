import AppKit
import Carbon.HIToolbox

/// Keeps a way back to RIMES visible when macOS omits a registered input mode
/// from its own input-source menu after switching to another method.
@MainActor
final class RIMESRecoveryStatusItem: NSObject {
    static let shared = RIMESRecoveryStatusItem()

    private var item: NSStatusItem?
    var isVisibleForSmoke: Bool { item != nil }

    func refresh(isSelected: Bool) {
        if isSelected {
            if let item {
                NSStatusBar.system.removeStatusItem(item)
                self.item = nil
            }
            return
        }
        guard item == nil else { return }
        let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
        if let button = item.button {
            if let path = Bundle.main.path(forResource: "etinput-menu", ofType: "pdf"),
               let image = NSImage(contentsOfFile: path) {
                image.size = NSSize(width: 17, height: 17)
                image.isTemplate = true
                button.image = image
                button.imagePosition = .imageOnly
            } else {
                button.title = "R"
            }
            button.toolTip = "切换到 \(ProductBrand.name) 输入法"
            button.setAccessibilityLabel("\(ProductBrand.name) 输入法快捷入口")
        }
        let menu = NSMenu(title: "\(ProductBrand.name)")
        let select = NSMenuItem(
            title: "切换到 \(ProductBrand.name)",
            action: #selector(selectRIMES(_:)), keyEquivalent: ""
        )
        select.target = self
        menu.addItem(select)
        let settings = NSMenuItem(
            title: "打开 \(ProductBrand.name) 设置…",
            action: #selector(openSettings(_:)), keyEquivalent: ""
        )
        settings.target = self
        menu.addItem(settings)
        item.menu = menu
        self.item = item
    }

    @objc private func selectRIMES(_ sender: Any?) {
        let modeID = RimesIdentity.bundleIdentifier + ".Hans"
        let filter = [kTISPropertyInputSourceID as String: modeID] as CFDictionary
        guard let sources = TISCreateInputSourceList(filter, false)?
                .takeRetainedValue() as? [TISInputSource],
              let source = sources.first else {
            IMELog.write("recovery status item: RIMES mode unavailable")
            NSSound.beep()
            return
        }
        let status = TISSelectInputSource(source)
        IMELog.write("recovery status item: select RIMES status=\(status)")
        if status != noErr { NSSound.beep() }
    }

    @objc private func openSettings(_ sender: Any?) {
        SettingsWindowController.shared.show()
    }
}
