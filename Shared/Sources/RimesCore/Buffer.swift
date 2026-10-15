import Foundation

public enum InputScheme: String, Codable, CaseIterable, Identifiable {
    case pinyin, ziranma, wubi86, lingxiWubi, english, chord
    public var id: String { rawValue }
    public var title: String { switch self { case .pinyin: return "全拼 · Pinyin"; case .ziranma: return "自然码 · Ziranma"; case .wubi86: return "五笔 86 · Wubi"; case .lingxiWubi: return "犀术五笔 · XSIME Wubi"; case .english: return "English"; case .chord: return "并击 · Chord" } }
    public var schemaID: String { switch self { case .pinyin, .chord: return "rimes_pinyin"; case .ziranma: return "rimes_ziranma"; case .wubi86: return "rimes_wubi"; case .lingxiWubi: return "lingxi_wubi"; case .english: return "" } }
}
public struct EngineSnapshot {
    public var preedit: String
    public var candidates: [String]
    public var commit: String
    public init(preedit: String = "", candidates: [String] = [], commit: String = "") { self.preedit = preedit; self.candidates = candidates; self.commit = commit }
}
public protocol InputEngine: AnyObject {
    func select(schema: String) -> Bool
    func process(key: Int32) -> EngineSnapshot
    func candidate(_ index: Int) -> EngineSnapshot
    func clear()
}
public enum TextBlocks {
    private static let enders: Set<Character> = ["。", "！", "？", "!", "?"]
    private static let closers: Set<Character> = ["”", "’", "」", "』", "）", ")", "》", "】", "\""]
    /// Preserve every original character, including inter-sentence whitespace. A block
    /// ends after a sentence ender or line break and keeps the closing quotes, further
    /// enders and line breaks that follow it, so a line break never becomes a block of
    /// its own (inserting a lone "\n" acts as Return, which sends in chat apps).
    public static func split(_ text: String) -> [String] {
        var blocks: [String] = [], current = "", closed = false
        for c in text {
            if closed && !(c.isNewline || enders.contains(c) || closers.contains(c)) { blocks.append(current); current = ""; closed = false }
            current.append(c)
            if c.isNewline || enders.contains(c) || current.count >= 160 { closed = true }
        }
        if !current.isEmpty { blocks.append(current) }
        return blocks
    }
}
public struct BufferSession {
    public private(set) var source = ""
    public private(set) var sourceRevision = UUID()
    public private(set) var generation: UUID?
    public private(set) var preview = ""
    public private(set) var result: [String]?
    public private(set) var retainedResults: [String] = []
    public private(set) var cursor = 0 // Character offset, not UTF-16.
    /// Fixed end of a selection; the cursor is the moving end. Any edit or plain
    /// cursor move clears it.
    public private(set) var selectionAnchor: Int?
    /// Character offsets where blocks end, excluding 0 and `source.count`. Each
    /// commit is its own block, as on the desktop; punctuation and continued Latin
    /// words join the block before them. Text set whole via `edit` falls back to
    /// clause segmentation.
    public private(set) var boundaries: [Int] = []
    public private(set) var requestSourceRevision: UUID?
    public init() {}
    public var generating: Bool { generation != nil }
    public mutating func edit(_ text: String, cursor: Int? = nil) {
        var offset = 0
        let seeded = DefaultBlockSegmenter.segments(from: text).map { offset += $0.count; return offset }
        replace(text, cursor: cursor ?? text.count, boundaries: seeded)
    }
    private mutating func replace(_ text: String, cursor: Int, boundaries: [Int]) {
        source = text; self.cursor = min(max(0, cursor), text.count); selectionAnchor = nil
        self.boundaries = Array(Set(boundaries.filter { $0 > 0 && $0 < text.count })).sorted()
        sourceRevision = UUID(); cancel(); result = nil; preview = ""
    }
    /// Source split at block boundaries; concatenation always equals `source`.
    public var blocks: [String] {
        guard !source.isEmpty else { return [] }
        var result: [String] = [], start = source.startIndex, previous = 0
        for end in boundaries + [source.count] {
            let index = source.index(start, offsetBy: end - previous)
            result.append(String(source[start..<index])); start = index; previous = end
        }
        return result
    }
    public mutating func moveCursor(_ offset: Int) { selectionAnchor = nil; cursor = min(max(0, cursor + offset), source.count) }
    /// Selected character range (anchor to cursor), or nil when nothing is selected.
    public var selection: Range<Int>? {
        guard let anchor = selectionAnchor, anchor != cursor else { return nil }
        return min(anchor, cursor)..<max(anchor, cursor)
    }
    public mutating func beginSelection() { selectionAnchor = cursor }
    /// Moves only the cursor end; the anchor stays where the selection began.
    public mutating func extendSelection(_ offset: Int) {
        if selectionAnchor == nil { selectionAnchor = cursor }
        cursor = min(max(0, cursor + offset), source.count)
    }
    public mutating func clearSelection() { selectionAnchor = nil }
    public mutating func insert(_ text: String) {
        guard !text.isEmpty else { return }
        let i = source.index(source.startIndex, offsetBy: cursor)
        var new = source; new.insert(contentsOf: text, at: i)
        let count = text.count
        var marks = boundaries.map { $0 > cursor ? $0 + count : $0 }
        let before = cursor > 0 ? source[source.index(before: i)] : nil
        let joins = before.map { Self.joins(text, after: $0) } ?? false
        if !joins { marks.append(cursor) }
        marks.append(cursor + count)
        replace(new, cursor: cursor + count, boundaries: marks)
    }
    /// Punctuation/whitespace attach to the preceding block; Latin letters and
    /// digits continue a preceding Latin word. Everything else starts a block.
    static func joins(_ text: String, after previous: Character) -> Bool {
        let punctuation = CharacterSet.punctuationCharacters.union(.whitespacesAndNewlines).union(.symbols)
        if text.unicodeScalars.allSatisfy({ punctuation.contains($0) }) && !text.unicodeScalars.contains(where: { $0.properties.isEmojiPresentation }) { return true }
        func latin(_ c: Character) -> Bool { c.isASCII && (c.isLetter || c.isNumber) }
        return text.allSatisfy(latin) && latin(previous)
    }
    public mutating func backspace() {
        if let range = selection { remove(range); return }
        guard cursor > 0 else { selectionAnchor = nil; return }
        remove(cursor - 1..<cursor)
    }
    /// Deletes the whole block holding the character before the cursor (a
    /// selection, if any, is deleted instead). The cursor lands at the block start.
    public mutating func deleteBlockBackward() {
        if let range = selection { remove(range); return }
        guard cursor > 0 else { selectionAnchor = nil; return }
        let start = boundaries.last { $0 < cursor } ?? 0
        let end = boundaries.first { $0 >= cursor } ?? source.count
        remove(start..<end)
    }
    /// Removes characters; boundaries inside the range collapse to its start.
    private mutating func remove(_ range: Range<Int>) {
        var new = source
        new.removeSubrange(new.index(new.startIndex, offsetBy: range.lowerBound)..<new.index(new.startIndex, offsetBy: range.upperBound))
        let marks = boundaries.map { $0 <= range.lowerBound ? $0 : $0 >= range.upperBound ? $0 - range.count : range.lowerBound }
        replace(new, cursor: range.lowerBound, boundaries: marks)
    }
    public mutating func begin() -> UUID {
        let id = UUID(); generation = id; requestSourceRevision = sourceRevision; preview = ""; result = nil; return id
    }
    public mutating func receive(_ text: String, id: UUID) {
        guard generation == id, requestSourceRevision == sourceRevision else { return }; preview = text
    }
    public mutating func finish(_ text: String, id: UUID, blocks: [String]? = nil) {
        guard generation == id, requestSourceRevision == sourceRevision else { return }
        result = blocks.map { $0.filter { !$0.isEmpty } } ?? TextBlocks.split(text); preview = text; generation = nil
    }
    public mutating func cancel() { generation = nil; requestSourceRevision = nil; preview = "" }
    public var pluginPending: [String] { retainedResults + (result ?? []) }
    public var pending: [String] { retainedResults + (result ?? blocks) }
    public mutating func invalidateResult() { cancel(); result = nil }
    public mutating func consumeSource() { edit("") }
    /// Once a translated block is inserted, retire its full source before another edit.
    public mutating func consumePlugin(all: Bool) {
        guard !generating else { return }
        if all { retainedResults = []; if result != nil { edit("") } else { cancel() }; return }
        if !retainedResults.isEmpty { retainedResults.removeFirst(); return }
        guard let blocks = result, !blocks.isEmpty else { return }
        retainedResults = Array(blocks.dropFirst()); edit("")
    }
    /// Called only after one explicit foreground proxy insertion. Never retries automatically.
    public mutating func consumed(all: Bool) {
        guard !generating else { return }
        if all { retainedResults = []; edit(""); return }
        if result != nil || !retainedResults.isEmpty { consumePlugin(all: all) }
        else if all { edit("") }
        else if let first = blocks.first {
            // Keep the remaining blocks exactly as they were.
            let shift = first.count
            replace(String(source.dropFirst(shift)), cursor: max(0, cursor - shift), boundaries: boundaries.map { $0 - shift })
        }
    }
}
