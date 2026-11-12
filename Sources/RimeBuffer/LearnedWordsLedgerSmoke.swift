import AppKit

private final class LearnedWordsFakeEngine: UserLexiconEngine {
    var isHealthy = true
    var dictionaries: Set<String> = ["rime_ice"]
    var rows: [String: String] = [:]
    private(set) var exportCalls = 0
    private(set) var imports = 0

    func hasUserDictionary(named name: String) -> Bool { dictionaries.contains(name) }

    func exportUserDictionary(named name: String, to fileURL: URL) -> Int {
        exportCalls += 1
        let text = rows[name] ?? ""
        do { try Data(text.utf8).write(to: fileURL) } catch { return -1 }
        return text.split(separator: "\n").filter { !$0.hasPrefix("#") }.count
    }

    func importUserDictionary(named name: String, from fileURL: URL) -> Int {
        imports += 1
        return -1
    }

    func restoreUserDictionarySnapshot(from fileURL: URL) -> Bool {
        imports += 1
        return false
    }
}

@discardableResult
func runLearnedWordsLedgerSmokeTest() -> Bool {
    let fm = FileManager.default
    let base = fm.temporaryDirectory
        .appendingPathComponent("rimes-learned-words-smoke-\(UUID().uuidString)", isDirectory: true)
    defer { try? fm.removeItem(at: base) }
    let suite = "RIMES.LearnedWordsSmoke.\(UUID().uuidString)"
    guard let defaults = UserDefaults(suiteName: suite) else { return false }
    defer { defaults.removePersistentDomain(forName: suite) }

    var failures: [String] = []
    func check(_ condition: Bool, _ message: String) {
        if !condition { failures.append(message) }
    }

    var calendar = Calendar(identifier: .gregorian)
    calendar.timeZone = TimeZone(identifier: "UTC")!
    let day1 = Date(timeIntervalSince1970: 1_794_355_200)        // 2026-11-11 00:00 UTC
    let day1Later = day1.addingTimeInterval(6 * 3_600)
    let day2 = day1.addingTimeInterval(26 * 3_600)                // 2026-11-12

    let engine = LearnedWordsFakeEngine()
    let preferences = LearnedWordsReviewPreferences(defaults: defaults)
    let root = base.appendingPathComponent("RIMES/lexicon-review", isDirectory: true)
    let ledger = LearnedWordsLedger(engine: engine, root: root, preferences: preferences,
                                    calendar: calendar)

    // Parsing: comments, deleted (count <= 0), malformed and duplicate rows.
    let parsed = LearnedWordsLedger.parseExport("""
    # Rime user dictionary export
    #@/db_name\trime_ice
    你好\tni hao\t12
    你好\tni hao\t3
    删掉\tshan diao\t-4
    零次\tling ci\t0
    坏行\tbad
    \tempty\t2
    英文\tying wen \t1
    """)
    check(parsed.count == 2, "parse kept \(parsed.count) rows, expected 2")
    check(parsed[LearnedWordKey(word: "你好", code: "ni hao")] == 12, "duplicate rows must keep the highest count")
    check(parsed[LearnedWordKey(word: "英文", code: "ying wen")] == 1, "codes must be trimmed")

    // Disabled: nothing is exported or written.
    engine.rows["rime_ice"] = "你好\tni hao\t5\n世界\tshi jie\t1\n"
    ledger.captureIfDue(now: day1)
    check(engine.exportCalls == 0, "capture ran while recording was off")
    check(!fm.fileExists(atPath: root.path), "ledger wrote files while recording was off")

    // First capture becomes the undated baseline.
    preferences.setRecordsDaily(true)
    ledger.captureIfDue(now: day1)
    check(engine.exportCalls == 1, "first due capture did not export")
    check(ledger.lastCapturedDay(for: .chinese) == "2026-11-11", "baseline day not recorded")
    check(ledger.days(for: .chinese).isEmpty, "baseline must not be reported as learned today")
    check(ledger.currentEntries(for: .chinese).count == 2, "baseline state should hold 2 words")

    // Same day: captureIfDue skips; an explicit capture records the change.
    ledger.captureIfDue(now: day1Later)
    check(engine.exportCalls == 1, "captureIfDue ran twice on one day")
    engine.rows["rime_ice"] = "你好\tni hao\t7\n世界\tshi jie\t1\n输入法\tshu ru fa\t1\n"
    var result = try? ledger.capture(.chinese, now: day1Later)
    check(result == .recorded(day: "2026-11-11", added: 1, reused: 1, entryCount: 3),
          "same-day capture result \(String(describing: result))")

    engine.rows["rime_ice"] = "你好\tni hao\t9\n世界\tshi jie\t1\n输入法\tshu ru fa\t2\n"
    result = try? ledger.capture(.chinese, now: day1Later)
    check(result == .recorded(day: "2026-11-11", added: 0, reused: 0, entryCount: 3),
          "repeat capture must merge, got \(String(describing: result))")
    let today = ledger.days(for: .chinese).first
    check(today?.added.map(\.word) == ["输入法"], "added list \(String(describing: today?.added))")
    check(today?.added.first?.count == 2, "added word count should follow the latest export")
    check(today?.reused.count == 1 && today?.reused.first?.fromCount == 5
            && today?.reused.first?.toCount == 9,
          "reuse must keep the first count and update the last: \(String(describing: today?.reused))")

    // Next day: a deleted word disappears; a re-learned word counts as new.
    engine.rows["rime_ice"] = "你好\tni hao\t9\n世界\tshi jie\t-1\n输入法\tshu ru fa\t2\n回顾\thui gu\t1\n"
    ledger.captureIfDue(now: day2)
    let days = ledger.days(for: .chinese)
    check(days.map(\.day) == ["2026-11-12", "2026-11-11"], "days newest first: \(days.map(\.day))")
    check(days.first?.added.map(\.word) == ["回顾"] && days.first?.reused.isEmpty == true,
          "day 2 record \(String(describing: days.first))")
    check(!ledger.currentEntries(for: .chinese).contains { $0.word == "世界" },
          "deleted word must leave the current state")
    engine.rows["rime_ice"] = "你好\tni hao\t9\n世界\tshi jie\t1\n输入法\tshu ru fa\t2\n回顾\thui gu\t1\n"
    _ = try? ledger.capture(.chinese, now: day2)
    check(ledger.days(for: .chinese).first?.added.contains { $0.word == "世界" } == true,
          "a word learned again after deletion should count as new")

    // The ledger never writes to the Rime user dictionary.
    check(engine.imports == 0, "ledger called an import or restore")

    // Files are private.
    let statePath = root.appendingPathComponent("rime_ice/state.tsv").path
    let permissions = (try? fm.attributesOfItem(atPath: statePath)[.posixPermissions] as? NSNumber)?.intValue
    check(permissions == 0o600, "state file permissions \(String(describing: permissions))")

    // A symlinked ledger directory is refused.
    let linkedBase = base.appendingPathComponent("linked/RIMES", isDirectory: true)
    let elsewhere = base.appendingPathComponent("elsewhere", isDirectory: true)
    try? fm.createDirectory(at: linkedBase, withIntermediateDirectories: true)
    try? fm.createDirectory(at: elsewhere, withIntermediateDirectories: true)
    let linkedRoot = linkedBase.appendingPathComponent("lexicon-review")
    try? fm.createSymbolicLink(at: linkedRoot, withDestinationURL: elsewhere)
    let linked = LearnedWordsLedger(engine: engine, root: linkedRoot, preferences: preferences,
                                    calendar: calendar)
    do {
        _ = try linked.capture(.chinese, now: day1)
        failures.append("capture followed a symlinked ledger directory")
    } catch let error as LearnedWordsLedgerError {
        if case .unsafePath = error {} else { failures.append("unexpected symlink error \(error)") }
    } catch {
        failures.append("unexpected symlink error \(error)")
    }
    check((try? fm.contentsOfDirectory(atPath: elsewhere.path))?.isEmpty == true,
          "nothing may be written through the symlink")

    // Unhealthy engine fails cleanly.
    engine.isHealthy = false
    do {
        _ = try ledger.capture(.chinese, now: day2)
        failures.append("capture ran with an unhealthy engine")
    } catch {}
    engine.isHealthy = true

    // Review window: sections, rows and filters, without touching the shared ledger.
    _ = NSApplication.shared
    let review = LearnedWordsReviewWindowController(ledger: ledger, preferences: preferences)
    review.show(kind: .chinese)
    check(review.window != nil, "review window was not built")
    check(review.sections.count == 3, "sections \(review.sections.map(\.title))")
    check(review.sections.last?.section == .all, "last section should be all words")
    review.showSection(at: 1)
    check(review.rows.first?.word == "输入法" && review.rows.first?.isNew == true,
          "day rows should list new words first: \(review.rows.map(\.word))")
    check(review.rows.contains { $0.word == "你好" && $0.change == "5 → 9" }, "reuse row missing")
    review.showSection(at: 2)
    check(review.rows.count == 4, "all words rows \(review.rows.count)")
    check(review.rows.first?.count == 1, "all words should list least-used first")
    review.setFilter(.usedOnce)
    check(review.rows.allSatisfy { $0.count == 1 } && review.rows.count == 2,
          "used-once filter rows \(review.rows.map(\.word))")
    review.setFilter(.longPhrases)
    check(review.rows.isEmpty, "no test word has 5+ characters")
    review.setFilter(.everything)
    review.window?.close()
    check(LearnedWordsReviewWindowController.dayTitle("2026-11-12", today: "2026-11-12") == "今天",
          "today title")
    check(LearnedWordsReviewWindowController.dayTitle("2026-11-09", today: "2026-11-12")
            == "2026 年 11 月 9 日", "date title")

    if failures.isEmpty {
        print("learned-words-ledger-smoke: PASS baseline, same-day merge, next day, deletion, privacy, symlink, review window")
        return true
    }
    failures.forEach { print("learned-words-ledger-smoke: FAIL \($0)") }
    return false
}

/// Runs the ledger against real librime in an isolated user directory. Put a
/// copy of a `rime_ice.userdb` there first to exercise real learned rows.
@discardableResult
func runLearnedWordsLedgerEngineSmokeTest() -> Bool {
    let environment = ProcessInfo.processInfo.environment
    guard let isolatedPath = environment["RIMEBUFFER_USER_DIR"], !isolatedPath.isEmpty else {
        print("learned-words-ledger-engine-smoke: REFUSED (set isolated RIMEBUFFER_USER_DIR)")
        return false
    }
    let userDirectory = URL(fileURLWithPath: isolatedPath, isDirectory: true)
    guard rimeEngine.start(), rimeEngine.isHealthy else {
        print("learned-words-ledger-engine-smoke: engine start failed: \(rimeEngine.lastError())")
        return false
    }
    let suite = "RIMES.LearnedWordsEngineSmoke.\(UUID().uuidString)"
    guard let defaults = UserDefaults(suiteName: suite) else { return false }
    defer { defaults.removePersistentDomain(forName: suite) }
    let preferences = LearnedWordsReviewPreferences(defaults: defaults)
    preferences.setRecordsDaily(true)
    let ledger = LearnedWordsLedger(engine: rimeEngine,
                                    root: userDirectory.appendingPathComponent("lexicon-review"),
                                    preferences: preferences)
    guard rimeEngine.hasUserDictionary(named: UserLexiconKind.chinese.dictionaryName) else {
        print("learned-words-ledger-engine-smoke: no rime_ice user dictionary in the isolated directory")
        return false
    }
    do {
        let baseline = try ledger.capture(.chinese)
        guard case .baseline(let count) = baseline, count > 0 else {
            print("learned-words-ledger-engine-smoke: FAIL first capture \(baseline)")
            return false
        }
        let again = try ledger.capture(.chinese)
        guard case .recorded(_, 0, 0, count) = again, ledger.days(for: .chinese).isEmpty else {
            print("learned-words-ledger-engine-smoke: FAIL unchanged capture \(again)")
            return false
        }
        // Optional visual check: `learned-words-ledger-engine-smoke out.png`.
        if let index = CommandLine.arguments.firstIndex(of: "learned-words-ledger-engine-smoke"),
           CommandLine.arguments.indices.contains(index + 1) {
            let output = URL(fileURLWithPath: CommandLine.arguments[index + 1])
            _ = NSApplication.shared
            let review = LearnedWordsReviewWindowController(ledger: ledger, preferences: preferences)
            review.show(kind: .chinese)
            review.setFilter(.usedOnce)
            review.window?.layoutIfNeeded()
            review.window?.display()
            guard let view = review.window?.contentView,
                  let bitmap = view.bitmapImageRepForCachingDisplay(in: view.bounds) else {
                print("learned-words-ledger-engine-smoke: FAIL could not render the review window")
                return false
            }
            view.cacheDisplay(in: view.bounds, to: bitmap)
            try bitmap.representation(using: .png, properties: [:])?.write(to: output)
        }
        print("learned-words-ledger-engine-smoke: PASS baseline \(count) words, unchanged recapture records nothing")
        return true
    } catch {
        print("learned-words-ledger-engine-smoke: FAIL \(error.localizedDescription)")
        return false
    }
}
