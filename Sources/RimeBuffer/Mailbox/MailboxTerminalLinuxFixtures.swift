import Cocoa
import CryptoKit

/// Actual AppKit/SwiftTerm output, isolated from accounts and user Mailbox data.
@MainActor enum MailboxTerminalLinuxFixtures {
    static func export(to output: URL) throws {
        let manager = FileManager.default
        let root = manager.temporaryDirectory.appendingPathComponent("xsime-terminal-native-\(UUID())")
        let workspace = URL(fileURLWithPath: "/tmp/xsime-terminal-public-workspace", isDirectory: true)
        guard !manager.fileExists(atPath: workspace.path) else { throw CocoaError(.fileWriteFileExists) }
        let prior = getenv("RIMEBUFFER_USER_DIR").map { String(cString: $0) }
        let appearanceMode = getenv("RIMEBUFFER_APPEARANCE_MODE").map { String(cString: $0) }
        var ownsWorkspace = false
        setenv("RIMEBUFFER_USER_DIR", root.path, 1)
        defer {
            try? manager.removeItem(at: root)
            if ownsWorkspace { try? manager.removeItem(at: workspace) }
            if let prior { setenv("RIMEBUFFER_USER_DIR", prior, 1) } else { unsetenv("RIMEBUFFER_USER_DIR") }
            if let appearanceMode { setenv("RIMEBUFFER_APPEARANCE_MODE", appearanceMode, 1) } else { unsetenv("RIMEBUFFER_APPEARANCE_MODE") }
            OfficialPluginSmokePreferences.clear()
        }
        try manager.createDirectory(at: root, withIntermediateDirectories: true, attributes: [.posixPermissions: 0o700])
        try manager.createDirectory(at: workspace, withIntermediateDirectories: false, attributes: [.posixPermissions: 0o700])
        ownsWorkspace = true
        try manager.createDirectory(at: output, withIntermediateDirectories: true)
        let text = "这是一份公开的终端成果。\n\n选择文件、预览内容，再手动收入收件箱。"
        let file = workspace.appendingPathComponent("公开成果.txt")
        try Data(text.utf8).write(to: file)
        let command = "printf '\\033]0;公开终端\\007公开终端 · PTY ready\\n\\n输入会直接进入当前会话。\\n'; read line; printf '终端已退出。\\n'; exit 7"
        let app = NSApplication.shared; app.setActivationPolicy(.accessory); app.finishLaunching()
        var renders: [[String: Any]] = []
        func pump(_ seconds: TimeInterval = 0.15) { RunLoop.main.run(until: Date().addingTimeInterval(seconds)) }
        for theme in ["night", "day"] {
            setenv("RIMEBUFFER_APPEARANCE_MODE", theme, 1)
            let store = try MailboxStore(storageRoot: root.appendingPathComponent(theme))
            let host = MailboxWorkspaceViewController(store: store, remembersModule: false, isEnabled: { _ in true })
            let window = NSWindow(contentRect: NSRect(origin: .zero, size: MailboxUI.defaultSize), styleMask: [.titled, .resizable], backing: .buffered, defer: false)
            window.isReleasedWhenClosed = false; window.appearance = RimeUI.appKitAppearance
            window.contentViewController = host; window.orderFront(nil); host.selectModule(.terminal)
            func render(_ scene: String) throws {
                for (sizeName, size) in [("wide", MailboxUI.defaultSize), ("compact", MailboxUI.minimumSize)] {
                    window.setContentSize(size); pump(); host.view.layoutSubtreeIfNeeded()
                    func display(_ view: NSView) { view.needsDisplay = true; for child in view.subviews { display(child) }; view.displayIfNeeded() }
                    display(host.view)
                    guard let bitmap = host.view.bitmapImageRepForCachingDisplay(in: host.view.bounds) else { throw CocoaError(.coderInvalidValue) }
                    host.view.cacheDisplay(in: host.view.bounds, to: bitmap)
                    if let session = host.terminal.sessions.first {
                        // SwiftTerm clears the parent cache region. Capture its real
                        // child view separately and composite at its AppKit frame;
                        // no terminal text, glyph or geometry is reconstructed.
                        let terminal = session.terminal
                        guard let isolated = terminal.bitmapImageRepForCachingDisplay(in: terminal.bounds),
                              let context = NSGraphicsContext(bitmapImageRep: bitmap) else { throw CocoaError(.coderInvalidValue) }
                        terminal.cacheDisplay(in: terminal.bounds, to: isolated)
                        let frame = terminal.convert(terminal.bounds, to: host.view)
                        NSGraphicsContext.saveGraphicsState(); NSGraphicsContext.current = context
                        terminal.nativeBackgroundColor.setFill(); frame.fill()
                        let image = NSImage(size: terminal.bounds.size); image.addRepresentation(isolated)
                        image.draw(in: frame, from: .zero, operation: .sourceOver, fraction: 1)
                        NSGraphicsContext.restoreGraphicsState()
                    }
                    guard let png = bitmap.representation(using: .png, properties: [:]) else { throw CocoaError(.coderInvalidValue) }
                    let name = "\(theme)-terminal-\(scene)-\(sizeName).png"
                    try png.write(to: output.appendingPathComponent(name), options: .atomic)
                    renders.append(["image": name, "logicalWidth": size.width, "logicalHeight": size.height])
                }
            }
            try render("empty")
            window.setContentSize(MailboxUI.defaultSize); pump()
            let session = host.terminal.launch(kind: .shell, workspace: workspace, executable: URL(fileURLWithPath: "/bin/sh"), arguments: ["-c", command])
            defer { session.stop(); window.close() }
            let deadline = Date().addingTimeInterval(5)
            while !String(decoding: session.terminal.getTerminal().getBufferAsData(), as: UTF8.self).contains("PTY ready") && Date() < deadline { pump(0.05) }
            guard session.running,
                  String(decoding: session.terminal.getTerminal().getBufferAsData(), as: UTF8.self).contains("PTY ready") else { throw CocoaError(.executableLoad) }
            try render("running")
            let decoded = try MailboxCollectedFile.read(file)
            guard decoded.body == text && decoded.pngData == nil else { throw CocoaError(.coderInvalidValue) }
            host.terminal.preview(file); try render("file")
            host.terminal.collectSelection(); try render("receipt")
            guard store.snapshot.threads.last?.terminalSessionID == session.id else { throw CocoaError(.coderInvalidValue) }
            session.terminal.send(txt: "完成\n")
            let exitDeadline = Date().addingTimeInterval(5)
            while session.running && Date() < exitDeadline { pump(0.05) }
            guard session.exitCode == 7 else { throw CocoaError(.executableLoad) }
            try render("exited")
        }
        let repo = URL(fileURLWithPath: manager.currentDirectoryPath)
        var sources: [String: String] = [:]
        for path in ["Sources/RimeBuffer/Mailbox/MailboxTerminalWorkspace.swift", "Sources/RimeBuffer/Mailbox/MailboxAdaptiveSplitView.swift", "Sources/RimeBuffer/Mailbox/MailboxWorkspaceUI.swift", "Sources/RimeBuffer/Mailbox/MailboxTerminalLinuxFixtures.swift", "Sources/RimeBuffer/CodexSession/CodexProcessEnvironment.swift"] {
            sources[path] = SHA256.hash(data: try Data(contentsOf: repo.appendingPathComponent(path))).map { String(format: "%02x", $0) }.joined()
        }
        let contract: [String: Any] = ["purpose": "public-mailbox-terminal-contract", "schemaVersion": 1, "sources": sources,
            "capture": "AppKit cacheDisplay; actual SwiftTerm child cache composed at its native frame with its layer background",
            "kinds": MailboxTerminalKind.allCases.map(\.rawValue), "directory": workspace.path, "command": command, "fileName": file.lastPathComponent, "fileBody": text,
            "metrics": ["fontSize": MailboxTerminalRules.fontSize, "terminalInset": MailboxTerminalRules.terminalInset,
                "sidebarWidth": MailboxTerminalRules.sidebarWidth, "sidebarBreakpoint": MailboxTerminalRules.sidebarBreakpoint,
                "imageHeight": MailboxTerminalRules.imageHeight],
            "limits": ["maximumFileBytes": MailboxTerminalRules.maximumFileBytes, "maximumTextBytes": MailboxTerminalRules.maximumTextBytes,
                "maximumImageDimension": MailboxTerminalRules.maximumImageDimension, "maximumImagePixels": MailboxTerminalRules.maximumImagePixels,
                "maximumFiles": MailboxTerminalRules.maximumFiles, "previewCharacters": MailboxTerminalRules.previewCharacters], "renders": renders]
        try JSONSerialization.data(withJSONObject: contract, options: [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes]).write(to: output.appendingPathComponent("native-terminal.json"), options: .atomic)
        print("Actual AppKit/SwiftTerm terminal fixtures: \(renders.count) renders; preview, atomic receipt, UUID association and exit status verified")
    }
}
