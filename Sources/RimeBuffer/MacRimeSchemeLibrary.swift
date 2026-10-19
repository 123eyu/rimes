import Cocoa
import CRimeBridge

// The archive implementation is the same source file as iOS (see the symlink
// RimeSchemeImportService.swift). Platform code owns deployment and activation.
struct ImportedRimeSchema: Codable, Identifiable, Hashable, Sendable {
    var id: String
    var name: String
}

struct MacRimeSchemePackage: Codable, Identifiable {
    let id: String
    let name: String
    let schemas: [ImportedRimeSchema]
    let sourceDigest: String
    let warnings: [String]
}

struct MacRimeSchemeSelection: Codable {
    let packageID: String
    var schemaID: String
}

final class MacRimeSchemeLibrary {
    static let shared = MacRimeSchemeLibrary()
    let root: URL
    let runtimePackage: MacRimeSchemePackage?
    private(set) var runtimeSelection: MacRimeSchemeSelection?
    var stagingRoot: URL { root.appendingPathComponent(".staging", isDirectory: true) }
    var runtimeResources: URL? { runtimePackage.map { packageURL($0.id) } }
    var runtimeUserDirectory: URL? { runtimePackage.map { learningURL($0.id) } }

    init(root: URL = RimesPaths.userDirectory().appendingPathComponent("schemes", isDirectory: true), runtime: Bool = true) {
        self.root = root
        if runtime, ProcessInfo.processInfo.environment["RIMEBUFFER_USER_DIR"] == nil,
           let data = try? Data(contentsOf: root.appendingPathComponent("active.json")), data.count < 4096,
           let selection = try? JSONDecoder().decode(MacRimeSchemeSelection.self, from: data),
           UUID(uuidString: selection.packageID) != nil,
           let package = Self.readPackage(at: root.appendingPathComponent("packages/\(selection.packageID)")),
           package.schemas.contains(where: { $0.id == selection.schemaID }),
           Self.regularFile(root.appendingPathComponent("packages/\(package.id)/build/\(selection.schemaID).schema.yaml")) {
            runtimePackage = package; runtimeSelection = selection
        } else { runtimePackage = nil; runtimeSelection = nil }
    }
    func packageURL(_ id: String) -> URL { root.appendingPathComponent("packages/" + id, isDirectory: true) }
    func learningURL(_ id: String) -> URL { root.appendingPathComponent("learning/" + id, isDirectory: true) }
    func packages() -> [MacRimeSchemePackage] {
        let entries = (try? FileManager.default.contentsOfDirectory(at: root.appendingPathComponent("packages"), includingPropertiesForKeys: [.isSymbolicLinkKey])) ?? []
        return entries.compactMap(Self.readPackage).sorted { $0.name < $1.name }
    }
    private static func regularFile(_ url: URL) -> Bool {
        guard let info = try? url.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey, .fileSizeKey]) else { return false }
        return info.isRegularFile == true && info.isSymbolicLink != true && (info.fileSize ?? 0) > 0
    }
    private static func readPackage(at url: URL) -> MacRimeSchemePackage? {
        guard UUID(uuidString: url.lastPathComponent) != nil,
              (try? url.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink) == false,
              let data = try? Data(contentsOf: url.appendingPathComponent("package.json")), data.count <= 262144,
              let package = try? JSONDecoder().decode(MacRimeSchemePackage.self, from: data),
              package.id == url.lastPathComponent, (1...32).contains(package.schemas.count),
              package.schemas.allSatisfy({ $0.id.range(of: "^[A-Za-z0-9_][A-Za-z0-9_.-]{0,99}$", options: .regularExpression) != nil && !$0.id.contains("..") }) else { return nil }
        return package
    }
    func install(_ review: RimeSchemeImportReview, selected: [String]) throws -> MacRimeSchemePackage {
        guard packages().count < 32 else { throw RimeSchemeImportError.invalid("最多保留 32 个方案包，请先移除不用的方案。") }
        let staged = try RimeSchemeImportService.stage(review: review, selectedSchemaIDs: selected, destinationRoot: stagingRoot)
        var published = false
        defer { if !published { try? FileManager.default.removeItem(at: staged.rootURL) } }
        let process = Process()
        guard let executable = Bundle.main.executableURL else { throw RimeSchemeImportError.invalid("找不到输入法程序。") }
        let framework = ProcessInfo.processInfo.environment["RIMEBUFFER_FRAMEWORKS_DIR"] ?? Bundle.main.privateFrameworksPath ?? "/Library/Input Methods/Squirrel.app/Contents/Frameworks"
        process.executableURL = executable
        process.arguments = ["scheme-deploy", staged.rootURL.path, framework] + staged.deploymentSchemaIDs
        let log = staged.rootURL.appendingPathComponent("deployment.log")
        FileManager.default.createFile(atPath: log.path, contents: nil)
        let output = try FileHandle(forWritingTo: log)
        defer { try? output.close() }
        process.standardOutput = output; process.standardError = output
        try process.run()
        let deadline = Date().addingTimeInterval(600)
        while process.isRunning && Date() < deadline { Thread.sleep(forTimeInterval: 0.1) }
        if process.isRunning {
            process.terminate()
            if process.isRunning { kill(process.processIdentifier, SIGKILL) }
            process.waitUntilExit()
            throw RimeSchemeImportError.invalid("方案编译超过十分钟，已停止；当前方案未改变。")
        }
        process.waitUntilExit()
        let logSize = (try log.resourceValues(forKeys: [.fileSizeKey])).fileSize ?? 0
        guard logSize <= 8 * 1024 * 1024 else { throw RimeSchemeImportError.invalid("方案产生过多错误日志；原方案未改变。") }
        let diagnostic = (try? String(contentsOf: log, encoding: .utf8)) ?? ""
        let nativeError = diagnostic.components(separatedBy: "\n").first { line in
            let lower = line.lowercased()
            let lua = lower.contains("lua_gears") || lower.contains("types.cc:") || lower.contains("lua_translation")
            return (lua && (lower.contains("error") || lower.contains("failed"))) || lower.contains("component not found") || lower.contains("error creating ")
        }
        guard process.terminationStatus == 0, nativeError == nil else {
            throw RimeSchemeImportError.invalid("方案部署或加载失败；原输入环境未改变。请检查方案依赖和运行能力。" + (nativeError.map { "\n" + String($0.prefix(600)) } ?? ""))
        }
        var bytes: Int64 = 0
        guard let files = FileManager.default.enumerator(at: staged.rootURL, includingPropertiesForKeys: [.isSymbolicLinkKey, .isRegularFileKey, .fileSizeKey]) else { throw RimeSchemeImportError.invalid("无法检查编译结果。") }
        for case let file as URL in files {
            let info = try file.resourceValues(forKeys: [.isSymbolicLinkKey, .isRegularFileKey, .fileSizeKey])
            guard info.isSymbolicLink != true else { throw RimeSchemeImportError.invalid("编译结果包含链接：\(file.lastPathComponent)。") }
            if file.lastPathComponent.hasSuffix(".userdb") || file.lastPathComponent.hasSuffix(".userdb.txt") {
                files.skipDescendants()
                try FileManager.default.removeItem(at: file)
                continue
            }
            if info.isRegularFile == true { bytes += Int64(info.fileSize ?? 0) }
            guard bytes <= RimeSchemeImportService.maximumDeployedSize else { throw RimeSchemeImportError.invalid("编译结果超过 8 GiB。") }
        }
        for schema in staged.schemaIDs where !Self.regularFile(staged.rootURL.appendingPathComponent("build/\(schema).schema.yaml")) {
            throw RimeSchemeImportError.invalid("方案未生成可用缓存：\(schema)")
        }
        let package = MacRimeSchemePackage(id: staged.id, name: review.sourceName, schemas: staged.schemas,
            sourceDigest: staged.sourceDigest, warnings: staged.warnings)
        try JSONEncoder().encode(package).write(to: staged.rootURL.appendingPathComponent("package.json"), options: .atomic)
        let target = packageURL(package.id)
        try FileManager.default.createDirectory(at: target.deletingLastPathComponent(), withIntermediateDirectories: true)
        try FileManager.default.moveItem(at: staged.rootURL, to: target)
        published = true
        return package
    }
    func activate(_ package: MacRimeSchemePackage?, schema: String? = nil) throws {
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        let active = root.appendingPathComponent("active.json")
        guard let package, let schema else {
            if FileManager.default.fileExists(atPath: active.path) { try FileManager.default.removeItem(at: active) }
            return
        }
        guard let stored = Self.readPackage(at: packageURL(package.id)), stored.schemas.contains(where: { $0.id == schema }),
              Self.regularFile(packageURL(package.id).appendingPathComponent("build/\(schema).schema.yaml")) else { throw RimeSchemeImportError.invalid("方案未部署完成。") }
        let learning = learningURL(package.id)
        try FileManager.default.createDirectory(at: learning, withIntermediateDirectories: true)
        // Ready before restart: the first key must not compile a large dictionary.
        let build = learning.appendingPathComponent("build")
        if !FileManager.default.fileExists(atPath: build.path) {
            let temporary = learning.appendingPathComponent(".build-" + UUID().uuidString)
            defer { try? FileManager.default.removeItem(at: temporary) }
            try FileManager.default.copyItem(at: packageURL(package.id).appendingPathComponent("build"), to: temporary)
            try FileManager.default.moveItem(at: temporary, to: build)
        }
        guard Self.regularFile(build.appendingPathComponent("\(schema).schema.yaml")) else {
            throw RimeSchemeImportError.invalid("已部署缓存不完整，请重新导入该方案。")
        }
        try JSONEncoder().encode(MacRimeSchemeSelection(packageID: package.id, schemaID: schema)).write(to: active, options: .atomic)
    }
    func selectWithinRuntime(_ schema: String) -> Bool {
        guard let package = runtimePackage, package.schemas.contains(where: { $0.id == schema }) else { return false }
        do { try activate(package, schema: schema); runtimeSelection?.schemaID = schema; return true }
        catch { return false }
    }
    func remove(_ package: MacRimeSchemePackage) throws {
        guard package.id != runtimePackage?.id else { throw RimeSchemeImportError.invalid("请先恢复内置方案，再删除正在使用的方案包。") }
        guard Self.readPackage(at: packageURL(package.id)) != nil else { throw RimeSchemeImportError.invalid("找不到方案包。") }
        try FileManager.default.removeItem(at: packageURL(package.id))
        // Learned user dictionaries intentionally outlive installed source packages.
    }
    /// One-shot maintenance mode, never an IMK server or input-event companion.
    static func deploymentCommand(_ arguments: [String]) -> Int32? {
        guard arguments.count >= 2, arguments[1] == "scheme-deploy" else { return nil }
        guard arguments.count >= 5 else { return 2 }
        let root = URL(fileURLWithPath: arguments[2]).standardizedFileURL
        guard root.deletingLastPathComponent().lastPathComponent == ".staging", UUID(uuidString: root.lastPathComponent) != nil,
              (try? root.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink) == false,
              arguments.dropFirst(4).allSatisfy({ $0.range(of: "^[A-Za-z0-9_][A-Za-z0-9_.-]{0,99}$", options: .regularExpression) != nil && !$0.contains("..") }) else { return 2 }
        let logs = FileManager.default.temporaryDirectory.appendingPathComponent("rimes-deploy-log-" + UUID().uuidString)
        do { try FileManager.default.createDirectory(at: logs, withIntermediateDirectories: true) } catch { return 1 }
        defer { try? FileManager.default.removeItem(at: logs) }
        guard BBRimeStart(root.path, root.path, logs.path, arguments[3]) else { return 1 }
        for id in arguments.dropFirst(4) {
            guard BBRimeDeploySchemaFile(root.appendingPathComponent(id + ".schema.yaml").path) else { return 1 }
            let session = BBRimeCreateSession()
            guard session != 0 else { return 1 }
            let loaded = BBRimeSelectSchema(session, id)
            if loaded {
                for sample in ["a", "nihao", "1234567890"] {
                    for key in sample.utf8 { _ = BBRimeProcessKey(session, Int32(key), 0) }
                    BBRimeClearComposition(session)
                }
            }
            BBRimeDestroySession(session)
            guard loaded else { return 1 }
        }
        return 0
    }

    static func importSmoke(arguments: [String]) -> Bool {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent("rimes-scheme-smoke-" + UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: root) }
        do {
            try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
            let review: RimeSchemeImportReview
            if arguments.count >= 3 { review = try RimeSchemeImportService.inspect(url: URL(fileURLWithPath: arguments[2])) }
            else { review = try RimeSchemeImportService.inspectTextDictionary(data: Data("你好\tnihao\t100\n世界\tshijie\t100\n".utf8), sourceName: "公开测试词库") }
            let schema = arguments.count >= 4 ? arguments[3] : review.schemes[0].id
            let keys = arguments.count >= 5 ? arguments[4] : "nihao"
            let expected = arguments.count >= 6 ? arguments[5] : "你好"
            let library = MacRimeSchemeLibrary(root: root, runtime: false)
            let package = try library.install(review, selected: [schema])
            guard library.packages().count == 1, !FileManager.default.fileExists(atPath: root.appendingPathComponent("active.json").path) else { return false }
            try library.activate(package, schema: schema)
            let data = try Data(contentsOf: root.appendingPathComponent("active.json"))
            guard try JSONDecoder().decode(MacRimeSchemeSelection.self, from: data).schemaID == schema else { return false }
            let user = library.learningURL(package.id), resources = library.packageURL(package.id)
            let framework = ProcessInfo.processInfo.environment["RIMEBUFFER_FRAMEWORKS_DIR"] ?? ""
            guard BBRimeStart(resources.path, user.path, root.path, framework) else { return false }
            let session = BBRimeCreateSession(); guard session != 0 else { return false }
            defer { BBRimeDestroySession(session) }
            guard BBRimeSelectSchema(session, schema) else { return false }
            for key in keys.utf8 { _ = BBRimeProcessKey(session, Int32(key), 0) }
            var context = BBRimeContext()
            guard BBRimeGetContext(session, &context), context.numCandidates > 0 else { return false }
            _ = BBRimeProcessKey(session, 32, 0)
            guard let pointer = BBRimeCopyCommit(session) else { return false }
            let committed = String(cString: pointer); BBRimeFreeString(pointer)
            guard committed == expected else { print("Unexpected fixture commit: \(committed)"); return false }
            try library.activate(nil)
            try library.remove(package)
            guard library.packages().isEmpty, FileManager.default.fileExists(atPath: user.path) else { return false }
            print("scheme-import-smoke: PASS inspect, stage, deploy, activate, candidate, commit, restore, remove; source SHA256=\(review.archiveSHA256)")
            return true
        } catch { print("scheme-import-smoke: FAIL \(error.localizedDescription)"); return false }
    }
}
