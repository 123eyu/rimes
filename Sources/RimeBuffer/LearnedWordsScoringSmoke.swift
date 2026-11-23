import AppKit

/// Scripted stand-in for the Codex connector: each call pops the next reply.
private final class LearnedWordsFakeScoringBackend: LearnedWordsScoringBackend {
    var availability: AITextProviderAvailability = .ready
    var replies: [([String]) -> Result<String, AITextProviderError>] = []
    private(set) var prompts: [String] = []
    private(set) var models: [String] = []

    func score(prompt: String, model: String,
               completion: @escaping (Result<String, AITextProviderError>) -> Void)
        -> any AITextCancellable {
        prompts.append(prompt)
        models.append(model)
        let words = prompt.components(separatedBy: "：\n").last?
            .split(separator: "\n").map(String.init) ?? []
        let reply = replies.isEmpty ? { _ in .failure(.failed) } : replies.removeFirst()
        completion(reply(words))
        return AITextNoopCancellation()
    }
}

private final class LearnedWordsRecordingNotifier: LearnedWordsScoringNotifier {
    private(set) var calls: [(kind: UserLexiconKind, scored: Int, unscored: Int, model: String)] = []
    func scoringRoundFinished(kind: UserLexiconKind, scored: Int, unscored: Int, model: String) {
        calls.append((kind, scored, unscored, model))
    }
}

/// Step 3 of docs/plans/learned-words-review.md: parsing, preferences and
/// exclusions, candidate order, batched rounds (partial failure and stop),
/// score storage, the Mailbox notice and the review window's score actions.
/// Everything is isolated; the scoring backend is a local fake.
@discardableResult
func runLearnedWordsScoringSmokeTest() -> Bool {
    let fm = FileManager.default
    let base = fm.temporaryDirectory
        .appendingPathComponent("rimes-learned-words-scoring-\(UUID().uuidString)", isDirectory: true)
    defer { try? fm.removeItem(at: base) }
    let suite = "RIMES.LearnedWordsScoringSmoke.\(UUID().uuidString)"
    guard let defaults = UserDefaults(suiteName: suite) else { return false }
    defer { defaults.removePersistentDomain(forName: suite) }

    var failures: [String] = []
    func check(_ condition: Bool, _ message: String) {
        if !condition { failures.append(message) }
    }
    func scoreLines(_ pairs: [(String, Int)]) -> String {
        pairs.map { "\($0.0)\t\($0.1)" }.joined(separator: "\n")
    }

    // Parsing.
    let expected: Set<String> = ["你好", "错词", "回顾", "半句话吧"]
    let parsed = LearnedWordsScoringPrompt.parse(
        "你好\t1\r\n错词 5\n回顾\t9\n不存在\t3\n半句话吧\t4\n半句话吧\t2\n乱写\n\t3\n",
        expected: expected
    )
    check(parsed == ["你好": 1, "错词": 5], "parse \(parsed)")
    check(LearnedWordsScoringPrompt.parse("你好\t2\n你好\t2", expected: expected) == ["你好": 2],
          "repeated identical scores should be kept")
    let prompt = LearnedWordsScoringPrompt.prompt(for: ["甲", "乙"])
    check(prompt.hasSuffix("待评分的词（共 2 个）：\n甲\n乙") && prompt.contains("词<TAB>分数"),
          "prompt shape")

    // Preferences: off by default, sanitised values, exclusions on.
    let preferences = LearnedWordsScoringPreferences(defaults: defaults)
    check(!preferences.isEnabled, "scoring must be off by default")
    check(preferences.model == "gpt-6-luna" && preferences.batchSize == 200
            && preferences.roundLimit == 600, "defaults")
    preferences.setModel("not-a-model")
    preferences.setBatchSize(7)
    check(preferences.model == "gpt-6-luna" && preferences.batchSize == 200,
          "invalid choices must be ignored")
    check(preferences.excludes("第3版") && preferences.excludes("iPhone")
            && preferences.excludes("一二三四五六七八九") && !preferences.excludes("回顾"),
          "default exclusions")
    preferences.setExcludesLatin(false)
    check(!preferences.excludes("iPhone"), "latin exclusion can be turned off")
    preferences.setExcludesLatin(true)

    // Candidates: unscored, not excluded, newest learned first, then rare.
    var calendar = Calendar(identifier: .gregorian)
    calendar.timeZone = TimeZone(identifier: "UTC")!
    let day1 = Date(timeIntervalSince1970: 1_794_355_200)        // 2026-11-11 00:00 UTC
    let day2 = day1.addingTimeInterval(26 * 3_600)
    let reviewPreferences = LearnedWordsReviewPreferences(defaults: defaults)
    reviewPreferences.setRecordsDaily(true)
    let engine = LearnedWordsFakeEngine()
    engine.rows["rime_ice"] = "你好\tni hao\t9\n世界\tshi jie\t1\n输入法\tshu ru fa\t2\n第3版\tdi san ban\t1\niPhone\tiphone\t1\n"
    let root = base.appendingPathComponent("RIMES/lexicon-review", isDirectory: true)
    let ledger = LearnedWordsLedger(engine: engine, root: root, preferences: reviewPreferences,
                                    calendar: calendar)
    _ = try? ledger.capture(.chinese, now: day1)
    engine.type("错词", code: "cuo ci", in: "rime_ice")
    engine.type("新学", code: "xin xue", in: "rime_ice")
    _ = try? ledger.capture(.chinese, now: day2)
    try? ledger.recordScores(["你好": 1], roundID: "seed", for: .chinese, now: day1)

    let candidates = ledger.scoringCandidates(for: .chinese, preferences: preferences, limit: 10)
    check(candidates == ["新学", "错词", "世界", "输入法"], "candidates \(candidates)")
    check(ledger.scoringCandidates(for: .chinese, preferences: preferences, limit: 2) == ["新学", "错词"],
          "round limit")
    let scoresFile = root.appendingPathComponent("rime_ice/scores.json")
    let mode = (try? fm.attributesOfItem(atPath: scoresFile.path)[.posixPermissions] as? NSNumber)?.intValue
    check(mode == 0o600, "scores permissions \(String(describing: mode))")

    // A round in batches of 2: the second batch partly parses, the third fails.
    let backend = LearnedWordsFakeScoringBackend()
    backend.replies = [
        { words in .success(scoreLines(words.map { ($0, $0 == "错词" ? 5 : 2) })) },
        { _ in .success("世界\t4\n输入法\t不知道\n") },
    ]
    var progress: [Int] = []
    var outcome: LearnedWordsScoringRound.Outcome?
    let round = LearnedWordsScoringRound(ledger: ledger, kind: .chinese, words: candidates,
                                         model: "gpt-6-luna", batchSize: 2, backend: backend,
                                         now: { day2 })
    round.start(progress: { done, _ in progress.append(done) }, completion: { outcome = $0 })
    check(round.batchCount == 2, "batches \(round.batchCount)")
    check(outcome == .finished(scored: 3, unscored: 1), "outcome \(String(describing: outcome))")
    check(backend.models == ["gpt-6-luna", "gpt-6-luna"], "model per request")
    check(backend.prompts.allSatisfy { !$0.contains("cuo ci") && !$0.contains("\t9") },
          "only words may be sent, never codes or counts")
    let stored = ledger.scores(for: .chinese)
    check(stored.mapValues(\.score) == ["你好": 1, "新学": 2, "错词": 5, "世界": 4],
          "stored \(stored.mapValues(\.score))")
    check(stored["错词"]?.roundID == round.id, "round id")
    check(ledger.scoringCandidates(for: .chinese, preferences: preferences, limit: 10) == ["输入法"],
          "scored words must not be sent again")

    let failing = LearnedWordsFakeScoringBackend()
    failing.replies = [{ _ in .failure(.timedOut) }]
    var failed: LearnedWordsScoringRound.Outcome?
    LearnedWordsScoringRound(ledger: ledger, kind: .chinese, words: ["输入法"], model: "gpt-6-luna",
                             batchSize: 2, backend: failing)
        .start(progress: { _, _ in }, completion: { failed = $0 })
    check(failed == .failed(scored: 0, message: AITextProviderError.timedOut.userFacingMessage),
          "failure \(String(describing: failed))")

    // Mailbox notice: counts only, posted as a local-notes-only plugin thread.
    let body = MailboxLearnedWordsScoringNotifier.body(kind: .chinese, scored: 3, unscored: 1,
                                                       model: "gpt-6-luna")
    check(body.contains("本轮已评分 3 个词") && body.contains("1 个词") && !body.contains("错词"),
          "notice body \(body)")
    if let mailbox = try? MailboxStore(storageRoot: base.appendingPathComponent("mailbox")) {
        MailboxLearnedWordsScoringNotifier(store: { mailbox })
            .scoringRoundFinished(kind: .chinese, scored: 3, unscored: 1, model: "gpt-6-luna")
        let thread = mailbox.snapshot.threads.first
        check(thread?.source.kind == .plugin && thread?.source.replyCapability == .localNotesOnly
                && thread?.title == "学习词评分完成" && thread?.unread == true,
              "mailbox thread \(String(describing: thread?.source))")
    } else {
        failures.append("isolated mailbox store unavailable")
    }

    // Review window: score column, filters, one-click delete of score 5.
    let windowNotifier = LearnedWordsRecordingNotifier()
    let windowBackend = LearnedWordsFakeScoringBackend()
    windowBackend.replies = [{ words in .success(scoreLines(words.map { ($0, 3) })) }]
    let review = LearnedWordsReviewWindowController(
        ledger: ledger, preferences: reviewPreferences, scoringPreferences: preferences,
        scoringBackend: { windowBackend }, scoringNotifier: windowNotifier
    )
    review.show(kind: .chinese)
    if let all = review.sections.firstIndex(where: { $0.section == .all }) { review.showSection(at: all) }
    check(!review.canStartScoring, "scoring button hidden while disabled")
    check(review.rows.first?.word == "错词" && review.rows.first?.score == 5,
          "highest score first \(review.rows.map { "\($0.word):\(String(describing: $0.score))" })")
    review.setFilter(.unscored)
    check(review.rows.map(\.word).sorted() == ["iPhone", "第3版", "输入法"], "unscored \(review.rows.map(\.word))")
    review.setFilter(.flagged)
    check(review.rows.map(\.word).sorted() == ["世界", "错词"], "flagged \(review.rows.map(\.word))")
    review.setFilter(.everything)

    preferences.setEnabled(true)
    check(review.canStartScoring, "scoring button shown once enabled")
    review.startScoring(skipConfirmation: true)
    check(review.scoringRound == nil && review.notice?.hasPrefix("本轮已评分 1 个词") == true,
          "window round notice \(String(describing: review.notice))")
    check(windowNotifier.calls.count == 1 && windowNotifier.calls.first?.scored == 1,
          "notifier calls \(windowNotifier.calls.count)")
    check(ledger.scores(for: .chinese)["输入法"]?.score == 3, "window round stored")
    review.startScoring(skipConfirmation: true)
    check(review.notice?.hasPrefix("没有需要评分的词") == true, "nothing left \(String(describing: review.notice))")

    check(review.canDeleteHighestScored, "delete score 5 available")
    review.requestDeletionOfHighestScored(skipConfirmation: true)
    let remaining = LearnedWordsLedger.parseExport(engine.rows["rime_ice"] ?? "")
    check(remaining[LearnedWordKey(word: "错词", code: "cuo ci")] == nil
            && remaining[LearnedWordKey(word: "世界", code: "shi jie")] != nil,
          "only words scored 5 should be deleted")
    check(review.notice?.hasPrefix("已删除 1 个词") == true, "delete notice \(String(describing: review.notice))")
    check(!review.canDeleteHighestScored, "nothing scored 5 left")
    review.window?.close()

    if failures.isEmpty {
        print("learned-words-scoring-smoke: PASS parse, preferences, candidates, rounds, storage, mailbox, window")
        return true
    }
    failures.forEach { print("learned-words-scoring-smoke: FAIL \($0)") }
    return false
}
