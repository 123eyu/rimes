import Foundation
import RimesCore

struct KeyboardAppearanceConfiguration: Codable, Equatable {
    var layout: OrdinaryKeyboardLayout = .qwerty
    /// Pre-build-28 skin choice, read only during migration to KeyboardThemeStore.
    var skin: KeyboardSkin = .system
    var directSwipeSymbols = false
    var portraitHeight = 100
    var landscapeHeight = 100
    var numericNineGrid = false
    var layoutOptionsRevision: UUID?
    var longPressSwipeSymbols = false
    var longPressSwipeSymbolsRevision: UUID?
    var revision: UUID?

    init(layout: OrdinaryKeyboardLayout = .qwerty, skin: KeyboardSkin = .system,
         longPressSwipeSymbols: Bool = false, revision: UUID? = nil) {
        self.layout = layout; self.skin = skin
        self.longPressSwipeSymbols = longPressSwipeSymbols; self.revision = revision
    }
    private enum CodingKeys: String, CodingKey { case layout, skin, longPressSwipeSymbols, longPressSwipeSymbolsRevision, revision, directSwipeSymbols, portraitHeight, landscapeHeight, numericNineGrid, layoutOptionsRevision }
    init(from decoder: Decoder) throws {
        let values = try decoder.container(keyedBy: CodingKeys.self)
        layout = try values.decodeIfPresent(OrdinaryKeyboardLayout.self, forKey: .layout) ?? .qwerty
        skin = try values.decodeIfPresent(KeyboardSkin.self, forKey: .skin) ?? .system
        longPressSwipeSymbols = try values.decodeIfPresent(Bool.self, forKey: .longPressSwipeSymbols) ?? false
        longPressSwipeSymbolsRevision = try values.decodeIfPresent(UUID.self, forKey: .longPressSwipeSymbolsRevision)
        directSwipeSymbols = (try? values.decode(Bool.self, forKey: .directSwipeSymbols)) ?? false
        portraitHeight = min(130, max(100, (try? values.decode(Int.self, forKey: .portraitHeight)) ?? 100))
        landscapeHeight = min(130, max(100, (try? values.decode(Int.self, forKey: .landscapeHeight)) ?? 100))
        numericNineGrid = (try? values.decode(Bool.self, forKey: .numericNineGrid)) ?? false
        layoutOptionsRevision = try? values.decode(UUID.self, forKey: .layoutOptionsRevision)
        revision = try values.decodeIfPresent(UUID.self, forKey: .revision)
    }
}

/// Ordinary layout preferences, independent of chord mappings, imported schemes and pet themes.
final class KeyboardAppearanceStore {
    private let file: URL
    init(root: URL? = nil) {
        let root = root ?? FileManager.default.containerURL(forSecurityApplicationGroupIdentifier: ConfigurationStore.groupID)
            ?? FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        file = root.appendingPathComponent("keyboard-appearance-v1.json")
    }
    func load() -> KeyboardAppearanceConfiguration {
        guard let data = try? Data(contentsOf: file), data.count < 4096,
              let value = try? JSONDecoder().decode(KeyboardAppearanceConfiguration.self, from: data) else { return .init() }
        return value
    }
    func save(_ value: KeyboardAppearanceConfiguration) throws {
        var value = value; value.revision = UUID()
        // Layout edits do not overwrite an independently chosen gesture setting.
        let previous = load()
        value.longPressSwipeSymbols = previous.longPressSwipeSymbols
        value.longPressSwipeSymbolsRevision = previous.longPressSwipeSymbolsRevision
        value.directSwipeSymbols = previous.directSwipeSymbols
        value.portraitHeight = previous.portraitHeight; value.landscapeHeight = previous.landscapeHeight
        value.numericNineGrid = previous.numericNineGrid; value.layoutOptionsRevision = previous.layoutOptionsRevision
        try write(value)
    }
    func saveSwipeSymbols(_ enabled: Bool, direct: Bool = false) throws {
        var value = load()
        value.directSwipeSymbols = enabled && direct
        value.longPressSwipeSymbols = enabled; value.longPressSwipeSymbolsRevision = UUID()
        try write(value)
    }
    func saveLayoutOptions(portrait: Int, landscape: Int, numericNineGrid: Bool) throws {
        var value = load()
        value.portraitHeight = min(130, max(100, portrait)); value.landscapeHeight = min(130, max(100, landscape))
        value.numericNineGrid = numericNineGrid; value.layoutOptionsRevision = UUID()
        try write(value)
    }
    private func write(_ value: KeyboardAppearanceConfiguration) throws {
        try FileManager.default.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
        try JSONEncoder().encode(value).write(to: file, options: [.atomic, .completeFileProtectionUntilFirstUserAuthentication])
    }
}
