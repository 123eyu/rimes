import Foundation

/// One user action that deleted learned words. The words keep the counts they
/// had just before deletion, which is what undo writes back.
struct LearnedWordsDeletionRound: Codable, Equatable, Sendable {
    static let currentVersion = 1

    var version = LearnedWordsDeletionRound.currentVersion
    let id: String
    let dictionary: String
    let deletedAt: Date
    /// Where the decision came from: "manual" today; scores later.
    let source: String
    let words: [LearnedWordRecord]
    var undoneAt: Date?
    /// File name under `backups/`; the file may since have been pruned.
    let backupFile: String?

    var isUndone: Bool { undoneAt != nil }
}

struct LearnedWordsDeletionResult: Equatable {
    let round: LearnedWordsDeletionRound
    /// Selected words that were no longer learned (already deleted, or never
    /// in this dictionary), so there was nothing to delete.
    let notFound: Int
    /// Words librime still reports after the import; zero unless the import
    /// was partial.
    let notDeleted: Int
}

enum LearnedWordsDeletionError: LocalizedError, Equatable {
    case nothingToDelete
    case importFailed
    case roundNotFound(String)
    case alreadyUndone(String)
    case restoreIncomplete(restored: Int, total: Int)

    var errorDescription: String? {
        switch self {
        case .nothingToDelete: return "所选的词已经不在学习词库里，没有需要删除的词。"
        case .importFailed: return "Rime 没有接受删除请求，学习词库没有改变。"
        case .roundNotFound: return "找不到这一轮删除记录。"
        case .alreadyUndone: return "这一轮删除已经撤销过了。"
        case .restoreIncomplete(let restored, let total):
            return "只恢复了 \(restored) / \(total) 个词；删除记录保留，可以再试一次。"
        }
    }
}

/// Deletion goes through librime's own importer: a row with a negative count
/// turns the stored entry into a tombstone that keeps the old count's size,
/// and re-importing the original count brings it back exactly. Typing a
/// deleted word again also revives it with its old count plus one; that is
/// librime's behaviour and cannot be changed through the importer.
extension LearnedWordsLedger {
    static let deletionsDirectoryName = "deletions"
    static let backupsDirectoryName = "backups"
    static let keptBackups = 10

    func deletionsDirectory(for kind: UserLexiconKind) -> URL {
        dictionaryDirectory(for: kind)
            .appendingPathComponent(Self.deletionsDirectoryName, isDirectory: true)
    }

    func backupsDirectory(for kind: UserLexiconKind) -> URL {
        dictionaryDirectory(for: kind)
            .appendingPathComponent(Self.backupsDirectoryName, isDirectory: true)
    }

    // MARK: Delete

    /// Backs up the full learning dictionary, deletes the given words that are
    /// still learned, and checks with a fresh export that they are gone.
    @discardableResult
    func delete(_ keys: [LearnedWordKey],
                from kind: UserLexiconKind,
                source: String = "manual",
                now: Date = Date()) throws -> LearnedWordsDeletionResult {
        let requested = Set(keys)
        guard !requested.isEmpty else { throw LearnedWordsDeletionError.nothingToDelete }
        let beforeText = try exportText(kind)
        let before = Self.parseExport(beforeText)
        let targets = requested
            .compactMap { key in before[key].map { LearnedWordRecord(word: key.word, code: key.code, count: $0) } }
            .sorted { ($0.word, $0.code) < ($1.word, $1.code) }
        guard !targets.isEmpty else { throw LearnedWordsDeletionError.nothingToDelete }

        let id = roundID(for: now)
        let backupName = "\(id).tsv"
        try writeAtomically(Data(beforeText.utf8),
                            to: backupsDirectory(for: kind).appendingPathComponent(backupName))

        try importRows(targets.map { "\($0.word)\t\($0.code)\t-1" }, into: kind)
        let after = Self.parseExport(try exportText(kind))
        let deleted = targets.filter { (after[$0.key] ?? 0) <= 0 }
        guard !deleted.isEmpty else { throw LearnedWordsDeletionError.importFailed }

        let round = LearnedWordsDeletionRound(id: id, dictionary: kind.dictionaryName,
                                              deletedAt: now, source: source,
                                              words: deleted, undoneAt: nil,
                                              backupFile: backupName)
        try saveRound(round, for: kind)
        try updateState(for: kind) { state in
            for word in deleted { state[word.key] = nil }
        }
        pruneBackups(for: kind)
        IMELog.write("lexicon-review: deleted \(deleted.count) from \(kind.rawValue) round=\(id)")
        return LearnedWordsDeletionResult(round: round,
                                          notFound: requested.count - targets.count,
                                          notDeleted: targets.count - deleted.count)
    }

    // MARK: Undo

    /// Re-imports the round's original counts. librime keeps the larger of
    /// the stored and imported counts, so a word typed again since the
    /// deletion keeps its newer, higher count.
    @discardableResult
    func undo(roundID: String, for kind: UserLexiconKind,
              now: Date = Date()) throws -> LearnedWordsDeletionRound {
        guard var round = try loadRound(roundID, for: kind) else {
            throw LearnedWordsDeletionError.roundNotFound(roundID)
        }
        guard !round.isUndone else { throw LearnedWordsDeletionError.alreadyUndone(roundID) }
        try importRows(round.words.map { "\($0.word)\t\($0.code)\t\($0.count)" }, into: kind)
        let after = Self.parseExport(try exportText(kind))
        let restored = round.words.filter { (after[$0.key] ?? 0) >= $0.count }
        guard restored.count == round.words.count else {
            throw LearnedWordsDeletionError.restoreIncomplete(restored: restored.count,
                                                             total: round.words.count)
        }
        round.undoneAt = now
        try saveRound(round, for: kind)
        try updateState(for: kind) { state in
            for word in round.words { state[word.key] = after[word.key] }
        }
        IMELog.write("lexicon-review: undid round \(roundID) in \(kind.rawValue)")
        return round
    }

    // MARK: Reading

    /// Newest first.
    func deletionRounds(for kind: UserLexiconKind) -> [LearnedWordsDeletionRound] {
        guard let names = try? fileManager.contentsOfDirectory(
            atPath: deletionsDirectory(for: kind).path
        ) else { return [] }
        return names
            .filter { $0.hasSuffix(".json") && !$0.hasPrefix(".") }
            .sorted(by: >)
            .compactMap { try? loadRound(String($0.dropLast(5)), for: kind) }
    }

    /// Words removed by a round that is not undone and not learned again since.
    func currentlyDeleted(for kind: UserLexiconKind) -> Set<LearnedWordKey> {
        let learned = Set(currentEntries(for: kind).map(\.key))
        var deleted = Set<LearnedWordKey>()
        for round in deletionRounds(for: kind) where !round.isUndone {
            for word in round.words where !learned.contains(word.key) {
                deleted.insert(word.key)
            }
        }
        return deleted
    }

    // MARK: Helpers

    private func importRows(_ rows: [String], into kind: UserLexiconKind) throws {
        let directory = try ensureDirectory(dictionaryDirectory(for: kind))
        let url = directory.appendingPathComponent(".import-\(UUID().uuidString).tsv")
        defer { try? fileManager.removeItem(at: url) }
        let text = (["# Rime user dictionary export"] + rows).joined(separator: "\n") + "\n"
        try writeAtomically(Data(text.utf8), to: url)
        guard engine.importUserDictionary(named: kind.dictionaryName, from: url) >= 0 else {
            throw LearnedWordsDeletionError.importFailed
        }
    }

    /// Edits the saved state in place and keeps its capture day and time, so
    /// words learned since the last daily capture are still reported as new.
    private func updateState(for kind: UserLexiconKind,
                             _ edit: (inout [LearnedWordKey: Int]) -> Void) throws {
        guard var state = try loadState(for: kind),
              let header = try stateHeader(for: kind) else { return }
        edit(&state)
        try saveState(state, day: header.day, at: header.date ?? Date(), for: kind)
    }

    private func roundURL(_ id: String, for kind: UserLexiconKind) -> URL {
        deletionsDirectory(for: kind).appendingPathComponent("\(id).json")
    }

    private func loadRound(_ id: String, for kind: UserLexiconKind) throws -> LearnedWordsDeletionRound? {
        let url = roundURL(id, for: kind)
        guard fileManager.fileExists(atPath: url.path) else { return nil }
        let data = Data(try readText(url, limit: Self.maxStateBytes).utf8)
        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .secondsSince1970
        guard let round = try? decoder.decode(LearnedWordsDeletionRound.self, from: data),
              round.id == id else {
            throw LearnedWordsLedgerError.invalidData(url.path)
        }
        return round
    }

    private func saveRound(_ round: LearnedWordsDeletionRound, for kind: UserLexiconKind) throws {
        let encoder = JSONEncoder()
        encoder.dateEncodingStrategy = .secondsSince1970
        encoder.outputFormatting = [.sortedKeys]
        let data: Data
        do { data = try encoder.encode(round) } catch {
            throw LearnedWordsLedgerError.fileOperationFailed(error.localizedDescription)
        }
        try writeAtomically(data, to: roundURL(round.id, for: kind))
    }

    private func pruneBackups(for kind: UserLexiconKind) {
        let directory = backupsDirectory(for: kind)
        guard let names = try? fileManager.contentsOfDirectory(atPath: directory.path) else { return }
        let backups = names.filter { $0.hasSuffix(".tsv") && !$0.hasPrefix(".") }.sorted(by: >)
        for name in backups.dropFirst(Self.keptBackups) {
            try? fileManager.removeItem(at: directory.appendingPathComponent(name))
        }
    }

    /// Sorts by time (UTC, so a daylight-saving change cannot reorder rounds);
    /// the suffix keeps two rounds in the same second apart.
    private func roundID(for date: Date) -> String {
        let parts = Calendar(identifier: .gregorian).dateComponents(
            in: TimeZone(identifier: "UTC")!, from: date
        )
        let stamp = String(format: "%04d%02d%02d-%02d%02d%02d",
                           parts.year ?? 0, parts.month ?? 0, parts.day ?? 0,
                           parts.hour ?? 0, parts.minute ?? 0, parts.second ?? 0)
        return "\(stamp)-\(UUID().uuidString.prefix(4).lowercased())"
    }
}
