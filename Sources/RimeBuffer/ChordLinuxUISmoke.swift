import AppKit
import Foundation

enum ChordLinuxRenderGeometry {
    static func save(_ pane: NSView, beside output: URL) throws {
        var frames: [[String: Any]] = []
        func visit(_ view: NSView) {
            let frame = view.convert(view.bounds, to: pane)
            var item: [String: Any] = ["type": NSStringFromClass(type(of: view)),
                                      "x": frame.minX, "y": pane.isFlipped ? frame.minY : pane.bounds.height - frame.maxY,
                                      "width": frame.width, "height": frame.height]
            if let field = view as? NSTextField { item["text"] = field.stringValue; item["fontSize"] = field.font?.pointSize }
            if let button = view as? NSButton { item["text"] = button.title; item["fontSize"] = button.font?.pointSize }
            frames.append(item)
            view.subviews.forEach(visit)
        }
        visit(pane)
        try JSONSerialization.data(withJSONObject: frames, options: [.prettyPrinted, .sortedKeys])
            .write(to: output.appendingPathExtension("geometry.json"), options: .atomic)
    }
}

func runChordLinuxUISmoke(output: URL) -> Bool {
    let manager = FileManager.default
    let root = manager.temporaryDirectory.appendingPathComponent("chord-linux-render-\(UUID().uuidString)")
    let previous = ProcessInfo.processInfo.environment["RIMEBUFFER_APPEARANCE_MODE"]
    defer {
        try? manager.removeItem(at: root)
        if let previous { setenv("RIMEBUFFER_APPEARANCE_MODE", previous, 1) }
        else { unsetenv("RIMEBUFFER_APPEARANCE_MODE") }
    }
    do {
        let app = NSApplication.shared
        app.setActivationPolicy(.accessory)
        app.finishLaunching()
        try manager.createDirectory(at: output, withIntermediateDirectories: true)
        let repo = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent()
        let schema = try FlyChordSchemaParser.load(from: repo.appendingPathComponent("rime-data/my_combo.schema.yaml"))
        let progress = try FlyChordProgressStore(storageRoot: root)
        for theme in ["night", "day", "quiet", "rasta"] {
            setenv("RIMEBUFFER_APPEARANCE_MODE", theme, 1)
            guard runChordKeymapEditorSmokeTest(outputURL: output.appendingPathComponent("keymap-\(theme).png")) else { return false }
            for page in ["settings", "lessons", "practice", "progress"] {
                let controller = FlyChordLearningSettingsViewController(subpageID: page, schema: schema, progressStore: progress)
                let pane: NSView
                if page == "settings" {
                    pane = FlyChordConfigurationPageView(stateProvider: {
                        FlyChordConfigurationState(isEnabled: false, implementationName: "飞耀输入 · 并击",
                                                   isCurrent: false, duration: 0.1)
                    }, onDuration: { _ in }, onReset: {}, onMakeCurrent: { false }, observesChanges: false)
                } else { pane = controller.view }
                pane.wantsLayer = true
                pane.layer?.backgroundColor = RimeUI.surface.cgColor
                let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 698, height: 600),
                                      styleMask: .borderless, backing: .buffered, defer: false)
                window.isReleasedWhenClosed = false
                window.appearance = RimeUI.appKitAppearance
                window.backgroundColor = RimeUI.surface
                window.contentView = pane
                pane.layoutSubtreeIfNeeded()
                window.setContentSize(NSSize(width: 698, height: ceil(pane.fittingSize.height)))
                pane.layoutSubtreeIfNeeded()
                pane.displayIfNeeded()
                guard let bitmap = pane.bitmapImageRepForCachingDisplay(in: pane.bounds) else { window.close(); return false }
                pane.cacheDisplay(in: pane.bounds, to: bitmap)
                guard let data = bitmap.representation(using: .png, properties: [:]) else { window.close(); return false }
                let destination = output.appendingPathComponent("\(page)-\(theme).png")
                try data.write(to: destination, options: .atomic)
                try ChordLinuxRenderGeometry.save(pane, beside: destination)
                window.close()
            }
        }
        print("chord-linux-ui-smoke: PASS 20 actual native page renders with isolated progress and public schemas")
        return true
    } catch { print("chord-linux-ui-smoke: FAIL \(error.localizedDescription)"); return false }
}
