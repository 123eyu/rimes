import AppKit
import CryptoKit
import Foundation

/// Public, deterministic observations through the actual macOS stores. Linux
/// replays these values; the export never opens the user's statistics files.
enum StatisticsLinuxFixtures {
    static func export(to destination: URL) throws {
        let manager = FileManager.default
        let root = manager.temporaryDirectory.appendingPathComponent("statistics-linux-\(UUID().uuidString)")
        try manager.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? manager.removeItem(at: root) }
        var calendar = Calendar(identifier: .gregorian)
        calendar.timeZone = .current
        let date = calendar.date(from: DateComponents(year: 2026, month: 7, day: 18, hour: 23, minute: 58))!
        let base = date.timeIntervalSince1970
        func key(_ id: String, _ offset: Double, repeatKey: Bool = false, shortcut: Bool = false) -> [String: Any] {
            ["kind": "key", "key": id, "timestamp": base + offset,
             "repeat": repeatKey, "shortcut": shortcut]
        }
        func commit(_ count: Int, _ offset: Double) -> [String: Any] {
            ["kind": "commit", "characters": count, "timestamp": base + offset]
        }
        func chord(_ offset: Double, duration: Double = 0.08, releases: Int = 3) -> [String: Any] {
            ["kind": "chord", "timestamp": base + offset,
             "chordDuration": duration, "handledReleases": releases]
        }
        let sequences: [(String, [[String: Any]])] = [
            ("timing-and-admission", [key("KeyA", 0), commit(4, 5), chord(10),
                key("KeyA", 11, repeatKey: true), key("KeyC", 12, shortcut: true), key("ArrowLeft", 13),
                commit(-1, 14), chord(15, duration: -1), chord(16, releases: -1),
                key("KeyB", 21), key("KeyC", 20), commit(2, 21),
                key("KeyD", 119), commit(3, 122)]),
            ("boundaries-restart-and-consent", [key("KeyA", 0), commit(2, 2),
                ["kind": "boundary"], commit(3, 4), ["kind": "restart"], commit(1, 6),
                ["kind": "preferences", "keys": false, "activity": true], key("KeyB", 7),
                ["kind": "preferences", "keys": true, "activity": false], key("KeyC", 8), commit(5, 9),
                ["kind": "preferences", "keys": false, "activity": false], key("KeyD", 10),
                ["kind": "preferences", "keys": true, "activity": true], key("KeyE", 11), commit(2, 12)]),
            ("clearing-and-ties", [key("KeyZ", 0), key("KeyA", 1), key("KeyA", 2), key("KeyZ", 3),
                ["kind": "clear-day", "day": "2026-07-18"], key("KeyZ", 4), commit(2, 5),
                ["kind": "clear-activity"], key("KeyA", 6), commit(3, 7)])
        ]
        func read(_ path: URL) throws -> [String: Any] {
            var value = try JSONSerialization.jsonObject(with: Data(contentsOf: path)) as! [String: Any]
            value.removeValue(forKey: "updatedAt")
            return value
        }
        func replay(_ id: String, _ sequence: [[String: Any]]) throws -> [String: Any] {
            let directory = root.appendingPathComponent(id)
            var keys = KeyFrequencyStore(storageRoot: directory, autosaveDelay: 3600)
            var activity = TypingSpeedStore(storageRoot: directory, autosaveDelay: 3600)
            var collectKeys = true, collectActivity = true
            var events: [[String: Any]] = []
            for var value in sequence {
                let kind = value["kind"] as! String
                switch kind {
                case "boundary": activity.endCurrentSession()
                case "restart":
                    keys.saveNow(); activity.saveNow()
                    keys = KeyFrequencyStore(storageRoot: directory, autosaveDelay: 3600)
                    activity = TypingSpeedStore(storageRoot: directory, autosaveDelay: 3600)
                case "preferences":
                    collectKeys = value["keys"] as! Bool; collectActivity = value["activity"] as! Bool
                    activity.endCurrentSession()
                case "clear-day":
                    let formatter = DateFormatter(); formatter.dateFormat = "yyyy-MM-dd"
                    formatter.calendar = calendar; formatter.locale = Locale(identifier: "en_US_POSIX")
                    keys.clear(day: formatter.date(from: value["day"] as! String)!)
                case "clear-activity": activity.clearAll()
                default:
                    let timestamp = value["timestamp"] as! Double
                    value["day"] = keys.dayKey(for: Date(timeIntervalSince1970: timestamp))
                    let event: InputTelemetryEvent
                    if kind == "key" {
                        let id = value["key"] as! String
                        if collectKeys { keys.record(keyID: id, at: Date(timeIntervalSince1970: timestamp)) }
                        event = .key(.init(keyID: id, timestamp: timestamp,
                            isRepeat: value["repeat"] as! Bool,
                            modifierFlags: value["shortcut"] as! Bool ? UInt(1) << 20 : 0,
                            schemaID: "public-fixture"))
                    } else if kind == "commit" {
                        event = .commit(.init(characterCount: value["characters"] as! Int,
                            timestamp: timestamp, source: .direct, schemaID: "public-fixture"))
                    } else {
                        event = .chord(.init(rimeKeyCodes: [113, 107, 109], timestamp: timestamp,
                            duration: value["chordDuration"] as! Double,
                            handledReleaseCount: value["handledReleases"] as! Int, schemaID: "public-fixture"))
                    }
                    if collectActivity { activity.consume(event) }
                }
                events.append(value)
            }
            keys.saveNow(); activity.saveNow()
            guard keys.storageState == .ready, activity.storageIssue == nil else {
                throw NSError(domain: "StatisticsLinuxFixtures", code: 1)
            }
            let keyFile = try read(directory.appendingPathComponent("stats/key_frequency.json"))
            let activityFile = try read(directory.appendingPathComponent("stats/typing_speed.json"))
            let snapshots: [[String: Any]] = keys.historySnapshot().days.map { day in
                let value = keys.snapshot(dayKey: day.dayKey)
                return ["dayKey": day.dayKey, "counts": value.counts, "total": value.total,
                        "maxCount": value.maxCount, "topKeyId": value.topKeyId as Any? ?? NSNull()]
            }
            return ["id": id, "events": events, "keys": keyFile, "activity": activityFile,
                    "snapshots": snapshots]
        }
        let keyboard: [[String: Any]] = KeyboardLayout.macANSI.keys.map { key in
            ["id": key.keyId, "label": key.label,
             "x": key.frame.minX, "y": key.frame.minY,
             "width": key.frame.width, "height": key.frame.height]
        }
        let repo = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .deletingLastPathComponent().deletingLastPathComponent()
        let sources = ["OfficialPlugins/native/macos/KeyFrequencyStore.swift",
                       "OfficialPlugins/native/macos/TypingSpeedStore.swift",
                       "OfficialPlugins/native/macos/DailyInputMetrics.swift",
                       "OfficialPlugins/native/macos/StatisticsSettingsViewController.swift",
                       "Sources/RimeBuffer/InputTelemetry.swift", "Sources/RimeBuffer/TypingPracticeTelemetry.swift",
                       "Sources/RimeBuffer/MetricsChartViews.swift", "Sources/RimeBuffer/StatisticsLinuxFixtures.swift",
                       "Sources/RimeBuffer/KeyboardHeatmapView.swift", "Sources/RimeBuffer/YearHistoryHeatmapView.swift",
                       "Sources/RimeBuffer/StatisticsDashboardSmoke.swift"]
        let hashes = try Dictionary(uniqueKeysWithValues: sources.map { name in
            (name, SHA256.hash(data: try Data(contentsOf: repo.appendingPathComponent(name)))
                .map { String(format: "%02x", $0) }.joined())
        })
        let result: [String: Any] = ["schemaVersion": 1, "purpose": "public-statistics-contract",
            "sources": hashes, "timezone": TimeZone.current.identifier,
            "cases": try sequences.map { try replay($0.0, $0.1) }, "keyboard": keyboard]
        try JSONSerialization.data(withJSONObject: result, options: [.prettyPrinted, .sortedKeys])
            .write(to: destination, options: .atomic)
    }
}
