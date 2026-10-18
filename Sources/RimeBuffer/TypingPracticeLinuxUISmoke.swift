import AppKit
import Foundation

/// Captures real controls and actual local editor lifecycle using public text.
func runTypingPracticeLinuxUISmoke(_ output: URL) -> Bool {
    let root = FileManager.default.temporaryDirectory.appendingPathComponent("typing-linux-ui-\(UUID().uuidString)")
    let previous = ProcessInfo.processInfo.environment["RIMEBUFFER_APPEARANCE_MODE"]
    defer {
        try? FileManager.default.removeItem(at: root)
        if let previous { setenv("RIMEBUFFER_APPEARANCE_MODE", previous, 1) } else { unsetenv("RIMEBUFFER_APPEARANCE_MODE") }
    }
    do {
        let app = NSApplication.shared; app.setActivationPolicy(.accessory); app.finishLaunching()
        try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
        func encoded<T: Encodable>(_ value: T) throws -> Any { try JSONSerialization.jsonObject(with: JSONEncoder().encode(value)) }
        let context = TypingTestContext(schemaID: "rime_ice", mode: .practice, inputSourceID: "public.source")
        var count = 0
        for theme in ["night", "day", "quiet", "rasta"] {
            setenv("RIMEBUFFER_APPEARANCE_MODE", theme, 1)
            for id in ["rimes.morning-street", "rimes.english-morning"] {
                let article = TypingTestArticles.article(id: id)!
                for phase in ["ready", "live", "complete", "history"] {
                    let name = "\(article.language.rawValue)-\(phase)-\(theme)"
                    let store = TypingTestHistoryStore(storageRoot: root.appendingPathComponent(name))
                    for number in 0..<4 {
                        let sample = TypingTestSession(article: article, context: context)
                        sample.start(at: 10); sample.recordKey(at: 11)
                        _ = sample.reconcileCommittedText(article.text, at: 130 - Double(number) * 10)
                        if number == 2 { sample.markPractice(reason: .focusLost) }
                        guard let result = sample.finish(at: 130 - Double(number) * 10, completedAt: Date(timeIntervalSince1970: 1_780_000_000 + Double(number) * 500)), store.add(result) else { return false }
                    }
                    var clock: Double = 10
                    let controller = TypingSpeedSettingsViewController(subpageID: phase == "history" ? "history" : "overview", historyStore: store,
                        contextProvider: { context }, clockProvider: { clock }, telemetryEnabled: false)
                    controller.prepareForSmoke(articleID: id)
                    let pane = controller.view
                    pane.wantsLayer = true; pane.layer?.backgroundColor = RimeUI.surface.cgColor
                    let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 698, height: 1000), styleMask: .borderless, backing: .buffered, defer: false)
                    window.isReleasedWhenClosed = false; window.appearance = RimeUI.appKitAppearance; window.backgroundColor = RimeUI.surface
                    window.contentViewController = controller
                    if phase == "live" || phase == "complete" {
                        controller.smokeStartByButton()
                        let length = phase == "live" ? 78 : article.characterCount
                        for i in 0..<length {
                            clock = 10 + Double(i) * 0.08
                            guard let event = NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [], timestamp: clock, windowNumber: 0, context: nil,
                                characters: "a", charactersIgnoringModifiers: "a", isARepeat: false, keyCode: 0) else { return false }
                            controller.practiceKey(event, isComposing: false)
                        }
                        clock = phase == "live" ? 20 : 130
                        controller.smokeEditor.insertText(String(article.text.prefix(length)), replacementRange: NSRange(location: NSNotFound, length: 0))
                        if phase == "complete" && controller.smokeIsArmed { return false }
                    }
                    pane.layoutSubtreeIfNeeded()
                    let height = ceil(pane.fittingSize.height)
                    guard height.isFinite, (450...2400).contains(height) else { return false }
                    window.setContentSize(NSSize(width: 698, height: height)); pane.layoutSubtreeIfNeeded(); pane.displayIfNeeded(); window.displayIfNeeded()
                    guard let bitmap = pane.bitmapImageRepForCachingDisplay(in: pane.bounds) else { return false }
                    pane.cacheDisplay(in: pane.bounds, to: bitmap)
                    guard let png = bitmap.representation(using: .png, properties: [:]) else { return false }
                    let image = output.appendingPathComponent(name + ".png")
                    try png.write(to: image, options: .atomic); try ChordLinuxRenderGeometry.save(pane, beside: image)
                    var fixture: [String: Any] = ["purpose": "public-typing-practice-dashboard", "theme": theme, "phase": phase, "articleID": id,
                        "context": try encoded(context), "history": try encoded(store.results), "clock": clock,
                        "editor": controller.smokeEditor.string, "width": 698, "height": height]
                    if let snapshot = controller.smokeSessionSnapshot {
                        fixture["metrics"] = try encoded(snapshot.metrics)
                        fixture["targetStates"] = snapshot.targetStates.map { value -> String in
                            switch value { case .pending: return "pending"; case .correct: return "correct"; case .incorrect: return "incorrect"; case .omitted: return "omitted" }
                        }
                    }
                    try JSONSerialization.data(withJSONObject: fixture, options: [.prettyPrinted, .sortedKeys]).write(to: image.appendingPathExtension("fixture.json"), options: .atomic)
                    window.close(); count += 1
                }
            }
        }
        print("typing-practice-linux-ui: PASS \(count) actual native English/Chinese ready/live/complete/history renders")
        return true
    } catch { print("typing-practice-linux-ui: FAIL \(error)"); return false }
}
