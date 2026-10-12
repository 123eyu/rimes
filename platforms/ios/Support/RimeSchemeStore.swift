import Foundation
import Darwin

struct ImportedRimeSchema: Codable, Identifiable, Hashable, Sendable {
    var id: String
    var name: String
}

struct RimeSchemePackage: Codable, Identifiable, Sendable {
    var id: String
    var name: String
    var schemas: [ImportedRimeSchema]
    var importedAt: Date
    var sourceDigest: String
    var warnings: [String]
}

struct RimeSchemeSelection: Codable, Equatable, Sendable {
    var packageID: String
    var schemaID: String
}

/// A layout switches between the package's own encoders, never a built-in dictionary.
struct ImportedRimeKeyboardLayouts: Equatable {
    var qwertySchemaID: String?
    var nineKeySchemaID: String?
}

struct RimeSchemeLibrary: Codable, Sendable {
    var packages: [RimeSchemePackage] = []
    var active: RimeSchemeSelection?
    var revision: String = UUID().uuidString
}

/// Local menu choices survive keyboard presentations until the main app makes
/// another explicit selection. A missing choice differs from choosing built-in.
struct KeyboardRimeSchemeChoice: Codable {
    var appRevision: String
    var selection: RimeSchemeSelection?
    var englishInput: Bool = false
}

enum RimeSchemeError: LocalizedError {
    case invalid(String)
    var errorDescription: String? {
        switch self { case .invalid(let message): return message }
    }
}

/// Only the containing app publishes packages and requests a default selection.
/// The extension keeps its own selection override and never needs group write access.
final class RimeSchemeStore: @unchecked Sendable {
    let root: URL
    var stagingRoot: URL { root.appendingPathComponent(".staging", isDirectory: true) }
    private var file: URL { root.appendingPathComponent("library-v1.json") }
    private let writeLibraryData: (Data, URL) throws -> Void
    private var keyboardSchemaInfo: [String: [String: Any]] = [:]
    private let keyboardSchemaLock = NSLock()
    init(root: URL? = nil, writeLibraryData: @escaping (Data, URL) throws -> Void = { data, file in
        try data.write(to: file, options: [.atomic, .completeFileProtectionUntilFirstUserAuthentication])
    }) {
        let group = FileManager.default.containerURL(forSecurityApplicationGroupIdentifier: ConfigurationStore.groupID)
            ?? FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        self.root = root ?? group.appendingPathComponent("Library/Application Support/RimeSchemes", isDirectory: true)
        self.writeLibraryData = writeLibraryData
    }
    func packageURL(id: String) -> URL {
        root.appendingPathComponent("packages", isDirectory: true).appendingPathComponent(id, isDirectory: true)
    }
    func load() -> RimeSchemeLibrary {
        guard let data = try? Data(contentsOf: file), data.count <= 1024 * 1024,
              var library = try? JSONDecoder().decode(RimeSchemeLibrary.self, from: data),
              library.packages.count <= 32 else { return .init() }
        library.packages = library.packages.filter(Self.valid)
        if let active = library.active, !library.packages.contains(where: { $0.id == active.packageID && $0.schemas.contains(where: { $0.id == active.schemaID }) }) { library.active = nil }
        return library
    }
    func resolve(_ selection: RimeSchemeSelection) -> (package: RimeSchemePackage, schema: ImportedRimeSchema, resources: URL)? {
        guard let package = load().packages.first(where: { $0.id == selection.packageID }),
              let schema = package.schemas.first(where: { $0.id == selection.schemaID }) else { return nil }
        let url = packageURL(id: package.id)
        guard Self.isCompiledFile(url.appendingPathComponent("build/\(schema.id).schema.yaml")) else { return nil }
        return (package, schema, url)
    }
    func keyboardLayouts(for selection: RimeSchemeSelection) -> ImportedRimeKeyboardLayouts? {
        guard let resolved = resolve(selection) else { return nil }
        let resources = resolved.resources
        func info(_ id: String) -> [String: Any]? {
            guard Self.safeSchemaID(id) else { return nil }
            let file = resources.appendingPathComponent("build/\(id).schema.yaml")
            guard Self.isCompiledFile(file) else { return nil }
            keyboardSchemaLock.lock()
            defer { keyboardSchemaLock.unlock() }
            if let cached = keyboardSchemaInfo[file.path] { return cached }
            let value = RimeMobile.keyboardSchemaInfo(atPath: file.path) as? [String: Any] ?? [:]
            guard value["schemaID"] as? String == id else { return nil }
            keyboardSchemaInfo[file.path] = value
            return value
        }
        guard let current = info(selection.schemaID) else { return nil }
        let isNineKey = current["nineKey"] as? Bool == true
        let baseID = isNineKey && selection.schemaID.hasSuffix("_t9")
            ? String(selection.schemaID.dropLast(3)) : selection.schemaID
        let nineID = isNineKey ? selection.schemaID : selection.schemaID + "_t9"
        let base = info(baseID), nine = info(nineID)
        // The native T9 sibling can be a compiled dependency rather than a
        // selectable schema. Both must use the same imported dictionary.
        let paired = base?["script"] as? Bool == true && base?["nineKey"] as? Bool == false
            && nine?["nineKey"] as? Bool == true
            && !(base?["dictionary"] as? String ?? "").isEmpty
            && base?["dictionary"] as? String == nine?["dictionary"] as? String
        return .init(qwertySchemaID: isNineKey ? (paired ? baseID : nil) : selection.schemaID,
                     nineKeySchemaID: isNineKey ? selection.schemaID : (paired ? nineID : nil))
    }
    /// Deployment is complete before this move. A partial directory is never selected.
    func publish(stagedURL: URL, package: RimeSchemePackage) throws {
        guard Self.valid(package), stagedURL.deletingLastPathComponent().standardizedFileURL == stagingRoot.standardizedFileURL,
              stagedURL.lastPathComponent == package.id else { throw RimeSchemeError.invalid("方案暂存目录无效。") }
        for schema in package.schemas {
            guard Self.isCompiledFile(stagedURL.appendingPathComponent("build/\(schema.id).schema.yaml")) else {
                throw RimeSchemeError.invalid("方案尚未部署完成：\(schema.name)")
            }
        }
        try withWriteLock {
            var library = load()
            guard library.packages.count < 32, !library.packages.contains(where: { $0.id == package.id }) else { throw RimeSchemeError.invalid("已安装的方案包数量达到上限。") }
            let target = packageURL(id: package.id)
            try FileManager.default.createDirectory(at: target.deletingLastPathComponent(), withIntermediateDirectories: true)
            try FileManager.default.moveItem(at: stagedURL, to: target)
            do {
                library.packages.append(package)
                // Installing is not activating: preserve the current selection and revision.
                try write(library)
            } catch { try? FileManager.default.removeItem(at: target); throw error }
        }
    }
    func activate(_ selection: RimeSchemeSelection?) throws {
        try withWriteLock {
            if let selection, resolve(selection) == nil { throw RimeSchemeError.invalid("找不到已部署的输入方案，请重新导入。") }
            var library = load(); library.active = selection; library.revision = UUID().uuidString
            try write(library)
        }
    }
    /// Remove only the imported resources. Learned words live in the extension's
    /// separate RimeImported directory and are never part of this transaction.
    func remove(packageID: String) throws {
        guard UUID(uuidString: packageID) != nil else { throw RimeSchemeError.invalid("方案包标识无效。") }
        try withWriteLock {
            var library = load()
            guard library.packages.contains(where: { $0.id == packageID }) else { throw RimeSchemeError.invalid("找不到要删除的方案包，请刷新列表。") }
            let target = packageURL(id: packageID), fm = FileManager.default
            let parent = target.deletingLastPathComponent()
            if fm.fileExists(atPath: parent.path) {
                let values = try parent.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
                guard values.isDirectory == true, values.isSymbolicLink != true else { throw RimeSchemeError.invalid("方案目录异常，无法删除。") }
            }
            let removed = root.appendingPathComponent(".removed-" + UUID().uuidString, isDirectory: true)
            // Moving a symbolic link moves the link itself, never its destination.
            let exists = (try? fm.attributesOfItem(atPath: target.path)) != nil
            if exists { try fm.moveItem(at: target, to: removed) }
            do {
                library.packages.removeAll { $0.id == packageID }
                if library.active?.packageID == packageID {
                    library.active = nil; library.revision = UUID().uuidString
                }
                try write(library)
            } catch {
                if exists { try fm.moveItem(at: removed, to: target) }
                throw error
            }
            // A cleanup failure leaves only an unselectable, private trash directory.
            if exists { try? fm.removeItem(at: removed) }
        }
    }
    static func safeSchemaID(_ id: String) -> Bool {
        !id.isEmpty && id.utf8.count <= 120 && id != "." && id != ".."
            && id.unicodeScalars.allSatisfy { CharacterSet(charactersIn: "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-").contains($0) }
    }
    private static func isCompiledFile(_ url: URL) -> Bool {
        guard let values = try? url.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey, .fileSizeKey]) else { return false }
        return values.isRegularFile == true && values.isSymbolicLink != true && (values.fileSize ?? 0) > 0
    }
    private static func valid(_ package: RimeSchemePackage) -> Bool {
        UUID(uuidString: package.id) != nil && !package.name.isEmpty && package.name.count <= 200
            && (1...64).contains(package.schemas.count) && Set(package.schemas.map(\.id)).count == package.schemas.count
            && package.schemas.allSatisfy { safeSchemaID($0.id) && !$0.name.isEmpty && $0.name.count <= 200 }
            && package.warnings.count <= 512 && package.warnings.allSatisfy { $0.count <= 4000 }
    }
    private func write(_ library: RimeSchemeLibrary) throws {
        let data = try JSONEncoder().encode(library)
        guard data.count <= 1024 * 1024 else { throw RimeSchemeError.invalid("方案目录过大。") }
        try writeLibraryData(data, file)
    }
    private func withWriteLock(_ action: () throws -> Void) throws {
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true, attributes: [.protectionKey: FileProtectionType.completeUntilFirstUserAuthentication])
        let fd = open(root.appendingPathComponent("library.lock").path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)
        guard fd >= 0 else { throw RimeSchemeError.invalid("无法锁定方案目录。") }
        defer { close(fd) }
        guard flock(fd, LOCK_EX) == 0 else { throw RimeSchemeError.invalid("方案目录正忙。") }
        defer { flock(fd, LOCK_UN) }
        try action()
    }
}
