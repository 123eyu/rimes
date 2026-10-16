import Cocoa

func runInputRepairSmokeTest() -> Bool {
    func fail(_ reason: String) -> Bool { print("input-repair-smoke: FAIL \(reason)"); return false }
    func event(_ text: String, key: UInt16, flags: NSEvent.ModifierFlags = .shift) -> NSEvent {
        NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: flags,
            timestamp: 0, windowNumber: 0, context: nil, characters: text,
            charactersIgnoringModifiers: text, isARepeat: false, keyCode: key)!
    }
    for secure in [false, true] {
        for client in [nil, "com.apple.loginwindow", "com.apple.TextEdit"] as [String?] {
            for front in [nil, "com.apple.loginwindow", "com.apple.TextEdit"] as [String?] {
                let expected = secure || client == "com.apple.loginwindow" || front == "com.apple.loginwindow"
                guard ProtectedInput.blocksIME(secureInput: secure,
                    clientBundleID: client, frontmostBundleID: front) == expected
                else { return fail("protected input boundary") }
            }
        }
    }
    let symbols: [(String, UInt16)] = [("?",44),("^",22),("!",18),(":",41),("(",25),(")",29),("<",43),(">",47)]
    for (text, key) in symbols {
        guard MacShiftKeyRouting.directText(for: event(text, key: key)) == nil,
              MacShiftKeyRouting.symbolKeysym(for: event(text, key: key)) == Int32(text.utf8.first!)
        else { return fail("Shift symbol \(text)") }
    }
    guard MacShiftKeyRouting.directText(for: event("A", key: 0)) == "A",
          MacShiftKeyRouting.directText(for: event("\r", key: 36)) == nil,
          MacShiftKeyRouting.symbolKeysym(for: event("\r", key: 36)) == nil
    else { return fail("uppercase and editing keys") }
    for modifier in [NSEvent.ModifierFlags.command, .control, .option, .function] {
        guard MacShiftKeyRouting.symbolKeysym(for: event("^", key: 22, flags: [.shift, modifier])) == nil,
              MacShiftKeyRouting.directText(for: event("A", key: 0, flags: [.shift, modifier])) == nil
        else { return fail("modified shortcut") }
    }

    let root = FileManager.default.temporaryDirectory.appendingPathComponent("rimes-health-\(UUID().uuidString)")
    defer { try? FileManager.default.removeItem(at: root) }
    do {
        let bad = root.appendingPathComponent("bad.userdb")
        let good = root.appendingPathComponent("good.userdb")
        for path in [bad, good] { try FileManager.default.createDirectory(at: path, withIntermediateDirectories: true) }
        try "Compaction error: Corruption: corrupted compressed block contents\n"
            .write(to: bad.appendingPathComponent("LOG"), atomically: true, encoding: .utf8)
        try "Compacted to: files[ 0 1 0 ]\n".write(to: good.appendingPathComponent("LOG"), atomically: true, encoding: .utf8)
        try "Corruption: old recovered error\n".write(to: good.appendingPathComponent("LOG.old"), atomically: true, encoding: .utf8)
        try FileManager.default.createSymbolicLink(at: root.appendingPathComponent("linked.userdb"), withDestinationURL: bad)
        guard UserDictionaryHealth.failedDictionaries(in: root) == ["bad.userdb"] else { return fail("storage error reporting and old/symlink exclusion") }
        try FileManager.default.removeItem(at: bad.appendingPathComponent("LOG"))
        try FileManager.default.createSymbolicLink(at: bad.appendingPathComponent("LOG"), withDestinationURL: good.appendingPathComponent("LOG.old"))
        guard UserDictionaryHealth.failedDictionaries(in: root).isEmpty else { return fail("log symlink exclusion") }
    } catch { return fail("fixture \(error)") }
    print("input-repair-smoke: protected input, Shift routing, and user dictionary diagnostics passed")
    return true
}

func runShiftPunctuationEngineSmokeTest() -> Bool {
    let engine = RimeEngine()
    guard engine.start() else { print("shift-punctuation-smoke: engine unavailable"); return false }
    let session = engine.createSession()
    guard session != 0 else { return false }
    defer { engine.destroySession(session) }
    guard engine.selectSchema("double_pinyin_flypy", session: session) else { return false }
    let examples: [(String, UInt16, String)] = [("?",44,"？"),("^",22,"……"),("!",18,"！"),(":",41,"：")]
    for asciiMode in [false, true] {
        for asciiPunct in [false, true] {
            engine.setOption("ascii_mode", asciiMode, session: session)
            engine.setOption("ascii_punct", asciiPunct, session: session)
            engine.setOption("full_shape", false, session: session)
            for (text, key, chinese) in examples {
                let event = NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: .shift,
                    timestamp: 0, windowNumber: 0, context: nil, characters: text,
                    charactersIgnoringModifiers: text, isARepeat: false, keyCode: key)!
                guard let code = MacShiftKeyRouting.symbolKeysym(for: event) else { return false }
                let handled = engine.processKey(code, mask: RimeKey.shiftMask, session: session)
                let committed = engine.takeCommit(session: session)
                // A declined key is passed through by the normal direct-input
                // fallback. It must never be transformed to the physical digit.
                let actual = committed ?? (handled ? "" : text)
                let expected = asciiMode || asciiPunct ? text : chinese
                guard actual == expected else {
                    print("shift-punctuation-smoke: FAIL \(text), ascii=\(asciiMode), punct=\(asciiPunct), actual=\(actual)")
                    return false
                }
            }
        }
    }
    print("shift-punctuation-smoke: 16 real-engine option/symbol combinations passed")
    return true
}
