import AppKit

@discardableResult
func runLearnedWordsDeletionSmokeTest() -> Bool {
    let fm = FileManager.default
    let base = fm.temporaryDirectory
        .appendingPathComponent("rimes-learned-words-deletion-\(UUID().uuidString)", isDirectory: true)
    defer { try? fm.removeItem(at: base) }
    let suite = "RIMES.LearnedWordsDeletionSmoke.\(UUID().uuidString)"
    guard let defaults = UserDefaults(suiteName: suite) else { return false }
    defer { defaults.removePersistentDomain(forName: suite) }

    var failures: [String] = []
    func check(_ condition: Bool, _ message: String) {
        if !condition { failures.append(message) }
    }
    func key(_ word: String, _ code: String) -> LearnedWordKey { LearnedWordKey(word: word, code: code) }
    func exported(_ engine: LearnedWordsFakeEngine) -> [LearnedWordKey: Int] {
        LearnedWordsLedger.parseExport(engine.rows["rime_ice"] ?? "")
    }

    var calendar = Calendar(identifier: .gregorian)
    calendar.timeZone = TimeZone(identifier: "UTC")!
    let day1 = Date(timeIntervalSince1970: 1_794_355_200)        // 2026-11-11 00:00 UTC
    let day2 = day1.addingTimeInterval(26 * 3_600)
    let day3 = day2.addingTimeInterval(24 * 3_600)

    let preferences = LearnedWordsReviewPreferences(defaults: defaults)
    preferences.setRecordsDaily(true)
    let engine = LearnedWordsFakeEngine()
    engine.rows["rime_ice"] = "你好\tni hao\t9\n世界\tshi jie\t1\n输入法\tshu ru fa\t2\n回顾\thui gu\t1\n错词\tcuo ci\t3\n"
    let root = base.appendingPathComponent("RIMES/lexicon-review", isDirectory: true)
    let ledger = LearnedWordsLedger(engine: engine, root: root, preferences: preferences,
                                    calendar: calendar)
    _ = try? ledger.capture(.chinese, now: day1)

    // A word learned after the last capture must still be reported later.
    engine.type("新词", code: "xin ci", in: "rime_ice")

    // Delete: backup, tombstones, round, state.
    let stateURL = root.appendingPathComponent("rime_ice/state.tsv")
    let headerBefore = (try? String(contentsOf: stateURL, encoding: .utf8))?
        .split(separator: "\n").prefix(3).joined(separator: "\n")
    let first = try? ledger.delete([key("错词", "cuo ci"), key("世界", "shi jie"), key("不存在", "bu cun zai")],
                                   from: .chinese, now: day1.addingTimeInterval(3_600))
    check(first?.round.words.map(\.word).sorted() == ["世界", "错词"],
          "deleted words \(String(describing: first?.round.words))")
    check(first?.round.words.first(where: { $0.word == "错词" })?.count == 3,
          "round must keep the original count")
    check(first?.notFound == 1 && first?.notDeleted == 0, "result counts \(String(describing: first))")
    check(exported(engine)[key("错词", "cuo ci")] == nil && exported(engine)[key("世界", "shi jie")] == nil,
          "deleted words still exported")
    check(engine.rows["rime_ice"]?.contains("错词\tcuo ci\t-3") == true,
          "tombstone should keep the old count's size")
    let backupName = first?.round.backupFile ?? ""
    let backup = root.appendingPathComponent("rime_ice/backups/\(backupName)")
    check((try? String(contentsOf: backup, encoding: .utf8))?.contains("错词\tcuo ci\t3") == true,
          "backup missing or incomplete")
    let backupMode = (try? fm.attributesOfItem(atPath: backup.path)[.posixPermissions] as? NSNumber)?.intValue
    check(backupMode == 0o600, "backup permissions \(String(describing: backupMode))")
    check(ledger.deletionRounds(for: .chinese).count == 1, "one round expected")
    check(ledger.currentlyDeleted(for: .chinese) == [key("错词", "cuo ci"), key("世界", "shi jie")],
          "currently deleted \(ledger.currentlyDeleted(for: .chinese))")
    let entriesAfterDelete = Set(ledger.currentEntries(for: .chinese).map(\.word))
    check(!entriesAfterDelete.contains("错词") && !entriesAfterDelete.contains("世界"),
          "state still lists deleted words")
    let headerAfter = (try? String(contentsOf: stateURL, encoding: .utf8))?
        .split(separator: "\n").prefix(3).joined(separator: "\n")
    check(headerBefore == headerAfter, "deletion must keep the state's capture day and time")

    // The next daily capture still reports the word learned before the deletion.
    _ = try? ledger.capture(.chinese, now: day2)
    let day2Record = ledger.days(for: .chinese).first
    check(day2Record?.added.map(\.word) == ["新词"] && day2Record?.reused.isEmpty == true,
          "day after deletion \(String(describing: day2Record))")

    // Undo brings back the exact counts, once.
    if let round = first?.round {
        let undone = try? ledger.undo(roundID: round.id, for: .chinese, now: day2)
        check(undone?.isUndone == true, "undo did not mark the round")
        check(exported(engine)[key("错词", "cuo ci")] == 3 && exported(engine)[key("世界", "shi jie")] == 1,
              "undo counts \(exported(engine))")
        check(ledger.currentlyDeleted(for: .chinese).isEmpty, "nothing should be deleted after undo")
        check(ledger.currentEntries(for: .chinese).contains { $0.word == "错词" && $0.count == 3 },
              "undo must put the word back into the state")
        do {
            _ = try ledger.undo(roundID: round.id, for: .chinese)
            failures.append("a round was undone twice")
        } catch let error as LearnedWordsDeletionError {
            check(error == .alreadyUndone(round.id), "second undo error \(error)")
        } catch { failures.append("second undo error \(error)") }
        _ = try? ledger.capture(.chinese, now: day2.addingTimeInterval(60))
        check(ledger.days(for: .chinese).first?.added.map(\.word) == ["新词"],
              "undo must not be reported as newly learned")
    }

    // Typing a deleted word again revives it; undo then keeps the higher count.
    let second = try? ledger.delete([key("错词", "cuo ci")], from: .chinese, now: day2.addingTimeInterval(120))
    engine.type("错词", code: "cuo ci", in: "rime_ice")
    check(exported(engine)[key("错词", "cuo ci")] == 4, "revive should restore the old count plus one")
    // The window works from the last capture, so the revival shows after it.
    check(ledger.currentlyDeleted(for: .chinese) == [key("错词", "cuo ci")], "deleted until the next capture")
    _ = try? ledger.capture(.chinese, now: day3)
    check(ledger.currentlyDeleted(for: .chinese).isEmpty, "a revived word is not deleted any more")
    check(ledger.days(for: .chinese).first?.added.contains { $0.word == "错词" } == true,
          "a revived word counts as learned again")
    if let round = second?.round {
        _ = try? ledger.undo(roundID: round.id, for: .chinese, now: day3)
        check(exported(engine)[key("错词", "cuo ci")] == 4, "undo must not lower a revived count")
    }

    // Failures leave everything as it was.
    func expectError(_ expected: LearnedWordsDeletionError, _ body: () throws -> Void) {
        do { try body(); failures.append("expected \(expected)") } catch let error as LearnedWordsDeletionError {
            check(error == expected, "got \(error), expected \(expected)")
        } catch { failures.append("got \(error), expected \(expected)") }
    }
    expectError(.nothingToDelete) { _ = try ledger.delete([], from: .chinese) }
    expectError(.nothingToDelete) { _ = try ledger.delete([key("不存在", "bu cun zai")], from: .chinese) }
    let roundsBeforeFailure = ledger.deletionRounds(for: .chinese).count
    engine.rejectsImports = true
    do {
        _ = try ledger.delete([key("你好", "ni hao")], from: .chinese)
        failures.append("a rejected import still deleted")
    } catch {}
    engine.rejectsImports = false
    check(ledger.deletionRounds(for: .chinese).count == roundsBeforeFailure, "a failed delete saved a round")
    check(exported(engine)[key("你好", "ni hao")] == 9, "a failed delete changed the dictionary")

    // A partial import records only what really went.
    engine.ignoredOnImport = ["回顾"]
    let partial = try? ledger.delete([key("回顾", "hui gu"), key("输入法", "shu ru fa")], from: .chinese)
    engine.ignoredOnImport = []
    check(partial?.round.words.map(\.word) == ["输入法"] && partial?.notDeleted == 1,
          "partial result \(String(describing: partial))")

    // Only the newest backups are kept.
    var pruneEngineRows = ""
    for index in 0..<12 { pruneEngineRows += "词\(index)\tci \(index)\t1\n" }
    engine.rows["rime_ice"] = (engine.rows["rime_ice"] ?? "") + pruneEngineRows
    for index in 0..<12 {
        _ = try? ledger.delete([key("词\(index)", "ci \(index)")], from: .chinese)
    }
    let backups = (try? fm.contentsOfDirectory(atPath: root.appendingPathComponent("rime_ice/backups").path))?
        .filter { $0.hasSuffix(".tsv") } ?? []
    check(backups.count == LearnedWordsLedger.keptBackups, "backups kept: \(backups.count)")

    // Review window: select, delete, see the round, undo.
    let windowEngine = LearnedWordsFakeEngine()
    windowEngine.rows["rime_ice"] = "甲\tjia\t1\n乙\tyi\t1\n丙\tbing\t5\n"
    let windowLedger = LearnedWordsLedger(engine: windowEngine,
                                          root: base.appendingPathComponent("W/lexicon-review"),
                                          preferences: preferences, calendar: calendar)
    _ = try? windowLedger.capture(.chinese)
    _ = NSApplication.shared
    let review = LearnedWordsReviewWindowController(ledger: windowLedger, preferences: preferences)
    review.show(kind: .chinese)
    check(review.selectedSection == .all, "all words should be selected first")
    check(!review.canDeleteSelection, "nothing selected yet")
    review.selectWords(at: IndexSet([0, 1]))
    check(review.canDeleteSelection, "two words selected")
    review.requestDeletion(skipConfirmation: true)
    check(review.notice?.hasPrefix("已删除 2 个词") == true, "notice \(String(describing: review.notice))")
    check(review.selectedSection == .all, "deleting must keep the current list")
    check(review.rows.map(\.word) == ["丙"], "rows after delete \(review.rows.map(\.word))")
    guard let roundIndex = review.sections.firstIndex(where: {
        if case .deletion = $0.section { return true } else { return false }
    }) else {
        failures.append("no deletion round in the list")
        failures.forEach { print("learned-words-deletion-smoke: FAIL \($0)") }
        return false
    }
    review.showSection(at: roundIndex)
    check(review.rows.count == 2 && review.rows.allSatisfy { $0.change == "已删除" && $0.isDeleted },
          "round rows \(review.rows)")
    check(review.canUndoSelectedRound && !review.canDeleteSelection, "round buttons")
    review.undoSelectedRound(nil)
    check(review.notice?.hasPrefix("已恢复 2 个词") == true, "undo notice \(String(describing: review.notice))")
    check(review.sections[roundIndex].detail.hasPrefix("已撤销"), "round detail \(review.sections[roundIndex])")
    check(!review.canUndoSelectedRound, "an undone round cannot be undone again")
    check(LearnedWordsLedger.parseExport(windowEngine.rows["rime_ice"] ?? "").count == 3,
          "window undo did not restore the words")
    review.window?.close()

    if failures.isEmpty {
        print("learned-words-deletion-smoke: PASS delete, backup, daily record, undo, revive, failures, pruning, window")
        return true
    }
    failures.forEach { print("learned-words-deletion-smoke: FAIL \($0)") }
    return false
}
