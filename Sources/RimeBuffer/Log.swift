import Foundation

/// Minimal file logger at ~/Library/Logs/RIMES/rimebuffer.log so behaviour
/// can be inspected without seeing the UI. Cheap; fine to leave on during
/// bring-up. `RIMES` is the compatibility directory name, kept across the
/// XSIME rename like ~/Library/RIMES/.
///
/// Privacy rule: NEVER log user-entered or committed text. Wrap any such value
/// in `IMELog.redact(_:)`, which records only its length. CI scans complete
/// multiline calls (including raw strings) for quoted or direct interpolation
/// of text-like values; counts/ids stay visible and content goes through
/// `redact`.
enum IMELog {
    static let directory = URL(fileURLWithPath: NSHomeDirectory())
        .appendingPathComponent("Library/Logs/RIMES", isDirectory: true)
    static let url = directory.appendingPathComponent("rimebuffer.log")
    /// 1.2.0 and earlier wrote to the home directory root.
    private static let legacyURL = URL(fileURLWithPath: NSHomeDirectory())
        .appendingPathComponent("rimebuffer.log")
    /// One process-wide writer keeps overlapping provider/controller queues
    /// from racing through independent seek-to-end operations. Ordinary writes
    /// are asynchronous so diagnostics never delay an IMK key callback or a
    /// streaming model delta.
    private static let writerQueue = DispatchQueue(
        label: "RimeBuffer.IMELog.writer",
        qos: .utility
    )

    /// Create the directory 0700 and the log 0600 (owner-only), tighten
    /// existing entries in place, and remove the legacy home-root log so no
    /// diagnostics linger outside ~/Library/Logs. Runs once, lazily and
    /// thread-safely.
    private static let prepared: Bool = {
        let fm = FileManager.default
        try? fm.createDirectory(at: directory, withIntermediateDirectories: true,
                                attributes: [.posixPermissions: 0o700])
        try? fm.setAttributes([.posixPermissions: 0o700], ofItemAtPath: directory.path)
        try? fm.removeItem(at: legacyURL)
        if fm.fileExists(atPath: url.path) {
            try? fm.setAttributes([.posixPermissions: 0o600], ofItemAtPath: url.path)
        } else {
            fm.createFile(atPath: url.path, contents: nil,
                          attributes: [.posixPermissions: 0o600])
        }
        return true
    }()

    /// Redacted stand-in for user text: length only, never the content itself.
    /// A hash would be reversible for the short strings an IME handles (a single
    /// character brute-forces trivially), so length is the safe representation.
    static func redact(_ text: String) -> String { "⟨\(text.count)⟩" }

    static func write(_ message: String) {
        let line = "\(Date().timeIntervalSince1970) \(message)\n"
        guard let data = line.data(using: .utf8) else { return }
        writerQueue.async {
            _ = prepared
            if let handle = try? FileHandle(forWritingTo: url) {
                handle.seekToEndOfFile()
                handle.write(data)
                try? handle.close()
            } else {
                try? data.write(to: url)
            }
        }
    }

    static func reset(_ header: String) {
        // Reset runs once during launch. Make it synchronous so every later
        // asynchronous write is ordered strictly after the new header.
        writerQueue.sync {
            _ = prepared
            try? "\(Date().timeIntervalSince1970) \(header)\n"
                .data(using: .utf8)?
                .write(to: url)
        }
    }
}
