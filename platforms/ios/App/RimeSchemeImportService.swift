import Foundation
import CryptoKit
import Yams
import CYaml
import ZIPFoundation

struct RimeSchemeCandidate: Identifiable, Sendable {
    let id: String
    let name: String
    let dependencies: [String]
    let warnings: [String]
    let blockingIssues: [String]
    let recommended: Bool
}

struct RimeSchemeImportReview: Sendable {
    let sourceName: String
    let archiveSHA256: String
    let schemes: [RimeSchemeCandidate]
    let fileCount: Int
    let expandedBytes: UInt64
    let warnings: [String]
    let unsupportedFeatures: [String]
    var details: [String] {
        ["识别到 \(schemes.count) 个 Rime 方案，\(fileCount) 个文件，展开约 \(expandedBytes / 1_024 / 1_024) MB。",
         "方案文件、词典、Lua 与 OpenCC 数据将部署到新的隔离目录。"]
    }
    fileprivate let archiveData: Data
    fileprivate let packagePrefix: String
    fileprivate let permittedPaths: [String]
    fileprivate let normalizedConfigurations: [String: Data]
    fileprivate var stagingSHA256: String? = nil
}

struct RimeSchemeStagedPackage: Sendable {
    let id: String
    let rootURL: URL
    let schemas: [ImportedRimeSchema]
    let sourceDigest: String
    let warnings: [String]
    let deploymentSchemaIDs: [String]
    var schemaIDs: [String] { schemas.map(\.id) }
}

enum RimeSchemeImportError: LocalizedError {
    case invalid(String)
    var errorDescription: String? { if case .invalid(let value) = self { return value }; return nil }
}

/// Main-app archive inspection and staging. Deployment and atomic publication belong to
/// the installer; keyboard processes never compile an unreviewed archive during first use.
enum RimeSchemeImportService {
    private static let maximumArchiveSize = 256 * 1_024 * 1_024
    private static let maximumFileSize: UInt64 = 128 * 1_024 * 1_024
    private static let maximumExpandedSize: UInt64 = 512 * 1_024 * 1_024
    private static let maximumEntries = 4_096
    private static let maximumConfigurationSize = 512 * 1_024
    private static let globalFiles: Set<String> = ["default.custom.yaml", "installation.yaml", "user.yaml",
        "hamster.yaml", "hamster.custom.yaml", "weasel.yaml", "weasel.custom.yaml", "squirrel.yaml", "squirrel.custom.yaml",
        "trime.yaml", "trime.custom.yaml", "ibus_rime.yaml", "ibus_rime.custom.yaml"]

    static func inspect(url: URL) throws -> RimeSchemeImportReview {
        let kind = url.pathExtension.lowercased()
        guard ["zip", "txt", "tsv"].contains(kind) else { throw invalid("请选择 Rime ZIP，或带编码的 UTF-8 TXT / TSV 词库。") }
        let access = url.startAccessingSecurityScopedResource()
        defer { if access { url.stopAccessingSecurityScopedResource() } }
        let file = try FileHandle(forReadingFrom: url)
        defer { try? file.close() }
        let data = try file.read(upToCount: maximumArchiveSize + 1) ?? Data()
        let name = url.deletingPathExtension().lastPathComponent
        return kind == "zip" ? try inspect(data: data, sourceName: name) : try inspectTextDictionary(data: data, sourceName: name)
    }

    /// A coded text table is data, not a scheme. Give it a deterministic, local
    /// table-translator wrapper; never infer pronunciations for unencoded words.
    static func inspectTextDictionary(data: Data, sourceName: String) throws -> RimeSchemeImportReview {
        guard data.count <= maximumConfigurationSize * 256,
              var text = String(data: data, encoding: .utf8) else { throw invalid("文本词库须为 UTF-8，且不超过 128 MB。") }
        if text.hasPrefix("\u{FEFF}") { text.removeFirst() }
        let digest = SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
        let id = "text_" + digest.prefix(16)
        var rows = [String](), alphabet = Set<Character>()
        let permitted = Set("abcdefghijklmnopqrstuvwxyz0123456789;'.,-/=")
        for (index, raw) in text.components(separatedBy: .newlines).enumerated() {
            if raw.isEmpty || raw.hasPrefix("#") { continue }
            let parts = raw.components(separatedBy: "\t")
            guard (2...3).contains(parts.count), !parts[0].isEmpty, parts[0].utf8.count <= 4096,
                  !parts[0].unicodeScalars.contains(where: { CharacterSet.controlCharacters.contains($0) }),
                  !parts[1].isEmpty, parts[1].count <= 256,
                  parts[1].allSatisfy({ permitted.contains($0) || $0 == " " }),
                  parts[1].contains(where: { permitted.contains($0) }),
                  parts.count == 2 || (UInt32(parts[2]) != nil) else {
                throw invalid("第 \(index + 1) 行格式不符：每行使用“词条⇥小写字母/数字编码⇥可选整数权重”。纯汉字词表须先补充编码。")
            }
            alphabet.formUnion(parts[1].filter { $0 != " " })
            rows.append(parts.joined(separator: "\t"))
        }
        guard !rows.isEmpty else { throw invalid("文本词库没有带编码的有效词条。") }
        let quote: (String) -> String = { "'" + $0.replacingOccurrences(of: "'", with: "''") + "'" }
        let title = String(sourceName.prefix(120))
        let schema = """
        schema:
          schema_id: \(id)
          name: \(quote(title))
          version: '1'
        engine:
          processors: [ascii_composer, recognizer, key_binder, speller, punctuator, selector, navigator, express_editor]
          segmentors: [ascii_segmentor, abc_segmentor, punct_segmentor, fallback_segmentor]
          translators: [punct_translator, table_translator]
        speller:
          alphabet: \(quote(String(alphabet.sorted())))
          delimiter: " "
        translator:
          dictionary: \(id)
          enable_sentence: false
          enable_user_dict: true
          enable_completion: true
        menu:
          page_size: 9
        """
        let dictionary = "---\nname: \(id)\nversion: '1'\nsort: by_weight\n...\n" + rows.joined(separator: "\n") + "\n"
        let archive = try Archive(accessMode: .create)
        for (path, content) in [("\(id).schema.yaml", schema), ("\(id).dict.yaml", dictionary)] {
            let bytes = Data(content.utf8)
            try archive.addEntry(with: path, type: .file, uncompressedSize: Int64(bytes.count), compressionMethod: .none) { offset, count in
                bytes.subdata(in: Int(offset)..<Int(offset) + count)
            }
        }
        guard let bytes = archive.data else { throw invalid("无法准备文本词库。") }
        var review = try inspect(data: bytes, sourceName: title)
        // The diagnostic identity belongs to the original input, not its generated wrapper.
        review = .init(sourceName: review.sourceName, archiveSHA256: digest, schemes: review.schemes,
            fileCount: review.fileCount, expandedBytes: review.expandedBytes,
            warnings: review.warnings + ["已为带编码的文本词库生成独立码表方案；原文件未修改。"],
            unsupportedFeatures: review.unsupportedFeatures, archiveData: bytes, packagePrefix: review.packagePrefix,
            permittedPaths: review.permittedPaths, normalizedConfigurations: review.normalizedConfigurations,
            stagingSHA256: Self.digest(bytes))
        return review
    }

    static func inspect(data: Data, sourceName: String) throws -> RimeSchemeImportReview {
        let archive = try CheckedArchive(data)
        guard !archive.entries.contains(where: { $0.type == .file && $0.path.lowercased().hasSuffix(".gram") }) else {
            throw invalid("这个 ZIP 包含 .gram 语言模型。五端统一导入暂不支持模型，请选择上游提供的基础方案包。")
        }
        // Finder/GitHub ZIPs often also carry AppleDouble metadata, compiled schemas
        // and example subtrees. Discover source roots before counting schemas.
        let sourceSchemas = archive.entries.filter {
            $0.type == .file && $0.path.hasSuffix(".schema.yaml") && !ignoredSourcePath($0.path)
        }
        let parents = Set(sourceSchemas.map { ($0.path as NSString).deletingLastPathComponent })
        let outermost = parents.filter { parent in
            !parents.contains { other in other != parent && (other.isEmpty || parent.hasPrefix(other + "/")) }
        }
        guard outermost.count == 1, let parent = outermost.first else {
            throw invalid(sourceSchemas.isEmpty ? "压缩包中没有 Rime 源方案（.schema.yaml）。" :
                "压缩包包含多套独立的方案目录，无法确定要导入哪一套；请选择上游提供的单套方案 ZIP。")
        }
        let rootSchemas = sourceSchemas.filter { ($0.path as NSString).deletingLastPathComponent == parent }
        guard rootSchemas.count <= 32 else { throw invalid("一套方案最多包含 32 个 .schema.yaml 文件。") }
        let prefix = parent.isEmpty ? "" : parent + "/"
        var permitted = [String](), entriesByPath = [String: Entry]()
        for entry in archive.entries where entry.type == .file && entry.path.hasPrefix(prefix) {
            let relative = String(entry.path.dropFirst(prefix.count))
            if !ignoredSourcePath(relative), allowedResource(relative) {
                permitted.append(relative); entriesByPath[relative] = entry
            }
        }
        let paths = Set(permitted)
        var schemas = [String: Node](), dictionaryHeaders = [String: Node](), configurationNodes = [String: Node]()
        var normalizedConfigurations = [String: Data](), normalizationWarnings = [String]()
        for (path, entry) in entriesByPath where path.hasSuffix(".yaml") && !path.hasSuffix(".dict.yaml") {
            guard entry.uncompressedSize <= maximumConfigurationSize else { throw invalid("配置 \(path) 超过 512 KB。") }
            let original = try archive.contents(entry, limit: maximumConfigurationSize)
            let normalized = try normalizeConfiguration(original, path: path)
            if let warning = normalized.warning {
                normalizationWarnings.append(warning)
                normalizedConfigurations[path] = normalized.data
            }
            let node = try yaml(normalized.data, path: path)
            configurationNodes[path] = node
            if path.hasSuffix(".schema.yaml") {
                guard let id = node["schema"]?["schema_id"]?.string, validID(id), path == id + ".schema.yaml" else {
                    throw invalid("\(path) 的 schema_id 缺失、不合法或与文件名不一致。")
                }
                guard schemas[id] == nil else { throw invalid("方案 ID 重复：\(id)。") }
                schemas[id] = node
            }
        }
        // Read dictionary headers only. Large dictionary bodies are data, not YAML trees.
        for (path, entry) in entriesByPath where path.hasSuffix(".dict.yaml") {
            let header = try archive.dictionaryHeader(entry)
            // Rime's dictionary reader tolerates tab-only blank header lines (spreadsheet
            // exports commonly contain them). Normalize only that whitespace for inspection;
            // keep the source dictionary and all tab-separated body columns byte-for-byte.
            let headerText = String(decoding: header, as: UTF8.self).components(separatedBy: "\n")
                .map { $0.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty ? "" : $0 }.joined(separator: "\n")
            let node = try yaml(Data(headerText.utf8), path: path)
            guard let name = node["name"]?.string, validResourceID(name) else {
                throw invalid("词典 \(path) 的 name 缺失或不合法。")
            }
            let fileID = String(path.dropLast(".dict.yaml".count))
            if fileID != name, (fileID as NSString).lastPathComponent != name {
                normalizationWarnings.append("词典 \(path) 的内部名称为 \(name)，保留原文件并按文件名检查引用；实际编译结果决定是否可用。")
            }
            dictionaryHeaders[fileID] = node
        }
        var warnings = ["不会导入全局 default.custom.yaml、安装标识、其他输入法界面配置，也不会修改现有并击方案。"] + normalizationWarnings
        var unsupported = [String]()
        // Included preset files are parsed by native Rime too; check their resource references
        // even when a reference is several includes away from the visible schema.
        for (path, node) in configurationNodes {
            for (key, value) in scalarFields(node) where !value.isEmpty {
                let leaf = key.split(separator: "/").last.map(String.init) ?? key
                if leaf == "dictionary", !validResourceID(value) {
                    throw invalid("\(path) 的 \(leaf) 引用标识不合法。")
                }
                if ["prism", "import_preset"].contains(leaf), !validID(value) {
                    throw invalid("\(path) 的 \(leaf) 引用标识不合法。")
                }
                if leaf == "opencc_config" { try checkRelativePath(value) }
                if ["__include", "__patch"].contains(leaf) { try checkReference(value, paths: paths) }
            }
            var effective = configurationFields(node)
            if path.hasSuffix(".schema.yaml") {
                let custom = String(path.dropLast(".schema.yaml".count)) + ".custom.yaml"
                if let patch = configurationNodes[custom]?["patch"] {
                    applyConfigurationPatch(patch, to: &effective)
                }
            } else if path.hasSuffix(".custom.yaml"), let patch = node["patch"] {
                let base = String(path.dropLast(".custom.yaml".count)) + ".schema.yaml"
                effective = configurationNodes[base].map(configurationFields) ?? [:]
                applyConfigurationPatch(patch, to: &effective)
            }
            try checkUserDictionaryReferences(effective, path: path, paths: paths)
        }
        // Read upstream selection intent, without installing its global preferences.
        var preferred = configurationNodes["default.yaml"]?["schema_list"]?.array().compactMap { $0["schema"]?.string } ?? []
        if let custom = archive.entries.first(where: { $0.path == prefix + "default.custom.yaml" }) {
            let node = try yaml(archive.contents(custom, limit: maximumConfigurationSize), path: "default.custom.yaml")
            if let list = node["patch"]?["schema_list"]?.array() { preferred = list.compactMap { $0["schema"]?.string } }
        }
        if let manifest = configurationNodes["rimes-package.yaml"]?["rimes_package"] {
            guard manifest["format"]?.string == "1" else { throw invalid("方案包清单版本不受支持，请更新犀术IME后再导入。") }
            if let ids = manifest["recommended_schemes"]?.array(of: String.self) { preferred = ids }
        }
        var candidates = [RimeSchemeCandidate]()
        for id in schemas.keys.sorted() {
            guard let node = schemas[id] else { continue }
            var effective = configurationFields(node)
            if let patch = configurationNodes[id + ".custom.yaml"]?["patch"] {
                applyConfigurationPatch(patch, to: &effective)
            }
            let name = effective["schema/name"] ?? id
            guard !name.isEmpty, name.count <= 120 else { throw invalid("方案名称须为 1–120 个字符：\(id)。") }
            let dependencies = effective.keys.filter { $0.hasPrefix("schema/dependencies/@") }.sorted().compactMap { effective[$0] }
            var issues = [String](), notes = [String]()
            for dependency in dependencies {
                if !validID(dependency) || schemas[dependency] == nil { issues.append("缺少依赖方案 \(dependency).schema.yaml。") }
            }
            if effective["schema/schema_id"] != id { issues.append("自定义补丁不能更改 schema_id。") }
            // Inspect the effective values, not both an overridden base dictionary and
            // its replacement. Native deployment remains the final configuration compiler.
            for (key, value) in effective.sorted(by: { $0.key < $1.key }) {
                let leaf = key.split(separator: "/").last.map(String.init) ?? key
                if leaf == "dictionary", !value.isEmpty {
                    guard validResourceID(value) else { throw invalid("词典引用标识不合法：\(id)。") }
                    if dictionaryHeaders[value] == nil {
                        if key == "translator/dictionary" { issues.append("缺少主词典 \(value).dict.yaml。") }
                        else { notes.append("辅助配置引用未随包提供的词典 \(value)；部署后需验证对应反查功能。") }
                    } else { try checkDictionary(value, headers: dictionaryHeaders, stack: [], checked: &issues) }
                }
                if leaf == "import_preset", !value.isEmpty {
                    guard validID(value) else { throw invalid("预设引用标识不合法：\(id)。") }
                    if value != "default", !paths.contains(value + ".yaml") { issues.append("缺少预设 \(value).yaml。") }
                }
                if value.hasPrefix("lua_"), value.contains("@") {
                    let parts = value.split(separator: "@", omittingEmptySubsequences: false)
                    if parts.count > 1, parts[1].hasPrefix("*") {
                        let members = parts[1].dropFirst().split(separator: "*", omittingEmptySubsequences: false).map(String.init)
                        guard let module = members.first, !module.isEmpty,
                              members.dropFirst().allSatisfy({ validID($0) && !$0.contains(".") && !$0.contains("-") }) else {
                            throw invalid("Lua 组件引用不合法：\(value)。")
                        }
                        let modulePath = try luaPath(module)
                        if !hasLuaModule(modulePath, paths: paths) { issues.append("缺少 Lua 组件 \(modulePath)。") }
                    } else if !paths.contains("rime.lua") { issues.append("旧式 Lua 组件 \(value) 需要包内 rime.lua。") }
                }
            }
            candidates.append(.init(id: id, name: name, dependencies: dependencies, warnings: unique(notes),
                                    blockingIssues: unique(issues), recommended: preferred.contains(id)))
        }
        for (path, entry) in entriesByPath where path.hasSuffix(".lua") {
            guard entry.uncompressedSize <= 1_024 * 1_024 else { throw invalid("Lua 文件 \(path) 超过 1 MB。") }
            let data = try archive.contents(entry, limit: 1_024 * 1_024)
            guard let text = String(data: data, encoding: .utf8) else { throw invalid("Lua 文件不是 UTF-8：\(path)。") }
            let tokens = luaTokens(text)
            for dependency in luaRequires(tokens) {
                let modulePath = try luaPath(dependency.module)
                if !hasLuaModule(modulePath, paths: paths), !["utf8", "math", "string", "table", "coroutine"].contains(dependency.module) {
                    if dependency.optional {
                        warnings.append("\(path) 的可选模块 \(dependency.module) 未随包提供，将使用脚本已有降级路径。")
                    } else { throw invalid("Lua 必需模块缺失：\(path) → \(modulePath)。") }
                }
            }
            let code = tokens.filter { !$0.literal }.map(\.value).joined(separator: " ")
            if ["os . execute", "io . popen", "package . loadlib"].contains(where: code.contains) {
                unsupported.append("\(path)：桌面程序启动／系统命令不在 iOS 键盘中执行；相关工具不可用，输入处理器仍保留。")
            }
        }
        // OpenCC assets may include unused legacy configs; report absent files without dropping
        // Lua replacement filters that intentionally load shipped Data/*.lua tables instead.
        for (path, entry) in entriesByPath where path.hasPrefix("opencc/") && path.hasSuffix(".json") {
            let data = try archive.contents(entry, limit: maximumConfigurationSize)
            guard let object = try? JSONSerialization.jsonObject(with: data) else { throw invalid("OpenCC 配置无效：\(path)。") }
            for reference in jsonFileReferences(object) {
                // A leading ./ is a same-directory reference, not traversal. Never normalize
                // away .. before validation, because native OpenCC reads the original string.
                let localReference = reference.split(separator: "/", omittingEmptySubsequences: false)
                    .filter { $0 != "." }.joined(separator: "/")
                try checkRelativePath(localReference)
                let resolved = ((path as NSString).deletingLastPathComponent as NSString).appendingPathComponent(localReference)
                if !paths.contains(resolved) { warnings.append("\(path) 引用未随包提供的 \(reference)；若使用 Lua 替代转换，需验证替代数据实际生效。") }
            }
        }
        let skipped = archive.entries.filter { $0.type == .file }.count - permitted.count
        if skipped > 0 { warnings.append("\(skipped) 个全局偏好、界面布局、教程或非方案资源未纳入部署。") }
        if entriesByPath.keys.contains(where: { $0.hasSuffix(".lua") }) {
            warnings.append("方案含 Lua 输入逻辑，将在受限运行环境中执行；导入检查不会执行脚本。")
        }
        return .init(sourceName: String(sourceName.prefix(120)), archiveSHA256: digest(data), schemes: candidates,
                     fileCount: archive.entries.filter { $0.type == .file }.count, expandedBytes: archive.expandedSize,
                     warnings: unique(warnings), unsupportedFeatures: unique(unsupported), archiveData: data,
                     packagePrefix: prefix, permittedPaths: permitted.sorted(), normalizedConfigurations: normalizedConfigurations)
    }

    static func stage(review: RimeSchemeImportReview, selectedSchemaIDs: [String], destinationRoot: URL) throws -> RimeSchemeStagedPackage {
        guard !selectedSchemaIDs.isEmpty, selectedSchemaIDs.count <= 32, Set(selectedSchemaIDs).count == selectedSchemaIDs.count else {
            throw invalid("请选择至少一个不重复的输入方案。")
        }
        let byID = Dictionary(uniqueKeysWithValues: review.schemes.map { ($0.id, $0) })
        let selected = try selectedSchemaIDs.map { id -> RimeSchemeCandidate in
            guard let value = byID[id] else { throw invalid("检查结果中没有方案 \(id)。") }
            guard value.blockingIssues.isEmpty else { throw invalid(value.blockingIssues.joined(separator: "\n")) }
            return value
        }
        var dependencyOrder = [String](), visited = Set<String>()
        func visit(_ id: String, ancestors: Set<String>) throws {
            // schema/dependencies is a build set, not recursive dictionary inclusion.
            // Rime permits reciprocal reverse-lookup schemas and compiles each once.
            guard !ancestors.contains(id), !visited.contains(id) else { return }
            guard ancestors.count < 32 else { throw invalid("方案依赖超过 32 层：\(id)。") }
            guard let candidate = byID[id] else { throw invalid("缺少依赖方案 \(id)。") }
            guard candidate.blockingIssues.isEmpty else { throw invalid("依赖方案 \(id)：" + candidate.blockingIssues.joined(separator: "\n")) }
            for dependency in candidate.dependencies { try visit(dependency, ancestors: ancestors.union([id])) }
            visited.insert(id); dependencyOrder.append(id)
        }
        for id in selectedSchemaIDs { try visit(id, ancestors: []) }
        guard digest(review.archiveData) == (review.stagingSHA256 ?? review.archiveSHA256) else { throw invalid("归档在检查后发生变化，请重新检查。") }
        let archive = try CheckedArchive(review.archiveData)
        let id = UUID().uuidString
        let manager = FileManager.default
        try manager.createDirectory(at: destinationRoot, withIntermediateDirectories: true)
        guard try destinationRoot.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink != true else {
            throw invalid("方案暂存目录不能是符号链接。")
        }
        let root = destinationRoot.appendingPathComponent(id, isDirectory: true)
        try manager.createDirectory(at: root, withIntermediateDirectories: false,
                                    attributes: [.posixPermissions: 0o700])
        do {
            var values = URLResourceValues(); values.isExcludedFromBackup = true
            var protected = root; try protected.setResourceValues(values)
            let permitted = Set(review.permittedPaths)
            for entry in archive.entries where entry.type == .file && entry.path.hasPrefix(review.packagePrefix) {
                let relative = String(entry.path.dropFirst(review.packagePrefix.count))
                guard permitted.contains(relative), relative != "default.yaml" else { continue }
                let target = root.appendingPathComponent(relative)
                try manager.createDirectory(at: target.deletingLastPathComponent(), withIntermediateDirectories: true,
                                            attributes: [.posixPermissions: 0o700])
                if let normalized = review.normalizedConfigurations[relative] {
                    try normalized.write(to: target, options: .atomic)
                    try manager.setAttributes([.posixPermissions: 0o600], ofItemAtPath: target.path)
                    continue
                }
                guard manager.createFile(atPath: target.path, contents: nil, attributes: [.posixPermissions: 0o600]) else {
                    throw invalid("无法创建方案资源 \(relative)。")
                }
                let file = try FileHandle(forWritingTo: target)
                do {
                    var written: UInt64 = 0
                    let crc = try archive.archive.extract(entry, bufferSize: 32_768) { chunk in
                        guard written <= maximumFileSize - UInt64(chunk.count), written + UInt64(chunk.count) <= entry.uncompressedSize else {
                            throw invalid("资源展开大小超过声明或限制：\(relative)。")
                        }
                        try file.write(contentsOf: chunk); written += UInt64(chunk.count)
                    }
                    try file.close()
                    guard written == entry.uncompressedSize, crc == entry.checksum else { throw invalid("资源校验失败：\(relative)。") }
                } catch { try? file.close(); throw error }
            }
            var defaults = Node([(Node("config_version"), Node("1")),
                                 (Node("schema_list"), Node(selectedSchemaIDs.map { Node([(Node("schema"), Node($0))]) }))])
            if let entry = archive.entries.first(where: { $0.path == review.packagePrefix + "default.yaml" }) {
                let source = try yaml(review.normalizedConfigurations["default.yaml"] ?? archive.contents(entry, limit: maximumConfigurationSize), path: "default.yaml")
                for section in ["key_binder", "ascii_composer", "recognizer", "punctuator", "menu", "speller"] {
                    if let value = source[section] { defaults[section] = value }
                }
            }
            try Data(Yams.serialize(node: defaults).utf8).write(to: root.appendingPathComponent("default.yaml"), options: .atomic)
            return .init(id: id, rootURL: root, schemas: selected.map { .init(id: $0.id, name: $0.name) },
                         sourceDigest: review.archiveSHA256,
                         warnings: unique(review.warnings + review.unsupportedFeatures + dependencyOrder.flatMap { byID[$0]?.warnings ?? [] }),
                         deploymentSchemaIDs: dependencyOrder)
        } catch {
            try? manager.removeItem(at: root)
            throw error
        }
    }

    private static func allowedResource(_ path: String) -> Bool {
        let lower = path.lowercased()
        guard !globalFiles.contains(lower), !lower.hasPrefix("build/"), !lower.hasPrefix("sync/"),
              !lower.contains(".userdb"), !lower.hasPrefix("__macosx/") else { return false }
        if !path.contains("/") {
            return lower.hasSuffix(".yaml") || lower.hasSuffix(".txt") || lower == "rime.lua"
        }
        if lower.hasPrefix("lua/") {
            return lower.hasSuffix(".lua") || lower.hasSuffix(".txt") || lower.hasSuffix(".json") || lower.hasSuffix(".reverse.bin")
        }
        if lower.hasPrefix("opencc/") {
            return [".json", ".txt", ".ocd", ".ocd2", ".lua"].contains { lower.hasSuffix($0) }
        }
        // Rime Ice keeps source tables in cn_dicts/ and en_dicts/. These are data
        // resources, staged below the isolated package root just like root tables.
        if path.hasSuffix(".dict.yaml") { return validResourceID(String(path.dropLast(".dict.yaml".count))) }
        if path.hasSuffix(".txt") { return validResourceID(String(path.dropLast(".txt".count))) }
        return false
    }

    private static func checkDictionary(_ id: String, headers: [String: Node], stack: [String], checked issues: inout [String]) throws {
        var visited = Set<String>()
        func visit(_ id: String, stack: [String]) throws {
            guard !stack.contains(id), stack.count <= 32 else { throw invalid("词典依赖存在循环或层级过深：\(id)。") }
            guard visited.insert(id).inserted else { return }
            guard let header = headers[id] else { issues.append("缺少词典 \(id).dict.yaml。"); return }
            for dependency in header["import_tables"]?.array(of: String.self) ?? [] {
                guard validResourceID(dependency) else { throw invalid("词典引用标识不合法。") }
                try visit(dependency, stack: stack + [id])
            }
        }
        try visit(id, stack: stack)
    }

    private static func checkReference(_ value: String, paths: Set<String>) throws {
        let reference = value.hasSuffix("?") ? String(value.dropLast()) : value
        guard !reference.contains("\\"), !reference.split(separator: "/").contains(".."),
              !reference.unicodeScalars.contains(where: CharacterSet.controlCharacters.contains) else {
            throw invalid("配置引用不能指向方案目录之外。")
        }
        // `/punctuator` and `:/punctuator` refer to YAML nodes in the current
        // resource. Only the part before ':' is a filesystem resource identifier.
        if let colon = reference.firstIndex(of: ":") {
            let name = String(reference[..<colon])
            guard !name.isEmpty else { return }
            let resource = name.hasSuffix(".yaml") ? String(name.dropLast(5)) : name
            guard validResourceID(resource) else { throw invalid("配置资源引用不合法：\(name)。") }
            if !value.hasSuffix("?"), !paths.contains(resource + ".yaml") {
                throw invalid("缺少外部配置引用 \(resource).yaml。")
            }
        }
    }

    private static func ignoredSourcePath(_ path: String) -> Bool {
        path.split(separator: "/").contains { component in
            let value = component.lowercased()
            return value.hasPrefix(".") || value == "__macosx" || value == "build" || value == "sync" || value.contains(".userdb")
        }
    }

    private static func hasLuaModule(_ path: String, paths: Set<String>) -> Bool {
        paths.contains(path) || paths.contains(String(path.dropLast(4)) + "/init.lua")
    }

    private struct LuaToken { let value: String; let literal: Bool }

    /// A lexical scan, never evaluation: ignore comments and quoted source snippets.
    /// Keep literal arguments so `require`, `require(...)` and pcall can be identified
    /// without treating an unrelated pcall on the same line as an optional import.
    private static func luaTokens(_ text: String) -> [LuaToken] {
        let chars = Array(text.unicodeScalars)
        var index = 0, tokens = [LuaToken]()
        func longBracket(_ start: Int) -> (end: Int, value: String)? {
            guard start < chars.count, chars[start] == "[" else { return nil }
            var open = start + 1
            while open < chars.count, chars[open] == "=" { open += 1 }
            guard open < chars.count, chars[open] == "[" else { return nil }
            let equals = open - start - 1
            var cursor = open + 1
            while cursor < chars.count {
                if chars[cursor] == "]" {
                    var close = cursor + 1
                    while close < chars.count, chars[close] == "=" { close += 1 }
                    if close < chars.count, close - cursor - 1 == equals, chars[close] == "]" {
                        return (close + 1, String(String.UnicodeScalarView(chars[(open + 1)..<cursor])))
                    }
                }
                cursor += 1
            }
            return (chars.count, "") // syntax validity is the engine's responsibility
        }
        while index < chars.count {
            let char = chars[index]
            if CharacterSet.whitespacesAndNewlines.contains(char) { index += 1; continue }
            if char == "-", index + 1 < chars.count, chars[index + 1] == "-" {
                if let block = longBracket(index + 2) { index = block.end }
                else { while index < chars.count, chars[index] != "\n" { index += 1 } }
                continue
            }
            if char == "'" || char == "\"" {
                index += 1
                var value = String.UnicodeScalarView()
                var escaped = false
                while index < chars.count, chars[index] != char {
                    if chars[index] == "\\", index + 1 < chars.count {
                        escaped = true
                        index += 1 // escaped/dynamic module names are checked by the actual Lua runtime
                    }
                    value.append(chars[index]); index += 1
                }
                if index < chars.count { index += 1 }
                tokens.append(.init(value: escaped ? "<escaped-string>" : String(value), literal: !escaped)); continue
            }
            if let block = longBracket(index) {
                tokens.append(.init(value: block.value, literal: true)); index = block.end; continue
            }
            if CharacterSet.letters.contains(char) || char == "_" {
                let start = index; index += 1
                while index < chars.count, CharacterSet.alphanumerics.contains(chars[index]) || chars[index] == "_" { index += 1 }
                tokens.append(.init(value: String(String.UnicodeScalarView(chars[start..<index])), literal: false))
            } else { tokens.append(.init(value: String(char), literal: false)); index += 1 }
        }
        return tokens
    }

    private static func luaRequires(_ tokens: [LuaToken]) -> [(module: String, optional: Bool)] {
        var result = [(module: String, optional: Bool)]()
        for i in tokens.indices where !tokens[i].literal && tokens[i].value == "require" {
            if i > 0, [".", ":"].contains(tokens[i - 1].value) { continue }
            var next = i + 1
            let optional = i >= 2 && tokens[i - 1].value == "(" && tokens[i - 2].value == "pcall" && !tokens[i - 2].literal
            if next < tokens.count, tokens[next].value == (optional ? "," : "("), !tokens[next].literal { next += 1 }
            if next < tokens.count, tokens[next].literal { result.append((tokens[next].value, optional)) }
        }
        return result
    }

    private static func luaPath(_ module: String) throws -> String {
        guard !module.isEmpty, module.count <= 180, !module.contains(".."),
              module.unicodeScalars.allSatisfy({ CharacterSet(charactersIn: "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_./-").contains($0) }),
              !module.hasPrefix("/"), !module.hasSuffix("/") else { throw invalid("Lua 模块引用不合法。") }
        return "lua/" + module.replacingOccurrences(of: ".", with: "/") + ".lua"
    }

    /// Return leaf names and scalar sequence contents without constructing arbitrary YAML tags.
    private static func scalarFields(_ root: Node) -> [(String, String)] {
        func walk(_ node: Node, key: String) -> [(String, String)] {
            switch node {
            case .scalar(let value): return [(key, value.string)]
            case .sequence(let values): return values.flatMap { walk($0, key: key) }
            case .mapping(let values): return values.flatMap { pair in walk(pair.value, key: pair.key.string ?? "") }
            }
        }
        return walk(root, key: "")
    }

    private static func yaml(_ data: Data, path: String) throws -> Node {
        guard data.count <= maximumConfigurationSize, let text = String(data: data, encoding: .utf8) else {
            throw invalid("配置不是有效 UTF-8 或超过 512 KB：\(path)。")
        }
        try checkYAMLEvents(data, path: path)
        let root: Node
        do { guard let value = try Yams.compose(yaml: text) else { throw invalid("空配置") }; root = value }
        catch { throw invalid("YAML 无法解析：\(path)。") }
        var budget = 40_000
        do { try validateNode(root, depth: 0, budget: &budget) }
        catch { throw invalid("\(path)：\(error.localizedDescription)") }
        return root
    }

    /// Repair only characters forbidden by YAML's character production, and disclose the
    /// exact repair. Dictionary bodies and executable Lua are never rewritten here.
    private static func normalizeConfiguration(_ data: Data, path: String) throws -> (data: Data, warning: String?) {
        guard let text = String(data: data, encoding: .utf8) else { throw invalid("配置不是 UTF-8：\(path)。") }
        func forbidden(_ value: Unicode.Scalar) -> Bool {
            let code = value.value
            return (code < 0x20 && ![0x09, 0x0a, 0x0d].contains(code)) || (0x7f...0x84).contains(code)
                || (0x86...0x9f).contains(code) || code == 0xfffe || code == 0xffff
        }
        let invalidScalars = text.unicodeScalars.filter(forbidden)
        var result = data, warnings = [String]()
        if !invalidScalars.isEmpty {
            let codes = Set(invalidScalars.map { String(format: "U+%04X", $0.value) }).sorted().joined(separator: "、")
            let clean = String(String.UnicodeScalarView(text.unicodeScalars.filter { !forbidden($0) }))
            result = Data(clean.utf8)
            warnings.append("隔离副本将移除 \(invalidScalars.count) 个 YAML 不允许的控制字符（\(codes)），可能改变对应符号候选；原 ZIP 不变。")
        }
        if path == "symbols.yaml" {
            try checkYAMLEvents(result, path: path)
            guard let root = try Yams.compose(yaml: String(decoding: result, as: UTF8.self)) else { throw invalid("符号配置为空。") }
            var budget = 40_000
            try validateNode(root, depth: 0, budget: &budget, allowDuplicateKeys: true)
            var duplicates = Set<String>()
            let normalized = deduplicateSymbols(root, path: "", duplicates: &duplicates)
            if !duplicates.isEmpty {
                result = Data(try Yams.serialize(node: normalized).utf8)
                warnings.append("符号表重复键 \(duplicates.sorted().joined(separator: "、")) 按 Rime 实际读取规则保留最后一项；原 ZIP 不变。")
            }
        }
        return (result, warnings.isEmpty ? nil : "\(path)：" + warnings.joined(separator: " "))
    }

    private static func deduplicateSymbols(_ node: Node, path: String, duplicates: inout Set<String>) -> Node {
        switch node {
        case .scalar: return node
        case .sequence(let values): return Node(values.map { deduplicateSymbols($0, path: path, duplicates: &duplicates) })
        case .mapping(let values):
            var positions = [String: Int](), pairs = [(Node, Node)]()
            for (key, value) in values {
                let name = key.string ?? ""
                let location = path.isEmpty ? name : path + "/" + name
                let converted = deduplicateSymbols(value, path: location, duplicates: &duplicates)
                if let index = positions[name] { pairs[index] = (key, converted); duplicates.insert(location) }
                else { positions[name] = pairs.count; pairs.append((key, converted)) }
            }
            return Node(pairs)
        }
    }

    /// libyaml emits events iteratively, so nesting is checked before Yams recursively
    /// composes nodes. Unlike a punctuation heuristic, quoted regexes/comments cannot hide
    /// structural nesting or falsely reject the scheme's long symbols lists.
    private static func checkYAMLEvents(_ data: Data, path: String) throws {
        var parser = yaml_parser_t()
        guard yaml_parser_initialize(&parser) != 0 else { throw invalid("无法创建 YAML 解析器。") }
        defer { yaml_parser_delete(&parser) }
        try data.withUnsafeBytes { raw in
            guard let bytes = raw.baseAddress?.assumingMemoryBound(to: UInt8.self) else { throw invalid("空 YAML。") }
            yaml_parser_set_input_string(&parser, bytes, raw.count)
            var depth = 0, events = 0, documents = 0
            while true {
                var event = yaml_event_t()
                guard yaml_parser_parse(&parser, &event) != 0 else { throw invalid("YAML 格式错误：\(path)。") }
                let type = event.type
                yaml_event_delete(&event)
                events += 1
                if type == YAML_MAPPING_START_EVENT || type == YAML_SEQUENCE_START_EVENT { depth += 1 }
                if type == YAML_MAPPING_END_EVENT || type == YAML_SEQUENCE_END_EVENT { depth -= 1 }
                if type == YAML_DOCUMENT_START_EVENT { documents += 1 }
                guard depth <= 64, events <= 40_000, documents <= 1 else { throw invalid("YAML 层级、节点数或文档数超过限制：\(path)。") }
                if type == YAML_STREAM_END_EVENT { break }
            }
        }
    }

    private static func validateNode(_ node: Node, depth: Int, budget: inout Int, allowDuplicateKeys: Bool = false) throws {
        budget -= 1
        guard budget >= 0, depth <= 64 else { throw invalid("YAML 引用展开或层级超过限制。") }
        let tags = ["str", "map", "seq", "bool", "int", "float", "null", "value", "merge", "timestamp"]
        guard tags.contains(where: { node.tag.description == "tag:yaml.org,2002:" + $0 }) else { throw invalid("YAML 包含不支持的标签。") }
        switch node {
        case .scalar: break
        case .sequence(let values): for value in values { try validateNode(value, depth: depth + 1, budget: &budget, allowDuplicateKeys: allowDuplicateKeys) }
        case .mapping(let values):
            var keys = Set<String>()
            for (key, value) in values {
                guard case .scalar(let scalar) = key else { throw invalid("YAML 含复杂键名。") }
                guard keys.insert(scalar.string).inserted || allowDuplicateKeys else { throw invalid("YAML 键名重复：\(scalar.string)。") }
                try validateNode(key, depth: depth + 1, budget: &budget, allowDuplicateKeys: allowDuplicateKeys)
                try validateNode(value, depth: depth + 1, budget: &budget, allowDuplicateKeys: allowDuplicateKeys)
            }
        }
    }

    private static func jsonFileReferences(_ value: Any) -> [String] {
        if let map = value as? [String: Any] {
            return map.flatMap { key, value in key == "file" ? [(value as? String) ?? ""] : jsonFileReferences(value) }
        }
        if let list = value as? [Any] { return list.flatMap(jsonFileReferences) }
        return []
    }
    private static func matches(_ pattern: String, _ text: String) -> [String] {
        guard let expression = try? NSRegularExpression(pattern: pattern) else { return [] }
        return expression.matches(in: text, range: NSRange(text.startIndex..., in: text)).compactMap {
            Range($0.range(at: 1), in: text).map { String(text[$0]) }
        }
    }
    private static func validID(_ value: String) -> Bool {
        !value.isEmpty && value.count <= 100 && !value.contains("..") && value.unicodeScalars.allSatisfy {
            CharacterSet(charactersIn: "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-").contains($0)
        }
    }
    private static func validResourceID(_ value: String) -> Bool {
        value.count <= 256 && value.split(separator: "/", omittingEmptySubsequences: false).allSatisfy {
            $0 != "." && validID(String($0))
        }
    }
    /// Retain native namespaces to pair user_dict with its own db_class.
    /// Custom patch paths are applied separately using the compiler's semantics.
    private static func configurationFields(_ root: Node) -> [String: String] {
        var fields = [String: String]()
        func walk(_ node: Node, path: String) {
            switch node {
            case .scalar(let value): fields[path] = value.string
            case .mapping(let values):
                for pair in values {
                    // A slash in an ordinary YAML key is literal. Only patch keys
                    // are traversed as paths by the native configuration compiler.
                    guard let key = pair.key.string, !key.contains("/") else { continue }
                    walk(pair.value, path: path.isEmpty ? key : path + "/" + key)
                }
            case .sequence(let values):
                for (index, value) in values.enumerated() { walk(value, path: path + "/@" + String(index)) }
            }
        }
        walk(root, path: "")
        return fields
    }
    private static func checkUserDictionaryReferences(_ fields: [String: String], path: String, paths: Set<String>) throws {
        for (key, value) in fields where key == "user_dict" || key.hasSuffix("/user_dict") {
            // Empty and bare names already resolve inside the native user directory.
            // A subdirectory name is accepted only for a supplied StableDb table:
            // librime's StableDb::Open uses OpenReadOnly, never creates a database.
            guard !value.isEmpty else { continue }
            guard validResourceID(value) else { throw invalid("\(path) 的 user_dict 引用标识不合法。") }
            guard value.contains("/") else { continue }
            let namespace = String(key.dropLast("user_dict".count))
            guard fields[namespace + "db_class"] == "stabledb" else {
                throw invalid("\(path) 的子目录 user_dict 仅支持随包提供的只读 stabledb 词典。")
            }
            guard paths.contains(value + ".txt") else {
                throw invalid("\(path) 缺少只读用户词典 \(value).txt。")
            }
        }
    }
    private static func applyConfigurationPatch(_ patch: Node, to fields: inout [String: String]) {
        func apply(_ node: Node, path: String, merge: Bool) {
            if merge, case .sequence(let values) = node {
                let indices = fields.keys.compactMap { key -> Int? in
                    guard key.hasPrefix(path + "/@") else { return nil }
                    return Int(key.dropFirst(path.count + 2).split(separator: "/").first ?? "")
                }
                let offset = (indices.max() ?? -1) + 1
                for (index, value) in values.enumerated() {
                    apply(value, path: path + "/@" + String(offset + index), merge: false)
                }
                return
            }
            if merge, case .mapping(let values) = node {
                edit(values, prefix: path, merge: true)
                return
            }
            fields = path.isEmpty ? [:] : fields.filter { $0.key != path && !$0.key.hasPrefix(path + "/") }
            for (key, value) in configurationFields(node) {
                fields[path.isEmpty ? key : key.isEmpty ? path : path + "/" + key] = value
            }
        }
        func edit(_ values: Node.Mapping, prefix: String, merge: Bool) {
            // Native ConfigMap applies keys in lexical order. A branch replacement
            // removes its old db_class; appending to a scalar concatenates it.
            for pair in values.sorted(by: { ($0.key.string ?? "") < ($1.key.string ?? "") }) {
                guard let raw = pair.key.string else { continue }
                let append = raw == "__append" || raw.hasSuffix("/+")
                let directive = raw == "__append" || raw == "__merge"
                let rawKey = directive ? "" : raw.hasSuffix("/+") || raw.hasSuffix("/=") ? String(raw.dropLast(2)) : raw
                if merge, rawKey.contains("/") { continue }
                let key = merge ? rawKey : String(rawKey.drop(while: { $0 == "/" }))
                let path = prefix.isEmpty ? key : key.isEmpty ? prefix : prefix + "/" + key
                if append, let value = pair.value.string, let existing = fields[path] {
                    fields[path] = existing + value
                } else {
                    apply(pair.value, path: path, merge: append || raw == "__merge" || (merge && !raw.hasSuffix("/=")))
                }
            }
        }
        if case .mapping(let values) = patch { edit(values, prefix: "", merge: false) }
    }
    private static func checkRelativePath(_ path: String) throws {
        let parts = path.split(separator: "/", omittingEmptySubsequences: false)
        guard !path.isEmpty, path.utf8.count <= 1_024, !path.hasPrefix("/"), !path.contains("\\"), !path.contains(":"),
              !parts.contains(".."), !parts.contains("."), !parts.dropLast().contains(""),
              !path.unicodeScalars.contains(where: CharacterSet.controlCharacters.contains) else { throw invalid("归档包含不安全的资源路径。") }
        for part in parts where !part.isEmpty {
            let base = part.split(separator: ".", omittingEmptySubsequences: false).first?.lowercased() ?? ""
            let reserved = ["con", "aux", "nul", "prn"].contains(base) || base.range(of: "^(com|lpt)[1-9]$", options: .regularExpression) != nil
            guard part.utf8.count <= 255, !part.hasSuffix("."), !part.hasSuffix(" "), !reserved else {
                throw invalid("资源路径不能在五端通用：\(part)。")
            }
        }
    }
    private static func digest(_ data: Data) -> String { SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined() }
    private static func unique(_ values: [String]) -> [String] { var seen = Set<String>(); return values.filter { seen.insert($0).inserted } }
    private static func invalid(_ text: String) -> RimeSchemeImportError { .invalid(text) }

    private struct CheckedArchive {
        let archive: Archive
        let entries: [Entry]
        let expandedSize: UInt64
        init(_ data: Data) throws {
            let expected = try Self.directoryCount(data)
            archive = try Archive(data: data, accessMode: .read)
            var result = [Entry](), paths = Set<String>(), size: UInt64 = 0
            for entry in archive {
                guard result.count < maximumEntries else { throw invalid("ZIP 超过 4096 项。") }
                try checkRelativePath(entry.path)
                let normalized = entry.path.trimmingCharacters(in: CharacterSet(charactersIn: "/")).precomposedStringWithCanonicalMapping.lowercased()
                guard paths.insert(normalized).inserted else { throw invalid("ZIP 资源路径重复。") }
                guard entry.type != .symlink else { throw invalid("ZIP 不得包含符号链接。") }
                guard entry.uncompressedSize <= maximumFileSize, size <= maximumExpandedSize - entry.uncompressedSize,
                      entry.compressedSize <= UInt64(data.count), entry.uncompressedSize <= max(1, entry.compressedSize) * 100 else {
                    throw invalid("ZIP 超过展开大小或压缩比限制。")
                }
                size += entry.uncompressedSize; result.append(entry)
            }
            guard result.count == expected else { throw invalid("ZIP 目录不完整。") }
            entries = result; expandedSize = size
        }
        func contents(_ entry: Entry, limit: Int) throws -> Data {
            guard entry.uncompressedSize <= limit else { throw invalid("配置文件超过读取限制：\(entry.path)。") }
            var data = Data()
            let crc = try archive.extract(entry, bufferSize: 32_768) { chunk in
                guard chunk.count <= limit, data.count <= limit - chunk.count else { throw invalid("配置展开超过限制。") }
                data.append(chunk)
            }
            guard UInt64(data.count) == entry.uncompressedSize, crc == entry.checksum else { throw invalid("配置校验失败：\(entry.path)。") }
            return data
        }
        func dictionaryHeader(_ entry: Entry) throws -> Data {
            let progress = Progress()
            var header = Data(), completed = false
            do {
                _ = try archive.extract(entry, bufferSize: 16_384, progress: progress) { chunk in
                    guard header.count <= maximumConfigurationSize - chunk.count else { throw invalid("词典头超过 512 KB：\(entry.path)。") }
                    header.append(chunk)
                    if let range = header.range(of: Data("\n...".utf8)),
                       header.count > range.upperBound,
                       [UInt8(9), UInt8(10), UInt8(13), UInt8(32)].contains(header[range.upperBound]) {
                        header = header.subdata(in: 0..<range.lowerBound)
                        completed = true
                        progress.cancel()
                    }
                }
            } catch Archive.ArchiveError.cancelledOperation where completed {
                // Use ZIPFoundation's cancellation contract for both stored and
                // deflated entries. Staging still checks the complete body CRC.
            }
            if !completed, header.contains(9) { throw invalid("词典缺少 YAML 结束标记：\(entry.path)。") }
            return header
        }
        static func directoryCount(_ data: Data) throws -> Int {
            guard (22...maximumArchiveSize).contains(data.count) else { throw invalid("ZIP 为空、损坏或超过 256 MB。") }
            func u16(_ n: Int) -> Int { Int(data[n]) | Int(data[n + 1]) << 8 }
            func u32(_ n: Int) -> UInt64 { UInt64(u16(n)) | UInt64(u16(n + 2)) << 16 }
            guard let end = stride(from: data.count - 22, through: max(0, data.count - 65_557), by: -1).first(where: {
                u32($0) == 0x06054b50 && $0 + 22 + u16($0 + 20) == data.count
            }) else { throw invalid("无法识别 ZIP 目录。") }
            let count = u16(end + 10), length = u32(end + 12), offset = u32(end + 16)
            guard u16(end + 4) == 0, u16(end + 6) == 0, u16(end + 8) == count, count <= maximumEntries,
                  offset != 0xffffffff, length != 0xffffffff, offset + length == UInt64(end) else {
                throw invalid("不支持分卷、ZIP64、超过 4096 项或目录异常的 ZIP。")
            }
            var cursor = Int(offset)
            for _ in 0..<count {
                guard cursor + 46 <= end, u32(cursor) == 0x02014b50, u16(cursor + 8) & 1 == 0 else {
                    throw invalid("ZIP 条目损坏或已加密。")
                }
                let length = 46 + u16(cursor + 28) + u16(cursor + 30) + u16(cursor + 32)
                guard length <= end - cursor else { throw invalid("ZIP 条目长度错误。") }
                cursor += length
            }
            guard cursor == end else { throw invalid("ZIP 目录计数不一致。") }
            return count
        }
    }
}
