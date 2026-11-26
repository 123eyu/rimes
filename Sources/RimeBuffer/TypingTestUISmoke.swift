import AppKit
import Foundation

/// In-process AppKit rendering and NSTextInputClient protocol checks. No input
/// source is selected, no real key events are posted, and storage is isolated.
func runTypingTestUISmokeTest(outputDirectory: URL? = nil) -> Bool {
    func fail(_ message: String) -> Bool { print("typing-test-ui-smoke: FAIL \(message)"); return false }
    guard Thread.isMainThread else { return fail("AppKit requires main thread") }
    let app = NSApplication.shared
    app.setActivationPolicy(.accessory)
    app.finishLaunching()
    let root = FileManager.default.temporaryDirectory.appendingPathComponent("rimes-typing-ui-\(UUID().uuidString)", isDirectory: true)
    defer { try? FileManager.default.removeItem(at: root) }
    do {
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        let store = TypingTestHistoryStore(storageRoot: root)
        let input = TypingTestTextView(frame: NSRect(x: 0, y: 0, width: 600, height: 140), textContainer: nil)
        input.acceptsTestInput = true
        input.isEditable = true
        var commits: [(String, Range<Int>?)] = []
        input.onCommittedText = { commits.append(($0, $1)) }
        input.setMarkedText("ni", selectedRange: NSRange(location: 2, length: 0), replacementRange: NSRange(location: NSNotFound, length: 0))
        input.setMarkedText("nihao", selectedRange: NSRange(location: 5, length: 0), replacementRange: NSRange(location: NSNotFound, length: 0))
        guard commits.isEmpty, input.hasMarkedText() else {
            return fail("marked text protocol: commits=\(commits.count), text=\(input.string), marked=\(input.markedRange()), selected=\(input.selectedRange())")
        }
        input.insertText("你好", replacementRange: NSRange(location: NSNotFound, length: 0))
        guard commits.count == 1, commits[0].0 == "你好", commits[0].1 == 0..<2 else { return fail("IME commit must publish exactly once with actual range") }
        input.setSelectedRange(NSRange(location: 0, length: 1))
        input.insertText("你", replacementRange: NSRange(location: NSNotFound, length: 0))
        guard commits.count == 2, commits.last?.1 == 0..<1 else { return fail("same-text replacement must remain observable") }
        input.setSelectedRange(NSRange(location: 2, length: 0))
        input.setMarkedText("hao", selectedRange: NSRange(location: 3, length: 0), replacementRange: NSRange(location: NSNotFound, length: 0))
        input.setMarkedText("", selectedRange: NSRange(location: 0, length: 0), replacementRange: NSRange(location: NSNotFound, length: 0))
        guard commits.count == 2, input.string == "你好" else { return fail("cancelled composition must not produce an attempt") }
        var assistance = 0
        input.onAssistedInput = { assistance += 1 }
        input.paste(nil)
        guard assistance == 1, input.string == "你好" else { return fail("paste must not mutate test") }
        input.insertText(String(repeating: "字", count: TypingTestSession.maximumCharacters + 1), replacementRange: NSRange(location: NSNotFound, length: 0))
        guard input.string == "你好", commits.count == 2 else { return fail("oversized commit must be rejected atomically") }

        let combining = TypingTestTextView(frame: NSRect(x: 0, y: 0, width: 600, height: 140), textContainer: nil)
        combining.acceptsTestInput = true
        combining.isEditable = true
        let accentArticle = TypingTestArticle(id: "smoke.accent", version: 1, language: .english,
                                              title: "accent", theme: "fixture", difficulty: "fixture", text: "é")
        let accentSession = TypingTestSession(article: accentArticle, context: TypingTestContext(schemaID: "smoke"))
        var accentClock: TimeInterval = 1
        var accentAccepted = true
        combining.onCommittedText = { text, range in
            accentAccepted = accentAccepted && accentSession.reconcileCommittedText(text, at: accentClock, explicitInsertionRange: range)
        }
        combining.insertText("e", replacementRange: NSRange(location: NSNotFound, length: 0))
        accentClock = 2
        combining.insertText("\u{0301}", replacementRange: NSRange(location: NSNotFound, length: 0))
        guard accentAccepted, combining.string == "é",
              accentSession.snapshot(at: 3).metrics.correctCharacterCount == 1,
              accentSession.snapshot(at: 3).metrics.attemptedCharacterCount == 2 else {
            return fail("separate combining-mark commit must score the resulting grapheme")
        }

        let article = TypingTestArticles.defaultArticle
        let lifecycleStore = TypingTestHistoryStore(storageRoot: root.appendingPathComponent("lifecycle"))
        var clock: TimeInterval = 100
        let lifecycle = TypingSpeedSettingsViewController(
            subpageID: "overview", historyStore: lifecycleStore,
            contextProvider: { TypingTestContext(schemaID: "smoke-lifecycle") },
            clockProvider: { clock }, telemetryEnabled: false
        )
        _ = lifecycle.view
        lifecycle.smokeStartByButton()
        guard lifecycle.smokeIsArmed, lifecycle.smokeHasTimer,
              lifecycle.smokeSessionSnapshot?.isStarted == false,
              lifecycle.smokeSessionSnapshot?.metrics.elapsedSeconds == 0 else {
            return fail("start button must only arm the test")
        }
        guard let key = NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [],
                                         timestamp: clock, windowNumber: 0, context: nil,
                                         characters: "a", charactersIgnoringModifiers: "a",
                                         isARepeat: false, keyCode: 0) else { return fail("local event fixture") }
        clock = 101
        lifecycle.practiceKey(key, isComposing: false)
        guard lifecycle.smokeSessionSnapshot?.isStarted == true,
              lifecycle.smokeSessionSnapshot?.metrics.physicalKeyCount == 1 else {
            return fail("first physical action must start timing")
        }
        clock = 161
        lifecycle.smokeEditor.insertText(article.text, replacementRange: NSRange(location: NSNotFound, length: 0))
        guard !lifecycle.smokeIsArmed, !lifecycle.smokeHasTimer,
              lifecycleStore.results.count == 1, lifecycleStore.results[0].isComplete else {
            return fail("complete commit must finish once and stop timer")
        }
        lifecycle.practiceKey(key, isComposing: false)
        lifecycle.smokeEditor.insertText("迟到", replacementRange: NSRange(location: NSNotFound, length: 0))
        lifecycle.smokeFinishByButton()
        guard lifecycleStore.results.count == 1, lifecycle.smokeEditor.string == article.text else {
            return fail("late input or duplicate finish must not rewrite results")
        }
        clock = 200
        lifecycle.smokeStartByButton()
        guard lifecycle.smokeEditor.string.isEmpty, lifecycle.smokeIsArmed,
              lifecycle.smokeHasTimer, lifecycle.smokeSessionSnapshot?.isStarted == false else {
            return fail("restart must clear editor and create a fresh armed session")
        }
        clock = 201
        lifecycle.practiceKey(key, isComposing: false)
        clock = 211
        lifecycle.smokeEditor.insertText(String(article.text.prefix(10)), replacementRange: NSRange(location: NSNotFound, length: 0))
        lifecycle.viewWillDisappear()
        guard !lifecycle.smokeIsArmed, !lifecycle.smokeHasTimer,
              lifecycleStore.results.count == 2,
              lifecycleStore.results.contains(where: { !$0.isComplete && $0.practiceReasons.contains(.interrupted) }) else {
            return fail("leaving the page must freeze an explicitly non-formal partial result")
        }
        clock = 300
        lifecycle.smokeStartByButton()
        lifecycle.practiceKey(key, isComposing: false)
        clock = 301
        lifecycle.smokeEditor.insertText(String(article.text.dropLast()), replacementRange: NSRange(location: NSNotFound, length: 0))
        // Simulate the NSTextView super.resignFirstResponder reentrant commit
        // between the real will-resign marker and its did-resign interruption.
        lifecycle.smokeEditor.onWillResignFocus?()
        clock = 311
        lifecycle.smokeEditor.insertText(String(article.text.suffix(1)), replacementRange: NSRange(location: NSNotFound, length: 0))
        lifecycle.smokeEditor.onFocusLost?()
        guard lifecycleStore.results.count == 3,
              lifecycleStore.results.contains(where: { $0.isComplete && $0.practiceReasons.contains(.focusLost) && !$0.isComparable }),
              !lifecycle.smokeHasTimer else {
            return fail("resign-time final commit must not become a formal result")
        }
        for number in 0..<4 {
            let session = TypingTestSession(article: article, context: TypingTestContext(schemaID: "smoke", mode: number == 0 ? .firstAttempt : .practice))
            session.start(at: 10)
            session.recordKey(at: 11)
            _ = session.reconcileCommittedText(article.text, at: 130 - Double(number) * 10)
            if let result = session.finish(at: 130 - Double(number) * 10, completedAt: Date(timeIntervalSince1970: 1_780_000_000 + Double(number) * 500)) {
                guard store.add(result) else { return fail("seed isolated result") }
            }
        }
        if let problem = typingTestTrackSmoke() { return fail(problem) }
        if let problem = typingTestSteadinessSmoke() { return fail(problem) }
        if let outputDirectory { try FileManager.default.createDirectory(at: outputDirectory, withIntermediateDirectories: true) }
        // Content areas the settings window can give a plugin page: the default
        // 980x680 window, its 860x600 minimum, and a large display.
        let sizes: [(name: String, size: NSSize)] = [("default", NSSize(width: 770, height: 500)),
                                                      ("minimum", NSSize(width: 650, height: 440)),
                                                      ("large", NSSize(width: 1180, height: 900))]
        for page in ["overview", "history"] {
            for (sizeName, size) in sizes {
                let controller = TypingSpeedSettingsViewController(subpageID: page, historyStore: store,
                                                                   library: TypingTestLibraryStore(storageRoot: root.appendingPathComponent("library-\(page)-\(sizeName)")))
                controller.prepareForSmoke(articleID: article.id, committedText: String(article.text.prefix(78)))
                let window = NSWindow(contentRect: NSRect(origin: .zero, size: size),
                                      styleMask: [.titled], backing: .buffered, defer: false)
                window.isReleasedWhenClosed = false
                window.appearance = RimeUI.appKitAppearance
                window.backgroundColor = RimeUI.surface
                window.setContentSize(size)
                let pane = controller.view
                // The settings shell supplies the page background; stand in for it here.
                let backdrop = TypingTestSmokeBackdrop(frame: NSRect(origin: .zero, size: size))
                window.contentView = backdrop
                backdrop.addSubview(pane)
                pane.frame = backdrop.bounds
                pane.autoresizingMask = [.width, .height]
                pane.layoutSubtreeIfNeeded()
                window.contentView?.layoutSubtreeIfNeeded()
                guard let scroll = controller.smokePageScroll, let document = scroll.documentView else {
                    return fail("\(page)/\(sizeName): page must be one scroll view")
                }
                if ProcessInfo.processInfo.environment["TT_DEBUG"] != nil { print("DBG \(page)/\(sizeName): window=\(window.frame.size) backdrop=\(backdrop.frame.size) pane=\(pane.frame.size) scroll=\(scroll.frame.size) doc=\(document.frame.size) clip=\(scroll.contentView.frame.size)") }
                func dump() -> String { "doc=\(document.frame) clip=\(scroll.contentView.bounds)" }
                guard document.frame.width >= scroll.contentView.bounds.width - 0.5 else { return fail("\(page)/\(sizeName): document narrower than viewport \(dump())") }
                guard controller.smokeMaterialRowCount == TypingTestArticles.all.count else { return fail("material column must list the built-in articles as cards") }
                if page == "overview" {
                    let editor = controller.smokeEditor
                    guard controller.smokeTargetString == article.scoredText else { return fail("\(page)/\(sizeName): source text missing") }
                    // No scroll view of its own: the text is as tall as all its rows and the page scrolls.
                    let rows = controller.smokeTrack.smokeRows.count
                    guard editor.enclosingScrollView === scroll, editor.bounds.width > 250,
                          editor.frame.height >= CGFloat(rows) * 70 else {
                        return fail("\(page)/\(sizeName): text must show every row inside the page scroll: rows \(rows) height \(editor.frame.height) \(dump())")
                    }
                    let bottom = editor.convert(editor.bounds, to: document).maxY
                    guard document.frame.height >= bottom - 0.5 else { return fail("\(page)/\(sizeName): document ends above the text \(dump()) bottom \(bottom)") }
                    scroll.contentView.scroll(to: NSPoint(x: 0, y: max(0, document.frame.height - scroll.contentView.bounds.height)))
                    scroll.reflectScrolledClipView(scroll.contentView)
                    guard scroll.contentView.bounds.maxY >= bottom - 0.5 else {
                        return fail("\(page)/\(sizeName): scrolling cannot reach the last row \(dump()) bottom \(bottom)")
                    }
                    scroll.contentView.scroll(to: .zero)
                    scroll.reflectScrolledClipView(scroll.contentView)
                    // Readings and the speed curve are cards of one band, at the same height.
                    guard let chart = controller.smokeChartFrame, let reading = controller.smokeReadingFrame,
                          abs(chart.minY - reading.minY) < 1, abs(chart.maxY - reading.maxY) < 1 else {
                        return fail("\(page)/\(sizeName): the speed curve must sit level with the reading cards")
                    }
                }
                pane.displayIfNeeded()
                window.displayIfNeeded()
                guard let bitmap = backdrop.bitmapImageRepForCachingDisplay(in: backdrop.bounds) else { return fail("bitmap surface") }
                backdrop.cacheDisplay(in: backdrop.bounds, to: bitmap)
                guard let png = bitmap.representation(using: .png, properties: [:]), !png.isEmpty else { return fail("PNG rendering") }
                if let outputDirectory {
                    let url = outputDirectory.appendingPathComponent("typing-test-\(page)-\(sizeName).png")
                    try png.write(to: url, options: .atomic)
                    print("typing-test-ui-smoke: rendered \(url.path)")
                }
                window.close()
            }
        }
        // Folding the material column and the stat cells gives the text the room.
        do {
            let controller = TypingSpeedSettingsViewController(subpageID: "overview", historyStore: store,
                                                               library: TypingTestLibraryStore(storageRoot: root.appendingPathComponent("library-fold")),
                                                               contextProvider: { TypingTestContext(schemaID: "smoke-fold") }, telemetryEnabled: false)
            let size = NSSize(width: 770, height: 500)
            let window = NSWindow(contentRect: NSRect(origin: .zero, size: size), styleMask: [.titled], backing: .buffered, defer: false)
            window.isReleasedWhenClosed = false
            window.appearance = RimeUI.appKitAppearance
            let backdrop = TypingTestSmokeBackdrop(frame: NSRect(origin: .zero, size: size))
            window.contentView = backdrop
            let pane = controller.view
            backdrop.addSubview(pane)
            pane.frame = backdrop.bounds
            pane.autoresizingMask = [.width, .height]
            controller.prepareForSmoke(articleID: article.id, committedText: String(article.text.prefix(300)))
            pane.layoutSubtreeIfNeeded()
            // Typing deep into the text scrolls the page so the row being typed stays in view.
            controller.smokeTrack.scrollCaretIntoView()
            if let scroll = controller.smokePageScroll, let document = scroll.documentView {
                let top = controller.smokeTrack.convert(NSPoint(x: 0, y: controller.smokeTrack.smokeCaretRowTop), to: document).y
                let clip = scroll.contentView.bounds
                guard clip.minY > 100, top >= clip.minY - 1, top + 70 <= clip.maxY + 1 else {
                    return fail("scroll: the row being typed must be in view: row top \(top), visible \(clip.minY)...\(clip.maxY)")
                }
            } else { return fail("scroll: page scroll missing") }
            let before = controller.smokeEditor.frame.width, widthBefore = controller.smokeMaterials.smokeWidth
            guard widthBefore > 100, !controller.smokeMaterialsButtonShown else { return fail("fold: material column should start open, width \(widthBefore)") }
            controller.smokeToggleMaterials()
            controller.smokeToggleCells()
            pane.layoutSubtreeIfNeeded()
            // Hidden means hidden: no rail, no divider; the header offers the way back.
            guard controller.smokeCellsHidden, controller.smokeMaterials.smokeWidth < 1, controller.smokeMaterials.isHidden,
                  !controller.smokeMaterialsDividerShown, controller.smokeMaterialsButtonShown else {
                return fail("fold: column width \(controller.smokeMaterials.smokeWidth), cellsHidden \(controller.smokeCellsHidden)")
            }
            guard controller.smokeEditor.frame.width > before + 150 else {
                return fail("fold: text should take the sidebar's room: \(before) -> \(controller.smokeEditor.frame.width)")
            }
            controller.smokeToggleMaterials()
            pane.layoutSubtreeIfNeeded()
            guard controller.smokeMaterials.smokeWidth > 100, !controller.smokeMaterialsButtonShown, controller.smokeMaterialsDividerShown else {
                return fail("fold: the sidebar must come back")
            }
            controller.smokeToggleMaterials()
            pane.layoutSubtreeIfNeeded()
            if let outputDirectory, let bitmap = backdrop.bitmapImageRepForCachingDisplay(in: backdrop.bounds) {
                backdrop.cacheDisplay(in: backdrop.bounds, to: bitmap)
                if let png = bitmap.representation(using: .png, properties: [:]) {
                    try png.write(to: outputDirectory.appendingPathComponent("typing-test-overview-folded.png"), options: .atomic)
                }
            }
            window.close()
        }
        // Imported books: real import through the controller, then the column and
        // an imported passage rendered at the default size.
        do {
            let libraryRoot = root.appendingPathComponent("library-import")
            let importLibrary = TypingTestLibraryStore(storageRoot: libraryRoot)
            let file = root.appendingPathComponent("fixture.epub")
            try typingTestSmokeFixture().epub.write(to: file)
            let controller = TypingSpeedSettingsViewController(subpageID: "overview", historyStore: store, library: importLibrary,
                                                               contextProvider: { TypingTestContext(schemaID: "smoke-import") },
                                                               telemetryEnabled: false)
            let size = NSSize(width: 770, height: 500)
            let window = NSWindow(contentRect: NSRect(origin: .zero, size: size), styleMask: [.titled], backing: .buffered, defer: false)
            window.isReleasedWhenClosed = false
            window.appearance = RimeUI.appKitAppearance
            let backdrop = TypingTestSmokeBackdrop(frame: NSRect(origin: .zero, size: size))
            window.contentView = backdrop
            let pane = controller.view
            backdrop.addSubview(pane)
            pane.frame = backdrop.bounds
            pane.autoresizingMask = [.width, .height]
            let rowsBefore = controller.smokeMaterialRowCount
            controller.importFiles([file])
            let deadline = Date().addingTimeInterval(10)
            while importLibrary.books.isEmpty, Date() < deadline { RunLoop.main.run(until: Date().addingTimeInterval(0.05)) }
            RunLoop.main.run(until: Date().addingTimeInterval(0.1))
            guard let book = importLibrary.books.first else { return fail("import through the controller produced no book") }
            guard controller.smokeMaterials.smokeTab == 1, controller.smokeMaterials.smokeOpenBookTitle == book.title,
                  controller.smokeMaterialRowCount == 1 + book.passageCount,
                  controller.smokeMaterials.currentArticleID == "epub.\(book.id).1",
                  controller.smokeTargetString == book.article(number: 1)?.scoredText else {
                return fail("import must open the book on the 书籍 tab and its first piece: rows \(rowsBefore)->\(controller.smokeMaterialRowCount), tab \(controller.smokeMaterials.smokeTab), current=\(String(describing: controller.smokeMaterials.currentArticleID))")
            }
            controller.importFiles([file])
            RunLoop.main.run(until: Date().addingTimeInterval(0.3))
            guard importLibrary.books.count == 1 else { return fail("importing the same file twice must not duplicate it") }
            pane.layoutSubtreeIfNeeded(); pane.displayIfNeeded()
            if let outputDirectory, let bitmap = backdrop.bitmapImageRepForCachingDisplay(in: backdrop.bounds) {
                backdrop.cacheDisplay(in: backdrop.bounds, to: bitmap)
                if let png = bitmap.representation(using: .png, properties: [:]) {
                    try png.write(to: outputDirectory.appendingPathComponent("typing-test-overview-book.png"), options: .atomic)
                }
            }
            window.close()
        }
        print("typing-test-ui-smoke: PASS marked/commit/cancel/replacement, input limits, article selection, history and default/minimum/large native layouts")
        return true
    } catch { return fail(error.localizedDescription) }
}

private final class TypingTestSmokeBackdrop: NSView {
    override func draw(_ dirtyRect: NSRect) {
        RimeUI.surface.setFill()
        dirtyRect.fill()
    }
}

/// The double-track view on its own: paragraph rows indented two cells, the
/// typed row mirroring them with breaks the view inserts itself, line breaks
/// never counted, and typed rows landing exactly under their source rows.
private func typingTestTrackSmoke() -> String? {
    let paragraphs = ["天地玄黄宇宙洪荒日月盈昃辰宿列张寒来暑往秋收", "冬藏闰余成岁律吕调阳云腾致雨", "露结为霜金生丽水"]
    let source = paragraphs.joined(separator: "\n")
    let flat = Array(paragraphs.joined())
    let view = TypingTestTrackTextView(frame: NSRect(x: 0, y: 0, width: 700, height: 400), textContainer: nil)
    view.acceptsTestInput = true
    view.isEditable = true
    view.setSource(source, states: [], typedCorrectness: [])
    let columns = view.columns
    guard columns == 24, view.smokeIndentCells == 2 else { return "track: columns \(columns) indent \(view.smokeIndentCells)" }
    // 24 columns, indent 2: the first paragraph exactly fills its row, each later one starts a row.
    let rows = view.smokeRows
    let first = paragraphs[0].count, second = paragraphs[1].count
    guard first == 22, rows.count == 3,
          rows[0] == (0, 22, 2), rows[1] == (22, 36, 2), rows[2] == (36, 44, 2) else { return "track: paragraph rows \(rows)" }

    func type(_ text: String) {
        for character in text { view.insertText(String(character), replacementRange: NSRange(location: NSNotFound, length: 0)) }
    }
    type(String(flat.prefix(first)))
    guard view.string.hasSuffix("\n"), TypingTestSession.normalized(view.string) == String(flat.prefix(first)),
          view.string.filter({ $0 == "\n" }).count == 1 else {
        return "track: no automatic break after the first paragraph: \(view.string.debugDescription)"
    }
    view.deleteBackward(nil)
    guard !view.string.hasSuffix("\n"), TypingTestSession.normalized(view.string).count == first - 1 else {
        return "track: backspace after a break must take the break and the character: \(view.string.debugDescription)"
    }
    type(String(flat[(first - 1)..<first]))
    guard view.string.hasSuffix("\n") else { return "track: break not restored" }
    type(String(flat[first..<(first + second)]))
    guard view.string.filter({ $0 == "\n" }).count == 2 else { return "track: no break after the second paragraph: \(view.string.debugDescription)" }
    type(String(flat[(first + second)...]))
    guard TypingTestSession.normalized(view.string) == String(flat), !view.string.hasSuffix("\n"),
          view.string.filter({ $0 == "\n" }).count == 2 else { return "track: typed text \(view.string.debugDescription)" }
    let tops = view.smokeTypedLineTops, rowTops = view.smokeTypedRowTops
    guard tops.count == rowTops.count else { return "track: \(tops.count) typed lines for \(rowTops.count) source rows" }
    for (index, pair) in zip(tops, rowTops).enumerated() where abs(pair.0 - pair.1) > 1.5 {
        return "track: typed line \(index) at \(pair.0), its source row expects \(pair.1)"
    }
    // Latin text, CJK punctuation runs and mixed text must still sit one character per cell.
    let mixed = TypingTestTrackTextView(frame: NSRect(x: 0, y: 0, width: 700, height: 400), textContainer: nil)
    mixed.acceptsTestInput = true
    mixed.isEditable = true
    let sample = "The quick brown fox AV fi 他说：“今天，好。”然后（注意！）好，，。。！？”“ Mixed中文English混排，ok。(a) 'b' 1,234.5"
    mixed.setSource(sample, states: [], typedCorrectness: [])
    for character in sample { mixed.insertText(String(character), replacementRange: NSRange(location: NSNotFound, length: 0)) }
    guard mixed.smokeWorstCellDrift < 0.05 else { return "track: typed cells drift \(mixed.smokeWorstCellDrift) cell from the source grid" }
    return nil
}

/// Typing through the real page: per keystroke at most one forward page scroll,
/// the candidate anchor exactly on the cell the next character goes into (also
/// at row ends), and nothing on the page moving while the live readings refresh.
private func typingTestSteadinessSmoke() -> String? {
    let temporary = FileManager.default.temporaryDirectory
    let controller = TypingSpeedSettingsViewController(
        subpageID: "overview",
        historyStore: TypingTestHistoryStore(storageRoot: temporary.appendingPathComponent("tt-steady-\(UUID())")),
        library: TypingTestLibraryStore(storageRoot: temporary.appendingPathComponent("tt-steady-lib-\(UUID())")),
        contextProvider: { TypingTestContext(schemaID: "steady") }, telemetryEnabled: false)
    let size = NSSize(width: 900, height: 520)
    let window = NSWindow(contentRect: NSRect(origin: .zero, size: size), styleMask: [.titled], backing: .buffered, defer: false)
    window.isReleasedWhenClosed = false
    defer { window.close() }
    let backdrop = TypingTestSmokeBackdrop(frame: NSRect(origin: .zero, size: size))
    window.contentView = backdrop
    let pane = controller.view
    backdrop.addSubview(pane)
    pane.frame = backdrop.bounds
    pane.autoresizingMask = [.width, .height]
    let article = TypingTestArticles.all[0]
    controller.prepareForSmoke(articleID: article.id)
    pane.layoutSubtreeIfNeeded()
    controller.smokeStartByButton()
    guard let scroll = controller.smokePageScroll else { return "steady: page scroll missing" }
    let editor = controller.smokeTrack
    var scrolls: [CGFloat] = []
    scroll.contentView.postsBoundsChangedNotifications = true
    let token = NotificationCenter.default.addObserver(forName: NSView.boundsDidChangeNotification, object: scroll.contentView, queue: nil) { _ in
        scrolls.append(scroll.contentView.bounds.minY)
    }
    defer { NotificationCenter.default.removeObserver(token) }
    func pump(_ seconds: TimeInterval = 0.002) { RunLoop.current.run(until: Date().addingTimeInterval(seconds)) }
    func frames() -> [ObjectIdentifier: NSRect] {
        var out: [ObjectIdentifier: NSRect] = [:]
        func walk(_ view: NSView) { out[ObjectIdentifier(view)] = view.convert(view.bounds, to: nil); view.subviews.forEach(walk) }
        walk(pane)
        return out
    }
    let none = NSRange(location: NSNotFound, length: 0)
    // Enough text for several row ends, a paragraph end and page scrolls.
    for (index, character) in TypingTestSession.normalized(article.text).prefix(130).enumerated() {
        scrolls = []
        let before = scroll.contentView.bounds.minY
        editor.setMarkedText("　", selectedRange: NSRange(location: 0, length: 0), replacementRange: none); pump()
        var actual = NSRange()
        let anchor = editor.firstRect(forCharacterRange: editor.markedRange(), actualRange: &actual)
        guard let cell = editor.smokeCursorCell else { return "steady: no cursor cell at \(index)" }
        let expected = window.convertToScreen(editor.convert(cell, to: nil))
        guard abs(anchor.minX - expected.minX) < 0.5, abs(anchor.minY - expected.minY) < 0.5, anchor.width >= 1 else {
            return "steady: candidate anchor \(anchor) is not the cursor cell \(expected) at character \(index)"
        }
        editor.insertText(String(character), replacementRange: none); pump()
        guard scrolls.count <= 1, scrolls.allSatisfy({ $0 >= before }) else {
            return "steady: page scrolled \(scrolls) from \(before) for character \(index)"
        }
    }
    // A reading changing width (— → 99.5 → 123 → 1234) must not slide its unit.
    let tile = TypingTestStatTile(title: "有效速度", symbolName: "speedometer")
    let host = NSView(frame: NSRect(x: 0, y: 0, width: 160, height: TypingTestStatTile.height))
    host.addSubview(tile)
    NSLayoutConstraint.activate([tile.leadingAnchor.constraint(equalTo: host.leadingAnchor), tile.widthAnchor.constraint(equalToConstant: 152),
                                 tile.topAnchor.constraint(equalTo: host.topAnchor)])
    window.contentView?.addSubview(host)
    let unitXs = ["—", "99.5", "123", "1234"].map { value -> CGFloat in tile.update(value: value, unit: "字/分"); return tile.smokeUnitX }
    host.removeFromSuperview()
    guard unitXs.allSatisfy({ abs($0 - unitXs[0]) < 0.5 }) else { return "steady: stat unit slides with the reading: \(unitXs)" }
    // While composing and idle, the 0.25 s refresh changes readings but must move nothing.
    editor.setMarkedText("　", selectedRange: NSRange(location: 0, length: 0), replacementRange: none)
    pane.layoutSubtreeIfNeeded()
    let idle = frames()
    pump(0.6)
    pane.layoutSubtreeIfNeeded()
    let now = frames(); let moved = now.keys.filter { key in idle[key].map { $0 != now[key] } ?? false }.count
    guard moved == 0 else { return "steady: \(moved) views moved during the live refresh" }
    return nil
}
