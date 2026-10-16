import Foundation

/// LevelDB can reject a user dictionary while librime remains healthy enough
/// to return base-dictionary candidates. Its own LOG is the only diagnostic in
/// that case. Read bounded tails off the input thread; never repair or replace
/// a learning database implicitly.
final class UserDictionaryHealth {
    static let shared = UserDictionaryHealth()
    private let queue = DispatchQueue(label: "rimes.user-dictionary-health", qos: .utility)
    private var roots = Set<URL>()
    private var timer: DispatchSourceTimer?
    private var lastFailures: [String] = []
    // Published and read only on the main thread.
    private(set) var warning: String?

    func watch(directory: String) {
        let root = URL(fileURLWithPath: directory, isDirectory: true).standardizedFileURL
        queue.async { [self] in
            roots.insert(root)
            if timer == nil {
                let source = DispatchSource.makeTimerSource(queue: queue)
                source.schedule(deadline: .now(), repeating: .seconds(5))
                source.setEventHandler { [weak self] in self?.scan() }
                source.resume()
                timer = source
            }
        }
    }

    private func scan() {
        let failures = roots.flatMap { Self.failedDictionaries(in: $0) }.sorted()
        guard failures != lastFailures else { return }
        lastFailures = failures
        // Do not log raw LevelDB lines, dictionary names, or paths: external
        // tools may put user content into those fields.
        let message = failures.isEmpty ? nil
            : "检测到用户学习词库读写异常，部分学习词可能不可用。请先备份词库，再用兼容的 Rime 工具导出文本快照并重新导入。"
        DispatchQueue.main.async { [weak self] in
            self?.warning = message
            IMELog.write(failures.isEmpty
                ? "user dictionary diagnostic cleared"
                : "WARNING: user dictionary storage error count=\(failures.count); see dictionary settings")
            NotificationCenter.default.post(name: .rimeUserDictionaryHealthChanged, object: nil)
        }
    }

    static func containsStorageError(_ text: String) -> Bool {
        let text = text.lowercased()
        return ["compaction error:", "corruption:", "io error:",
                "corrupted compressed block", "unsupported compression"].contains {
            text.contains($0)
        }
    }

    static func failedDictionaries(in root: URL) -> [String] {
        let fm = FileManager.default
        let keys: Set<URLResourceKey> = [.isDirectoryKey, .isSymbolicLinkKey, .isRegularFileKey]
        guard let entries = try? fm.contentsOfDirectory(at: root,
            includingPropertiesForKeys: Array(keys), options: [.skipsHiddenFiles]) else { return [] }
        return entries.filter { $0.pathExtension == "userdb" }.compactMap { directory in
            guard let values = try? directory.resourceValues(forKeys: keys),
                  values.isDirectory == true, values.isSymbolicLink != true else { return nil }
            let log = directory.appendingPathComponent("LOG")
            guard let values = try? log.resourceValues(forKeys: keys),
                  values.isRegularFile == true, values.isSymbolicLink != true,
                  let file = try? FileHandle(forReadingFrom: log) else { return nil }
            defer { try? file.close() }
            do {
                let size = try file.seekToEnd()
                try file.seek(toOffset: size > 65_536 ? size - 65_536 : 0)
                let data = try file.read(upToCount: 65_536) ?? Data()
                return containsStorageError(String(decoding: data, as: UTF8.self))
                    ? directory.lastPathComponent : nil
            } catch { return nil }
        }
    }
}

extension Notification.Name {
    static let rimeUserDictionaryHealthChanged = Notification.Name("RIMES.userDictionaryHealthChanged")
}
