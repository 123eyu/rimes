import Foundation
import CryptoKit

/// Public observations from the actual native scoring model, never user input.
func runTypingPracticeLinuxFixtures(_ output: URL) -> Bool {
    do {
        func encoded<T: Encodable>(_ value: T) throws -> [String: Any] {
            let data = try JSONEncoder().encode(value)
            return try JSONSerialization.jsonObject(with: data) as! [String: Any]
        }
        func articleValue(_ article: TypingTestArticle) -> [String: Any] {
            ["id": article.id, "version": article.version, "language": article.language.rawValue,
             "title": article.title, "theme": article.theme, "difficulty": article.difficulty,
             "text": article.text, "characterCount": article.characterCount, "wordCount": article.wordCount]
        }
        func observation(_ session: TypingTestSession, at time: Double) throws -> [String: Any] {
            let value = session.snapshot(at: time)
            let metrics = value.metrics
            return ["metrics": try encoded(metrics), "committedText": value.committedText,
                    "targetStates": value.targetStates.map { state in
                        switch state { case .pending: return "pending"; case .correct: return "correct"
                        case .incorrect: return "incorrect"; case .omitted: return "omitted" }
                    }, "typedCorrectness": value.typedCorrectness,
                    "canComplete": value.canComplete, "isStarted": value.isStarted,
                    "isFinished": value.isFinished, "isCancelled": value.isCancelled,
                    "rates": ["effectiveCPM": metrics.effectiveCPM, "rawCPM": metrics.rawCPM,
                              "wordsPerMinute": metrics.wordsPerMinute, "keysPerSecond": metrics.keysPerSecond,
                              "chordsPerSecond": metrics.chordsPerSecond as Any? ?? NSNull(),
                              "processAccuracy": metrics.processAccuracy as Any? ?? NSNull(),
                              "finalAccuracy": metrics.finalAccuracy as Any? ?? NSNull(),
                              "progress": metrics.progress, "codeLength": metrics.codeLength as Any? ?? NSNull()]]
        }
        let context = TypingTestContext(schemaID: "public.schema", keymapID: "public.keymap", keymapVersion: 1,
                                        chordCountingAvailable: true, inputSourceID: "public.source", keymapRevision: "snapshot-1")
        var cases: [[String: Any]] = []
        func run(_ name: String, _ text: String, _ events: [[String: Any]],
                 language: TypingTestLanguage = .chinese, chords: Bool = true) throws {
            let article = TypingTestArticle(id: "public." + name, version: 1, language: language,
                                           title: name, theme: "public fixture", difficulty: "fixture", text: text)
            let cfg = chords ? context : TypingTestContext(schemaID: "public.english", mode: .practice)
            let session = TypingTestSession(article: article, context: cfg)
            var steps: [[String: Any]] = []
            for event in events {
                let time = event["at"] as? Double ?? 0
                var step: [String: Any] = ["event": event]
                switch event["op"] as! String {
                case "start": session.start(at: time)
                case "key": session.recordKey(at: time, isRepeat: event["repeat"] as? Bool ?? false,
                                               isBackspace: event["backspace"] as? Bool ?? false,
                                               isComposing: event["composing"] as? Bool ?? false)
                case "chord": session.recordChord(at: time)
                case "upgrade": session.reclassifyBackspaceAsComposition()
                case "commit":
                    let range = (event["range"] as? [Int]).map { $0[0]..<$0[1] }
                    step["accepted"] = session.reconcileCommittedText(event["text"] as! String, at: time, explicitInsertionRange: range)
                case "reason": session.markPractice(reason: TypingTestPracticeReason(rawValue: event["reason"] as! String)!)
                case "cancel": session.cancel()
                case "finish":
                    if let result = session.finish(at: time, completedAt: Date(timeIntervalSince1970: 1_700_000_000)) {
                        var json = try encoded(result)
                        json.removeValue(forKey: "id")
                        json["isComparable"] = result.isComparable
                        json["displaySpeed"] = result.displaySpeed
                        step["result"] = json
                    } else { step["result"] = NSNull() }
                default: break
                }
                step["snapshot"] = try observation(session, at: time)
                steps.append(step)
            }
            cases.append(["name": name, "article": articleValue(article), "context": try encoded(cfg), "steps": steps])
        }
        func event(_ op: String, _ time: Double, _ extra: [String: Any] = [:]) -> [String: Any] {
            var value = extra; value["op"] = op; value["at"] = time; return value
        }
        try run("edits", "abcdef", [event("snapshot", 90), event("key", 100), event("key", 101),
            event("key", 102, ["backspace": true, "composing": true]), event("key", 103, ["backspace": true]),
            event("key", 104, ["repeat": true]), event("chord", 105), event("commit", 106, ["text": "abq"]),
            event("commit", 107, ["text": "ab"]), event("commit", 108, ["text": "abdef"]),
            event("snapshot", 120), event("key", 119), event("finish", 160), event("finish", 999),
            event("commit", 1000, ["text": "abcdef"])])
        try run("corrected", "abc", [event("start", 0), event("commit", 1, ["text": "axc"]),
            event("commit", 2, ["text": "abc"]), event("finish", 3)])
        try run("identical", "abc", [event("commit", 0, ["text": "abc"]),
            event("commit", 1, ["text": "abc", "range": [1, 2]]),
            event("commit", 2, ["text": "xyz", "range": [1, 2]]), event("finish", 3)])
        try run("unicode", "你👨‍👩‍👧‍👦e\u{301}\n好", [event("start", 0),
            event("commit", 1, ["text": "你👨‍👩‍👧‍👦é\r\n好"]), event("finish", 2)])
        try run("combining", "é", [event("commit", 0, ["text": "e", "range": [0, 1]]),
            event("commit", 1, ["text": "e\u{301}", "range": [0, 1]]), event("finish", 2)])
        try run("partial", "abcde", [event("start", 0), event("commit", 1, ["text": "ab"]), event("finish", 3)])
        try run("extra", "abc", [event("start", 0), event("commit", 1, ["text": "axbc"]), event("finish", 2)])
        try run("wrong", "abc", [event("commit", 1, ["text": "xxx"]), event("finish", 2)])
        try run("english", "abcdefghijklmnopqrstuvwxy", [event("chord", 0), event("start", 0),
            event("commit", 30, ["text": "abcdefghijklmnopqrstuvwxy"]), event("reason", 40, ["reason": "focusLost"]),
            event("reason", 41, ["reason": "focusLost"]), event("finish", 60)], language: .english, chords: false)
        try run("cancelled", "a", [event("commit", 1, ["text": "a"]), event("cancel", 2), event("finish", 3)])
        try run("zero", "a", [event("commit", 10, ["text": "a"]), event("finish", 10), event("finish", 11)])
        try run("backspace", "ab", [event("key", 0, ["backspace": true]), event("upgrade", 1),
            event("key", 2, ["repeat": true, "backspace": true, "composing": true]),
            event("commit", 3, ["text": "ab"]), event("finish", 4), event("upgrade", 5)])
        try run("bounded", "a", [event("commit", 0, ["text": "a"])]
            + (1...501).map { event("snapshot", Double($0)) } + [event("finish", 90_000)])
        try run("limits", "abc", [event("commit", 1, ["text": String(repeating: "字", count: 2049)]),
            event("commit", 2, ["text": "\0"]), event("finish", 3)])
        // Repeated symbols stress the native sequence-diff tie-breaking rules.
        var seed: UInt64 = 0x73534d45
        for index in 0..<32 {
            func number(_ n: Int) -> Int { seed = seed &* 6364136223846793005 &+ 1; return Int((seed >> 32) % UInt64(n)) }
            var text = Array("abbaaccb")
            var events = [event("start", 0), event("commit", 1, ["text": String(text)])]
            for step in 2...10 {
                if number(2) == 0, !text.isEmpty { text.remove(at: number(text.count)) }
                else { text.insert(Array("abc")[number(3)], at: number(text.count + 1)) }
                events.append(event("commit", Double(step), ["text": String(text)]))
            }
            events.append(event("finish", 12))
            try run("diff-\(index)", "abcabcabc", events)
        }
        var validations: [[String: Any]] = []
        func validate(_ name: String, _ value: [String: Any]) throws {
            let bytes = try JSONSerialization.data(withJSONObject: value, options: [.sortedKeys])
            let decoded = try? JSONDecoder().decode(TypingTestResult.self, from: bytes)
            validations.append(["name": name, "value": value, "valid": decoded.map(TypingTestHistoryStore.valid) ?? false])
        }
        var base = ((cases[0]["steps"] as! [[String: Any]]).first { $0["result"] is [String: Any] }!["result"] as! [String: Any])
        base.removeValue(forKey: "isComparable"); base.removeValue(forKey: "displaySpeed")
        base["id"] = "00000000-0000-4000-8000-000000000001"
        try validate("valid", base)
        for (field, value) in [("id", "bad-uuid" as Any), ("articleID", "" as Any),
                               ("articleID", "bad\nidentity" as Any), ("articleVersion", 0 as Any),
                               ("completedAt", -1 as Any), ("language", "other" as Any),
                               ("practiceReasons", ["focusLost", "focusLost"] as Any),
                               ("practiceReasons", ["unknown"] as Any)] {
            var changed = base; changed[field] = value; try validate(field, changed)
        }
        let baseMetrics = base["metrics"] as! [String: Any]
        for field in baseMetrics.keys.sorted() where field.hasSuffix("Count") {
            for value in [-1, 1_000_001] {
                var metrics = baseMetrics; metrics[field] = value
                var changed = base; changed["metrics"] = metrics; try validate(field + "-\(value)", changed)
            }
        }
        for (field, value) in [("elapsedSeconds", 0 as Any), ("elapsedSeconds", 86_401 as Any),
                               ("speedSamples", [] as Any), ("chordCount", NSNull() as Any)] {
            var metrics = baseMetrics; metrics[field] = value
            var changed = base; changed["metrics"] = metrics; try validate(field, changed)
        }
        var unknown = base; unknown["text"] = "must not be persisted"; unknown["application"] = "must not be persisted"
        try validate("unknown-fields-ignored", unknown)
        let root = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
        let names = ["OfficialPlugins/native/macos/TypingTestArticles.swift", "OfficialPlugins/native/macos/TypingTestSession.swift",
                     "OfficialPlugins/native/macos/TypingTestHistoryStore.swift", "OfficialPlugins/native/macos/TypingTestTextView.swift",
                     "OfficialPlugins/native/macos/TypingSpeedSettingsViewController.swift", "Sources/RimeBuffer/TypingPracticeTelemetry.swift",
                     "Sources/RimeBuffer/TypingTestUISmoke.swift", "Sources/RimeBuffer/TypingPracticeLinuxFixtures.swift",
                     "Sources/RimeBuffer/TypingPracticeLinuxUISmoke.swift", "Sources/RimeBuffer/MetricsChartViews.swift", "Sources/RimeBuffer/RimeUI.swift"]
        var sources: [String: String] = [:]
        for name in names { sources[name] = SHA256.hash(data: try Data(contentsOf: root.appendingPathComponent(name))).map { String(format: "%02x", $0) }.joined() }
        let value: [String: Any] = ["purpose": "public-typing-practice-contract", "schemaVersion": 1,
                                   "sources": sources, "articles": TypingTestArticles.all.map(articleValue), "cases": cases,
                                   "validation": validations]
        try JSONSerialization.data(withJSONObject: value, options: [.prettyPrinted, .sortedKeys, .fragmentsAllowed]).write(to: output, options: .atomic)
        print("Actual native typing practice: \(cases.count) cases, \(cases.reduce(0) { $0 + ($1["steps"] as! [Any]).count }) observations")
        return true
    } catch { print("Typing practice export failed: \(error)"); return false }
}
