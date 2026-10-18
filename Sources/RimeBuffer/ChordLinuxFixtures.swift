import Foundation
import CryptoKit

/// Export the current macOS keymap compiler and pure rules. The fixture uses
/// only checked-in public schemas, never the user's applied map or preferences.
enum ChordLinuxFixtures {
    static func value() throws -> [String: Any] {
        let root = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent()
        let schemaURL = root.appendingPathComponent("rime-data/my_combo.schema.yaml")
        let builtin = try ChordKeymapProfile.builtIn(from: FlyChordSchemaParser.load(from: schemaURL)).validated()
        func object<T: Encodable>(_ value: T) throws -> Any {
            try JSONSerialization.jsonObject(with: JSONEncoder().encode(value))
        }
        var explicit = ChordKeymapProfile(
            id: "00000000-0000-4000-8000-000000000001", name: "公开并击样例",
            leftKeys: "abcdq", rightKeys: "efgkm,.",
            mappings: [.init(keys: "ab", output: "ch", kind: .fragment),
                       .init(keys: "cd", output: "ni", kind: .syllable),
                       .init(keys: "ae", output: "hao", kind: .syllable),
                       .init(keys: "ac", output: "n", kind: .fragment),
                       .init(keys: "ef", output: "ong", kind: .fragment),
                       .init(keys: "acef", output: "nong", kind: .syllable),
                       .init(keys: "qkm", output: "qiong", kind: .syllable),
                       .init(keys: "b,", output: "lve", kind: .syllable)])
        explicit = try explicit.validated()
        var ziranma = explicit
        ziranma.outputEncoding = .ziranma
        var copy = builtin
        copy.id = "00000000-0000-4000-8000-000000000002"
        copy.name = "默认并击副本"
        let profiles = [builtin, explicit, ziranma, copy]
        let compilers: [[String: Any]] = try profiles.map { profile in
            ["profile": try object(profile), "schemaID": profile.schemaID,
             "algebra": try ChordKeymapCompiler.algebraRules(for: profile),
             "yaml": try ChordKeymapCompiler.schemaYAML(for: profile)]
        }
        var templates: [String: String] = [:]
        for profile in [explicit, ziranma] {
            func scalar(_ text: String) -> String { "'" + text.replacingOccurrences(of: "'", with: "''") + "'" }
            let algebra = try ChordKeymapCompiler.algebraRules(for: profile)
                .map { "    - " + scalar($0) }.joined(separator: "\n")
            templates[profile.outputEncoding.rawValue] = try ChordKeymapCompiler.schemaYAML(for: profile)
                .replacingOccurrences(of: "schema_id: " + profile.schemaID, with: "schema_id: @SCHEMA_ID@")
                .replacingOccurrences(of: "name: " + scalar(profile.name), with: "name: @NAME@")
                .replacingOccurrences(of: "alphabet: " + scalar(profile.alphabet), with: "alphabet: @ALPHABET@")
                .replacingOccurrences(of: algebra, with: "@ALGEBRA@")
        }
        let fragmentInputs = ZiranmaShuangpin.initials + [
            "a", "o", "e", "i", "u", "v", "ai", "ei", "ao", "ou", "an", "en", "ang", "eng",
            "ong", "ia", "ie", "iao", "iu", "ian", "in", "iang", "ing", "iong", "ua", "uo",
            "uai", "ui", "uan", "un", "uang", "ue", "ve", "van", "vn"]
        let syllables = Dictionary(uniqueKeysWithValues: ZiranmaShuangpin.syllables.sorted().map {
            ($0, ZiranmaShuangpin.syllableCode($0)!)
        })
        let fragments = Dictionary(uniqueKeysWithValues: Set(fragmentInputs).sorted().compactMap { input in
            ZiranmaShuangpin.fragmentCode(input).map { (input, $0) }
        })
        var rules: [[String: Any]] = []
        for (index, profile) in profiles.enumerated() {
            for keys in ["q", "y", "qy", "qkm", "dv", "dvi", "cd", "ac", "ef", "acef", "b,", ",."] {
                let codes = keys.unicodeScalars.map { Int32($0.value) }
                rules.append(["profileIndex": index, "keys": keys,
                              "canonical": profile.canonicalKeys(keys),
                              "insertBoundary": ChordKeymapBoundaryRules.shouldInsert(keys: codes, profile: profile),
                              "mayCombine": ChordKeymapBoundaryRules.mayCombine(keys: codes, profile: profile),
                              "mayAwaitComplement": ChordKeymapBoundaryRules.mayAwaitComplement(keys: codes, profile: profile)])
            }
        }
        var invalids: [[String: Any]] = []
        let base = try object(explicit) as! [String: Any]
        let changes: [(String, String, Any)] = [
            ("version", "formatVersion", 2), ("id", "id", "../escape"),
            ("empty-name", "name", "  "), ("control-name", "name", "a\nb"),
            ("long-name", "name", String(repeating: "中", count: 81)),
            ("overlap", "rightKeys", "ab"), ("unknown-key", "rightKeys", "1"),
            ("native-injection", "nativeSchemeID", "rime_ice"),
            ("null-boundary", "boundaryPolicy", NSNull()),
            ("null-encoding", "outputEncoding", NSNull()),
            ("empty-draft", "mappings", []),
            ("singleton", "mappings", [["keys": "a", "output": "ni", "kind": "syllable"]]),
            ("duplicates", "mappings", [["keys": "ab", "output": "ni", "kind": "syllable"], ["keys": "ba", "output": "hao", "kind": "syllable"]]),
            ("bad-output", "mappings", [["keys": "ab", "output": "Ni", "kind": "syllable"]]),
            ("bad-kind", "mappings", [["keys": "ab", "output": "ni", "kind": "other"]])]
        for (name, key, value) in changes {
            var changed = base; changed[key] = value
            let data = try JSONSerialization.data(withJSONObject: changed)
            let decoded = try? JSONDecoder().decode(ChordKeymapProfile.self, from: data)
            invalids.append(["name": name, "profile": changed,
                             "draftAccepted": decoded.flatMap { try? $0.validated(requireMappings: false) } != nil,
                             "applyAccepted": decoded.flatMap { try? $0.validated() } != nil])
        }
        let learning: [[String: Any]] = try [builtin, explicit, ziranma].map { profile in
            let schema = profile.isBuiltIn ? try FlyChordSchemaParser.load(from: schemaURL)
                : try FlyChordSchemaParser.loadActive(profile: profile)
            let curriculum = FlyChordCurriculum(schema: schema)
            let courses: [[String: Any]] = curriculum.courses.map { course in
                let samples = FlyChordExerciseSampler.sample(from: course, limit: 30, seed: 42)
                return ["id": course.id, "title": course.title, "keyCount": course.keyCount,
                        "mappings": course.mappings.map { ["id": $0.id, "keys": $0.chord, "output": $0.output] },
                        "sample42": samples.map { $0.mappingID }]
            }
            return ["profile": try object(profile), "courses": courses]
        }
        let contribution = BuiltInPlugins.makeAll().first {
            $0.descriptor.key.rawID == BuiltInPluginID.flyChordLearning
        }!.descriptor.settings!
        let route: [String: Any] = ["id": "extension." + contribution.id,
                                   "title": contribution.title, "icon": contribution.symbolName,
                                   "subpages": contribution.subpages.map { [$0.id, $0.title] }]
        let sources = ["Sources/RimeBuffer/BuiltInPlugins.swift", "OfficialPlugins/native/macos/ChordKeymap.swift",
                       "OfficialPlugins/native/macos/ChordController.swift",
                       "OfficialPlugins/native/macos/ChordExtension.swift",
                       "OfficialPlugins/native/macos/ChordOutputEncoding.swift",
                       "OfficialPlugins/native/macos/NativeChordScheme.swift",
                       "OfficialPlugins/native/macos/FlyChordLearning.swift",
                       "OfficialPlugins/native/macos/FlyChordLearningSettingsViewController.swift",
                       "OfficialPlugins/native/macos/ChordKeymapEditorViewController.swift",
                       "OfficialPlugins/native/macos/ChordKeymapActivation.swift",
                       "Sources/RimeBuffer/ChordLinuxFixtures.swift", "Sources/RimeBuffer/YoyoEngineSmoke.swift",
                       "rime-data/my_combo.schema.yaml"]
        let hashes = try Dictionary(uniqueKeysWithValues: sources.map { name in
            (name, SHA256.hash(data: try Data(contentsOf: root.appendingPathComponent(name)))
                .map { String(format: "%02x", $0) }.joined())
        })
        return ["schemaVersion": 1, "purpose": "public-chord-contract", "sources": hashes,
                "defaultDuration": ChordSettings.defaultDuration,
                "durationRange": [ChordSettings.range.lowerBound, ChordSettings.range.upperBound],
                "builtin": try object(builtin), "nativeProfiles": try object(NativeChordSchemeCatalog.all),
                "nativeTraces": try YoyoEngineFixtures.cases.map { profile, traces in
                    ["profile": try object(profile), "traces": try object(traces)]
                },
                "syllables": syllables, "fragments": fragments, "templates": templates,
                "compilers": compilers, "rules": rules, "validation": invalids, "learning": learning,
                "route": route]
    }
}
