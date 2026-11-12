import Foundation

/// One learned row as librime exports it: `word<TAB>code<TAB>count`.
struct LearnedWordRecord: Codable, Equatable, Sendable {
    let word: String
    let code: String
    let count: Int

    var key: LearnedWordKey { LearnedWordKey(word: word, code: code) }
}

struct LearnedWordKey: Hashable, Codable, Sendable {
    let word: String
    let code: String
}

/// A word that was already learned and was used again on this day.
struct LearnedWordReuse: Codable, Equatable, Sendable {
    let word: String
    let code: String
    let fromCount: Int
    let toCount: Int
}

/// What changed in one learning dictionary on one local calendar day.
struct LearnedWordsDay: Codable, Equatable, Sendable {
    static let currentVersion = 1

    var version = LearnedWordsDay.currentVersion
    let day: String
    var added: [LearnedWordRecord]
    var reused: [LearnedWordReuse]
    var lastCapturedAt: Date
}

enum LearnedWordsCaptureResult: Equatable {
    /// First capture for this dictionary: every existing word becomes the
    /// undated baseline instead of being reported as learned today.
    case baseline(entryCount: Int)
    case recorded(day: String, added: Int, reused: Int, entryCount: Int)
}

enum LearnedWordsLedgerError: LocalizedError, Equatable {
    case engineUnavailable
    case exportFailed
    case unsafePath(String)
    case fileTooLarge(String)
    case invalidData(String)
    case fileOperationFailed(String)

    var errorDescription: String? {
        switch self {
        case .engineUnavailable: return "Rime 引擎尚未就绪。"
        case .exportFailed: return "无法从 Rime 导出学习词库。"
        case .unsafePath(let path): return "学习词记录目录不安全：\(path)"
        case .fileTooLarge(let path): return "学习词记录文件超过安全大小：\(path)"
        case .invalidData(let path): return "学习词记录文件无法读取：\(path)"
        case .fileOperationFailed(let detail): return "学习词记录写入失败：\(detail)"
        }
    }
}

/// Recording is opt-in: unlike the counters in Statistics, this keeps the
/// user's actual words on disk.
final class LearnedWordsReviewPreferences {
    static let shared = LearnedWordsReviewPreferences()
    static let didChange = Notification.Name("RIMES.LearnedWordsReview.didChange")
    private static let recordsDailyKey = "lexiconReview.recordsDaily.v1"

    private let defaults: UserDefaults

    init(defaults: UserDefaults = .standard) { self.defaults = defaults }

    var recordsDaily: Bool { defaults.bool(forKey: Self.recordsDailyKey) }

    func setRecordsDaily(_ enabled: Bool) {
        defaults.set(enabled, forKey: Self.recordsDailyKey)
        NotificationCenter.default.post(name: Self.didChange, object: self)
    }
}

/// Keeps a per-day record of words librime learned, by comparing each day's
/// export of the learning dictionary with the previous one. librime records
/// no dates of its own, so the comparison is the only source of "learned
/// today". Everything stays under `~/Library/RIMES/lexicon-review/` and is
/// read-only with respect to the Rime user dictionary.
final class LearnedWordsLedger {
    static let shared = LearnedWordsLedger(engine: rimeEngine)

    static let directoryName = "lexicon-review"
    private static let stateFileName = "state.tsv"
    private static let daysDirectoryName = "days"
    private static let maxStateBytes = 64 * 1_024 * 1_024
    private static let maxDayBytes = 16 * 1_024 * 1_024
    private static let checkInterval: TimeInterval = 30 * 60
    private static let firstCheckDelay: TimeInterval = 120

    private let engine: UserLexiconEngine
    private let fileManager: FileManager
    private let root: URL
    private let preferences: LearnedWordsReviewPreferences
    private let calendar: Calendar
    private var timer: Timer?

    init(engine: UserLexiconEngine,
         root: URL? = nil,
         preferences: LearnedWordsReviewPreferences = .shared,
         fileManager: FileManager = .default,
         calendar: Calendar = .current) {
        self.engine = engine
        self.fileManager = fileManager
        self.root = root ?? RimesPaths.userDirectory(fileManager: fileManager)
            .appendingPathComponent(Self.directoryName, isDirectory: true)
        self.preferences = preferences
        self.calendar = calendar
    }

    // MARK: Scheduling

    /// Called after the engine starts. Checks every half hour and captures at
    /// most once per day per dictionary, so typing is never interrupted by an
    /// export.
    func startDailyCapture() {
        DispatchQueue.main.async { [weak self] in
            guard let self, self.timer == nil else { return }
            let timer = Timer(timeInterval: Self.checkInterval, repeats: true) { [weak self] _ in
                self?.captureIfDue()
            }
            timer.tolerance = 60
            RunLoop.main.add(timer, forMode: .common)
            self.timer = timer
            DispatchQueue.main.asyncAfter(deadline: .now() + Self.firstCheckDelay) { [weak self] in
                self?.captureIfDue()
            }
        }
    }

    func captureIfDue(now: Date = Date()) {
        guard preferences.recordsDaily, engine.isHealthy else { return }
        let today = dayString(for: now)
        for kind in UserLexiconKind.allCases
        where engine.hasUserDictionary(named: kind.dictionaryName) {
            guard lastCapturedDay(for: kind) != today else { continue }
            do {
                let result = try capture(kind, now: now)
                IMELog.write("lexicon-review: captured \(kind.rawValue) \(result)")
            } catch {
                IMELog.write("lexicon-review: capture \(kind.rawValue) failed: \(error.localizedDescription)")
            }
        }
    }

    // MARK: Capture

    @discardableResult
    func capture(_ kind: UserLexiconKind, now: Date = Date()) throws -> LearnedWordsCaptureResult {
        guard engine.isHealthy else { throw LearnedWordsLedgerError.engineUnavailable }
        let directory = try ensureDirectory(dictionaryDirectory(for: kind))
        let exportURL = directory.appendingPathComponent(".export-\(UUID().uuidString).tsv")
        defer { try? fileManager.removeItem(at: exportURL) }
        guard engine.exportUserDictionary(named: kind.dictionaryName, to: exportURL) >= 0 else {
            throw LearnedWordsLedgerError.exportFailed
        }
        let current = try Self.parseExport(readText(exportURL, limit: Self.maxStateBytes))
        let day = dayString(for: now)

        guard let previous = try loadState(for: kind) else {
            try saveState(current, day: day, at: now, for: kind)
            return .baseline(entryCount: current.count)
        }

        var record = try loadDay(day, for: kind)
            ?? LearnedWordsDay(day: day, added: [], reused: [], lastCapturedAt: now)
        var addedIndex = Dictionary(uniqueKeysWithValues:
            record.added.enumerated().map { ($0.element.key, $0.offset) })
        var reusedIndex = Dictionary(uniqueKeysWithValues:
            record.reused.enumerated().map { (LearnedWordKey(word: $0.element.word,
                                                            code: $0.element.code),
                                              $0.offset) })
        var newlyAdded = 0
        var newlyReused = 0

        for (key, count) in current.sorted(by: { $0.key.word < $1.key.word }) {
            let before = previous[key] ?? 0
            guard count > before else { continue }
            if let index = addedIndex[key] {
                record.added[index] = LearnedWordRecord(word: key.word, code: key.code, count: count)
            } else if before <= 0 {
                addedIndex[key] = record.added.count
                record.added.append(LearnedWordRecord(word: key.word, code: key.code, count: count))
                newlyAdded += 1
            } else if let index = reusedIndex[key] {
                let earlier = record.reused[index]
                record.reused[index] = LearnedWordReuse(word: key.word, code: key.code,
                                                        fromCount: earlier.fromCount,
                                                        toCount: count)
            } else {
                reusedIndex[key] = record.reused.count
                record.reused.append(LearnedWordReuse(word: key.word, code: key.code,
                                                      fromCount: before, toCount: count))
                newlyReused += 1
            }
        }
        record.lastCapturedAt = now
        // An unchanged day leaves no file, so the review list never shows
        // empty days.
        if !record.added.isEmpty || !record.reused.isEmpty {
            try saveDay(record, for: kind)
        }
        try saveState(current, day: day, at: now, for: kind)
        return .recorded(day: day, added: newlyAdded, reused: newlyReused,
                         entryCount: current.count)
    }

    // MARK: Reading

    /// Newest first.
    func days(for kind: UserLexiconKind) -> [LearnedWordsDay] {
        guard let names = try? fileManager.contentsOfDirectory(
            atPath: daysDirectory(for: kind).path
        ) else { return [] }
        return names
            .filter { $0.hasSuffix(".json") }
            .sorted(by: >)
            .compactMap { try? loadDay(String($0.dropLast(5)), for: kind) }
    }

    /// The most recent full export; the undated baseline lives here too.
    func currentEntries(for kind: UserLexiconKind) -> [LearnedWordRecord] {
        guard let state = try? loadState(for: kind) else { return [] }
        return state.map { LearnedWordRecord(word: $0.key.word, code: $0.key.code, count: $0.value) }
    }

    func lastCapturedDay(for kind: UserLexiconKind) -> String? {
        try? stateHeader(for: kind)?.day
    }

    func lastCapturedAt(for kind: UserLexiconKind) -> Date? {
        try? stateHeader(for: kind)?.date
    }

    // MARK: Parsing

    /// Keeps positive counts only: librime marks deleted words with a count of
    /// zero or below, and those are not learned words any more.
    static func parseExport(_ text: String) -> [LearnedWordKey: Int] {
        var result: [LearnedWordKey: Int] = [:]
        for line in text.split(whereSeparator: \.isNewline) where !line.hasPrefix("#") {
            let fields = line.split(separator: "\t", omittingEmptySubsequences: false)
            guard fields.count >= 3,
                  let count = Int(fields[2].trimmingCharacters(in: .whitespaces)),
                  count > 0 else { continue }
            let word = String(fields[0])
            let code = fields[1].trimmingCharacters(in: .whitespaces)
            guard !word.isEmpty else { continue }
            let key = LearnedWordKey(word: word, code: code)
            result[key] = max(result[key] ?? 0, count)
        }
        return result
    }

    // MARK: Storage

    func dictionaryDirectory(for kind: UserLexiconKind) -> URL {
        root.appendingPathComponent(kind.dictionaryName, isDirectory: true)
    }

    private func daysDirectory(for kind: UserLexiconKind) -> URL {
        dictionaryDirectory(for: kind).appendingPathComponent(Self.daysDirectoryName,
                                                               isDirectory: true)
    }

    private func stateURL(for kind: UserLexiconKind) -> URL {
        dictionaryDirectory(for: kind).appendingPathComponent(Self.stateFileName)
    }

    private func dayURL(_ day: String, for kind: UserLexiconKind) -> URL {
        daysDirectory(for: kind).appendingPathComponent("\(day).json")
    }

    private func loadState(for kind: UserLexiconKind) throws -> [LearnedWordKey: Int]? {
        let url = stateURL(for: kind)
        guard fileManager.fileExists(atPath: url.path) else { return nil }
        return Self.parseExport(try readText(url, limit: Self.maxStateBytes))
    }

    private func stateHeader(for kind: UserLexiconKind) throws -> (day: String, date: Date?)? {
        let url = stateURL(for: kind)
        guard fileManager.fileExists(atPath: url.path) else { return nil }
        try requireRegularFile(url)
        let handle = try FileHandle(forReadingFrom: url)
        defer { try? handle.close() }
        let head = String(decoding: handle.readData(ofLength: 512), as: UTF8.self)
        var day: String?
        var date: Date?
        for line in head.split(separator: "\n").prefix(4) {
            if line.hasPrefix("#@/captured_day\t") {
                day = String(line.dropFirst("#@/captured_day\t".count))
            } else if line.hasPrefix("#@/captured_at\t"),
                      let seconds = TimeInterval(line.dropFirst("#@/captured_at\t".count)) {
                date = Date(timeIntervalSince1970: seconds)
            }
        }
        return day.map { ($0, date) }
    }

    private func saveState(_ entries: [LearnedWordKey: Int], day: String, at date: Date,
                           for kind: UserLexiconKind) throws {
        var lines = [
            "# RIMES learned-word review state",
            "#@/captured_day\t\(day)",
            "#@/captured_at\t\(Int(date.timeIntervalSince1970))",
            "#@/db_name\t\(kind.dictionaryName)",
        ]
        lines.reserveCapacity(entries.count + lines.count)
        for (key, count) in entries.sorted(by: { ($0.key.word, $0.key.code) < ($1.key.word, $1.key.code) }) {
            lines.append("\(key.word)\t\(key.code)\t\(count)")
        }
        try writeAtomically(Data((lines.joined(separator: "\n") + "\n").utf8),
                            to: stateURL(for: kind))
    }

    private func loadDay(_ day: String, for kind: UserLexiconKind) throws -> LearnedWordsDay? {
        let url = dayURL(day, for: kind)
        guard fileManager.fileExists(atPath: url.path) else { return nil }
        let data = Data(try readText(url, limit: Self.maxDayBytes).utf8)
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .secondsSince1970
        guard let record = try? decoder.decode(LearnedWordsDay.self, from: data),
              record.day == day else {
            throw LearnedWordsLedgerError.invalidData(url.path)
        }
        return record
    }

    private func saveDay(_ record: LearnedWordsDay, for kind: UserLexiconKind) throws {
        _ = try ensureDirectory(daysDirectory(for: kind))
        let encoder = JSONEncoder()
        encoder.dateEncodingStrategy = .secondsSince1970
        encoder.outputFormatting = [.sortedKeys]
        let data: Data
        do { data = try encoder.encode(record) } catch {
            throw LearnedWordsLedgerError.fileOperationFailed(error.localizedDescription)
        }
        try writeAtomically(data, to: dayURL(record.day, for: kind))
    }

    func dayString(for date: Date) -> String {
        let parts = calendar.dateComponents([.year, .month, .day], from: date)
        return String(format: "%04d-%02d-%02d", parts.year ?? 0, parts.month ?? 0, parts.day ?? 0)
    }

    // MARK: File safety

    private func ensureDirectory(_ url: URL) throws -> URL {
        let owner = root.deletingLastPathComponent()
        do {
            try fileManager.createDirectory(at: owner, withIntermediateDirectories: true)
        } catch {
            throw LearnedWordsLedgerError.fileOperationFailed(error.localizedDescription)
        }
        var path = URL(fileURLWithPath: "/")
        let rootComponents = owner.pathComponents
        for component in url.pathComponents.dropFirst() {
            path.appendPathComponent(component, isDirectory: true)
            // Only the directories this ledger owns must not be links; the
            // user directory above it may legitimately be one.
            guard path.pathComponents.count > rootComponents.count else { continue }
            if let values = try? path.resourceValues(forKeys: [.isSymbolicLinkKey]),
               values.isSymbolicLink == true {
                throw LearnedWordsLedgerError.unsafePath(path.path)
            }
            var isDirectory: ObjCBool = false
            if fileManager.fileExists(atPath: path.path, isDirectory: &isDirectory) {
                guard isDirectory.boolValue else {
                    throw LearnedWordsLedgerError.unsafePath(path.path)
                }
            } else {
                do {
                    try fileManager.createDirectory(at: path, withIntermediateDirectories: false,
                                                    attributes: [.posixPermissions: 0o700])
                } catch {
                    throw LearnedWordsLedgerError.fileOperationFailed(error.localizedDescription)
                }
            }
        }
        return url
    }

    private func requireRegularFile(_ url: URL) throws {
        let values = try? url.resourceValues(forKeys: [.isSymbolicLinkKey, .isRegularFileKey])
        guard values?.isSymbolicLink != true, values?.isRegularFile == true else {
            throw LearnedWordsLedgerError.unsafePath(url.path)
        }
    }

    private func readText(_ url: URL, limit: Int) throws -> String {
        try requireRegularFile(url)
        let size = (try? fileManager.attributesOfItem(atPath: url.path)[.size] as? NSNumber)?.intValue ?? 0
        guard size <= limit else { throw LearnedWordsLedgerError.fileTooLarge(url.path) }
        guard let data = fileManager.contents(atPath: url.path),
              let text = String(data: data, encoding: .utf8) else {
            throw LearnedWordsLedgerError.invalidData(url.path)
        }
        return text
    }

    private func writeAtomically(_ data: Data, to url: URL) throws {
        _ = try ensureDirectory(url.deletingLastPathComponent())
        if let values = try? url.resourceValues(forKeys: [.isSymbolicLinkKey]),
           values.isSymbolicLink == true {
            throw LearnedWordsLedgerError.unsafePath(url.path)
        }
        let temporary = url.deletingLastPathComponent()
            .appendingPathComponent(".\(url.lastPathComponent).\(UUID().uuidString)")
        do {
            try data.write(to: temporary, options: .withoutOverwriting)
            try fileManager.setAttributes([.posixPermissions: 0o600], ofItemAtPath: temporary.path)
            if fileManager.fileExists(atPath: url.path) {
                _ = try fileManager.replaceItemAt(url, withItemAt: temporary)
            } else {
                try fileManager.moveItem(at: temporary, to: url)
            }
        } catch {
            try? fileManager.removeItem(at: temporary)
            throw LearnedWordsLedgerError.fileOperationFailed(error.localizedDescription)
        }
    }
}
