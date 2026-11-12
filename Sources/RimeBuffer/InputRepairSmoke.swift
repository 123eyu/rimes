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
    let backgroundHolder = SecureInputHolder(
        processIdentifier: 501, bundleID: "app.browser", name: "Browser"
    )
    let unnamedHolder = SecureInputHolder(
        processIdentifier: 501, bundleID: "app.browser", name: nil
    )
    let anonymousHolder = SecureInputHolder(
        processIdentifier: 501, bundleID: nil, name: nil
    )
    guard SecureInputNoticeRules.holderIsInBackground(
            holder: backgroundHolder, frontmostProcessIdentifier: 777,
            ownProcessIdentifier: 42),
          !SecureInputNoticeRules.holderIsInBackground(
            holder: backgroundHolder, frontmostProcessIdentifier: 501,
            ownProcessIdentifier: 42),
          !SecureInputNoticeRules.holderIsInBackground(
            holder: backgroundHolder, frontmostProcessIdentifier: 777,
            ownProcessIdentifier: 501),
          !SecureInputNoticeRules.holderIsInBackground(
            holder: backgroundHolder, frontmostProcessIdentifier: nil,
            ownProcessIdentifier: 42),
          !SecureInputNoticeRules.holderIsInBackground(
            holder: nil, frontmostProcessIdentifier: 777,
            ownProcessIdentifier: 42),
          SecureInputNoticeRules.detail(
            holder: backgroundHolder, frontmostProcessIdentifier: 777,
            ownProcessIdentifier: 42).contains("Browser"),
          SecureInputNoticeRules.detail(
            holder: unnamedHolder, frontmostProcessIdentifier: 777,
            ownProcessIdentifier: 42).contains("app.browser"),
          SecureInputNoticeRules.detail(
            holder: anonymousHolder, frontmostProcessIdentifier: 777,
            ownProcessIdentifier: 42).contains("501"),
          SecureInputNoticeRules.detail(
            holder: backgroundHolder, frontmostProcessIdentifier: 501,
            ownProcessIdentifier: 42) == SecureInputNoticeRules.foregroundDetail,
          SecureInputNoticeRules.detail(
            holder: nil, frontmostProcessIdentifier: 777,
            ownProcessIdentifier: 42) == SecureInputNoticeRules.unknownHolderDetail
    else { return fail("secure input notice wording") }

    // Secure input held by another background Dock app no longer pauses
    // composition, but only on positive identification of that holder.
    func heldBy(_ pid: pid_t, _ bundle: String?, regular: Bool = true) -> SecureInputHolder {
        SecureInputHolder(processIdentifier: pid, bundleID: bundle,
                          name: "Holder", isRegularApplication: regular)
    }
    func elsewhere(_ holder: SecureInputHolder?, front: pid_t? = 777,
                   frontBundle: String? = "app.front") -> Bool {
        SecureInputScopeRules.holderIsElsewhere(
            holder: holder, frontmostProcessIdentifier: front,
            frontmostBundleID: frontBundle, ownProcessIdentifier: 42,
            ownBundleID: "own.ime")
    }
    func protects(_ holder: SecureInputHolder?, client: String? = "app.front",
                  secure: Bool = true) -> Bool {
        SecureInputScopeRules.protectsField(
            secureInput: secure, holder: holder, frontmostProcessIdentifier: 777,
            frontmostBundleID: "app.front", clientBundleID: client,
            ownProcessIdentifier: 42, ownBundleID: "own.ime")
    }
    let terminal = heldBy(501, "app.terminal")
    guard elsewhere(terminal),
          !protects(terminal),
          !protects(terminal, client: "app.panel"),
          !protects(nil, secure: false),
          // Every doubt keeps the pause.
          protects(nil),
          protects(terminal, client: nil),
          protects(terminal, client: "app.terminal"),
          protects(terminal, client: ProtectedInput.loginWindowBundleID),
          !elsewhere(heldBy(501, "app.helper", regular: false)),
          !elsewhere(heldBy(501, nil)),
          !elsewhere(heldBy(777, "app.front")),
          !elsewhere(heldBy(502, "app.front")),
          !elsewhere(heldBy(42, "own.ime")),
          !elsewhere(heldBy(503, "own.ime")),
          !elsewhere(heldBy(501, ProtectedInput.loginWindowBundleID)),
          !elsewhere(heldBy(0, "app.terminal")),
          !elsewhere(terminal, front: nil),
          !elsewhere(terminal, frontBundle: nil),
          !elsewhere(terminal, frontBundle: ProtectedInput.loginWindowBundleID),
          // The loginwindow boundary is independent of the holder.
          !ProtectedInput.blocksIME(secureInput: true, clientBundleID: "app.front",
              frontmostBundleID: "app.front", secureInputHeldElsewhere: true),
          ProtectedInput.blocksIME(secureInput: true,
              clientBundleID: ProtectedInput.loginWindowBundleID,
              frontmostBundleID: "app.front", secureInputHeldElsewhere: true),
          ProtectedInput.blocksIME(secureInput: false, clientBundleID: "app.front",
              frontmostBundleID: ProtectedInput.loginWindowBundleID,
              secureInputHeldElsewhere: true),
          // The menu says what stays paused instead of "Chinese paused".
          SecureInputNoticeRules.lines(holder: terminal, frontmostProcessIdentifier: 777,
              holderIsElsewhere: true, ownProcessIdentifier: 42).first
              == SecureInputNoticeRules.backgroundHeadline,
          SecureInputNoticeRules.lines(holder: terminal, frontmostProcessIdentifier: 777,
              holderIsElsewhere: true, ownProcessIdentifier: 42).last?
              .contains("中文输入照常") == true,
          SecureInputNoticeRules.lines(holder: terminal, frontmostProcessIdentifier: 777,
              holderIsElsewhere: false, ownProcessIdentifier: 42)
              == [SecureInputNoticeRules.headline,
                  SecureInputNoticeRules.detail(holder: terminal,
                      frontmostProcessIdentifier: 777, ownProcessIdentifier: 42)]
    else { return fail("secure input held elsewhere") }
    let symbols: [(String, UInt16)] = [("?",44),("^",22),("!",18),(":",41),("(",25),(")",29),("<",43),(">",47)]
    for (text, key) in symbols {
        guard MacShiftKeyRouting.directText(for: event(text, key: key)) == nil,
              MacShiftKeyRouting.symbolKeysym(for: event(text, key: key)) == Int32(text.utf8.first!)
        else { return fail("Shift symbol \(text)") }
    }
    guard MacShiftKeyRouting.directText(for: event("A", key: 0)) == "A",
          MacShiftKeyRouting.directText(for: event("§", key: 20)) == "§",
          MacShiftKeyRouting.directText(for: event("É", key: 14)) == "É",
          MacShiftKeyRouting.directText(for: event("\r", key: 36)) == nil,
          MacShiftKeyRouting.directText(for: event("\u{f700}", key: 126)) == nil,
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
