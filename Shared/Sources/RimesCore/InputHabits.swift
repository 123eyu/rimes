import Foundation

public enum InputHabitsError: Error { case invalidConfiguration }

/// Explicit overrides only. Missing values inherit the scheme; existing custom
/// patches, unknown options and unrelated scheme choices are never rewritten.
public struct InputHabits: Codable, Equatable {
    public var version = 1
    public var schemes: [String: [String: Bool]] = [:]
    public init() {}
    public subscript(schema: String, option: String) -> Bool? {
        get { schemes[schema]?[option] }
        set {
            var values = schemes[schema] ?? [:]
            values[option] = newValue
            if values.isEmpty { schemes.removeValue(forKey: schema) } else { schemes[schema] = values }
        }
    }
}

public struct InputHabitOption: Codable, Equatable, Identifiable {
    public var name: String
    public var off: String
    public var on: String
    public var id: String { name }
    public init(name: String, off: String, on: String) { self.name = name; self.off = off; self.on = on }
}

public final class InputHabitsStore {
    public let file: URL
    public init(root: URL) { file = root.appendingPathComponent("input-habits-v1.json") }
    public func load() -> InputHabits {
        guard let data = try? Data(contentsOf: file), data.count <= 262_144,
              let value = try? JSONDecoder().decode(InputHabits.self, from: data), value.version == 1 else { return .init() }
        return value
    }
    /// Read/modify/write the latest snapshot so editing one field preserves
    /// changes to other schemes and unknown options made by a newer version.
    public func set(_ value: Bool?, schema: String, option: String) throws {
        guard !schema.isEmpty, schema.utf8.count <= 256, !option.isEmpty, option.utf8.count <= 96 else { throw InputHabitsError.invalidConfiguration }
        var next = InputHabits()
        if FileManager.default.fileExists(atPath: file.path) {
            let data = try Data(contentsOf: file)
            guard data.count <= 262_144,
                  let old = try? JSONDecoder().decode(InputHabits.self, from: data), old.version == 1 else {
                throw InputHabitsError.invalidConfiguration
            }
            next = old
        }
        next[schema, option] = value
        let data = try JSONEncoder().encode(next)
        guard data.count <= 262_144 else { throw CoreError.tooLarge }
        try FileManager.default.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
        try data.write(to: file, options: .atomic)
    }
}
