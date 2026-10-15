import Foundation
import RimesCore
import Yams
import ZIPFoundation

struct CustomLayoutImportResult {
    var layouts: [CustomKeyboardLayout]
    var title: String
    var details: [String]
    var warnings: [String]
}

enum CustomLayoutImportError: LocalizedError {
    case invalid(String)
    var errorDescription: String? { if case .invalid(let text) = self { return text }; return nil }
}

/// Main-app-only conversion. No imported file is installed in the engine or executed.
enum CustomLayoutImportService {
    private static let archiveLimit = 64 * 1_024 * 1_024
    private static let yamlLimit = 512 * 1_024
    private static let entryLimit = 512

    static func inspect(url: URL) throws -> CustomLayoutImportResult {
        let access = url.startAccessingSecurityScopedResource()
        defer { if access { url.stopAccessingSecurityScopedResource() } }
        let limit = url.pathExtension.lowercased() == "zip" ? archiveLimit : yamlLimit
        let handle = try FileHandle(forReadingFrom: url)
        defer { try? handle.close() }
        let data = try handle.read(upToCount: limit + 1) ?? Data()
        guard data.count <= limit else { throw invalid("导入文件过大：ZIP 上限 64 MB，配置上限 512 KB。") }
        return try inspect(data: data, filename: url.lastPathComponent)
    }

    static func inspect(data: Data, filename: String) throws -> CustomLayoutImportResult {
        switch (filename as NSString).pathExtension.lowercased() {
        case "json":
            var layout = try CustomKeyboardLayout.importData(data)
            // Import is always a new draft, never an overwrite of an existing applied ID.
            layout.id = UUID().uuidString
            return .init(layouts: [layout], title: "\(ProductBrand.name) 布局",
                         details: ["已识别 1 套布局，将作为新副本打开。"],
                         warnings: ["这里只导入普通输入键位，不改变并击方案或词库。"])
        case "yaml", "yml":
            return try inspectYAML(data, source: filename)
        case "zip":
            return try inspectZIP(data)
        default:
            throw invalid("请选择 \(ProductBrand.name) JSON、仓鼠 YAML 或含键盘配置的 ZIP。")
        }
    }

    private static func inspectZIP(_ data: Data) throws -> CustomLayoutImportResult {
        let expectedEntries = try checkZIPDirectory(data)
        let archive: Archive
        do { archive = try Archive(data: data, accessMode: .read) }
        catch { throw invalid("无法读取 ZIP，请检查压缩包是否完整。") }
        var entries = [Entry](), paths = Set<String>(), total: UInt64 = 0
        for entry in archive {
            guard entries.count < entryLimit else { throw invalid("ZIP 文件数量超过 512 项。") }
            try checkPath(entry.path)
            let normalized = entry.path.trimmingCharacters(in: CharacterSet(charactersIn: "/"))
                .precomposedStringWithCanonicalMapping.lowercased()
            guard paths.insert(normalized).inserted else { throw invalid("ZIP 含有重复路径，无法确定应导入哪份配置。") }
            guard entry.type != .symlink else { throw invalid("ZIP 含符号链接，不能作为布局包导入。") }
            guard entry.uncompressedSize <= 32 * 1_024 * 1_024,
                  total <= 128 * 1_024 * 1_024 - entry.uncompressedSize else {
                throw invalid("ZIP 展开大小超过限制（单项 32 MB，总计 128 MB）。")
            }
            total += entry.uncompressedSize
            guard entry.compressedSize <= UInt64(data.count),
                  entry.uncompressedSize <= max(1, entry.compressedSize) * 100 else {
                throw invalid("ZIP 条目压缩比超过 100 倍，无法安全读取。")
            }
            entries.append(entry)
        }
        guard entries.count == expectedEntries else { throw invalid("ZIP 目录不完整或含不支持的条目。") }
        let configs = entries.filter {
            $0.type == .file && ["hamster.yaml", "hamster.yml", "hamster.custom.yaml", "hamster.custom.yml"]
                .contains(($0.path as NSString).lastPathComponent.lowercased())
        }
        guard !configs.isEmpty else {
            throw invalid("压缩包中未找到 hamster.yaml 布局。Rime 方案和词典不是键位布局，请导入 \(ProductBrand.name) JSON 或仓鼠键盘 YAML。")
        }
        guard configs.count <= 8 else { throw invalid("ZIP 中键盘配置文件过多，请只保留需要导入的配置。") }
        let luaCount = entries.filter { $0.path.lowercased().hasSuffix(".lua") }.count
        let schemaCount = entries.filter { $0.path.lowercased().hasSuffix(".schema.yaml") }.count
        let dictionaryCount = entries.filter { $0.path.lowercased().hasSuffix(".dict.yaml") }.count
        var result = CustomLayoutImportResult(layouts: [], title: "ZIP 布局检查",
            details: ["检查了 \(entries.count) 个条目，仅读取 \(configs.count) 份仓鼠配置。"],
            warnings: ["Rime 方案、词库和 Lua 没有安装或执行；转换键位不代表原输入方案可用。"])
        if luaCount + schemaCount + dictionaryCount > 0 {
            result.details.append("另含 \(schemaCount) 个方案、\(dictionaryCount) 个词典、\(luaCount) 个 Lua 文件，仅统计，未加载。")
        }
        for entry in configs {
            guard entry.uncompressedSize <= yamlLimit else { throw invalid("仓鼠配置超过 512 KB。") }
            var content = Data()
            let checksum = try archive.extract(entry, bufferSize: 16_384) { chunk in
                guard content.count <= yamlLimit - chunk.count else { throw invalid("仓鼠配置展开后超过 512 KB。") }
                content.append(chunk)
            }
            guard UInt64(content.count) == entry.uncompressedSize, checksum == entry.checksum else {
                throw invalid("仓鼠配置大小或校验值不匹配，文件可能损坏。")
            }
            let converted = try inspectYAML(content, source: entry.path)
            result.layouts += converted.layouts
            result.details += converted.details
            result.warnings += converted.warnings
        }
        result.warnings = unique(result.warnings)
        return result
    }

    /// Screen the complete classic ZIP directory before the library iterator can omit an
    /// unsupported/encrypted entry. ZIP64 and multi-volume archives are deliberately unsupported.
    private static func checkZIPDirectory(_ data: Data) throws -> Int {
        guard (22...archiveLimit).contains(data.count) else { throw invalid("ZIP 为空、过大或已损坏。") }
        func u16(_ n: Int) -> Int { Int(data[n]) | Int(data[n + 1]) << 8 }
        func u32(_ n: Int) -> UInt64 { UInt64(u16(n)) | UInt64(u16(n + 2)) << 16 }
        let start = max(0, data.count - 65_557)
        guard let end = stride(from: data.count - 22, through: start, by: -1).first(where: {
            u32($0) == 0x06054b50 && $0 + 22 + u16($0 + 20) == data.count
        }) else { throw invalid("无法识别 ZIP 目录。") }
        let count = u16(end + 10), length = u32(end + 12), offset = u32(end + 16)
        guard u16(end + 4) == 0, u16(end + 6) == 0, u16(end + 8) == count,
              count <= entryLimit, count != 0xffff, offset != 0xffffffff, length != 0xffffffff,
              offset + length == UInt64(end) else {
            throw invalid("只支持最多 512 项的普通 ZIP；不支持分卷、ZIP64 或异常目录。")
        }
        var cursor = Int(offset)
        for _ in 0..<count {
            guard cursor + 46 <= end, u32(cursor) == 0x02014b50 else { throw invalid("ZIP 条目目录不完整。") }
            guard u16(cursor + 8) & 1 == 0 else { throw invalid("不支持加密 ZIP，请使用未加密的布局包。") }
            let recordLength = 46 + u16(cursor + 28) + u16(cursor + 30) + u16(cursor + 32)
            guard recordLength <= end - cursor else { throw invalid("ZIP 条目长度错误。") }
            cursor += recordLength
        }
        guard cursor == end else { throw invalid("ZIP 条目数量与目录不一致。") }
        return count
    }

    private static func checkPath(_ path: String) throws {
        let parts = path.split(separator: "/", omittingEmptySubsequences: false)
        guard !path.isEmpty, path.utf8.count <= 1_024, !path.hasPrefix("/"), !path.contains("\\"),
              !path.contains(":"), !path.unicodeScalars.contains(where: CharacterSet.controlCharacters.contains),
              !parts.contains(".."), !parts.contains("."),
              !parts.dropLast().contains("") else { throw invalid("ZIP 含有不安全或无法识别的文件路径。") }
    }

    private static func inspectYAML(_ data: Data, source: String) throws -> CustomLayoutImportResult {
        guard data.count <= yamlLimit, let text = String(data: data, encoding: .utf8) else {
            throw invalid("仓鼠配置须为不超过 512 KB 的 UTF-8 YAML。")
        }
        try checkYAMLComplexity(text)
        let root: Node
        do {
            guard let node = try Yams.compose(yaml: text) else { throw invalid("YAML 配置为空。") }
            root = node
        } catch { throw invalid("YAML 解析失败，请检查格式、引用及是否包含多个文档。") }
        var budget = 20_000
        try checkNode(root, depth: 0, budget: &budget)
        let keyboards = root["keyboards"] ?? root["patch"]?["keyboards"]
        var result = CustomLayoutImportResult(layouts: [], title: "仓鼠布局检查", details: [], warnings: [])
        if root["patch"] != nil {
            result.warnings.append("\(source)：全局偏好与方案补丁未应用，只读取显式 keyboards。")
        }
        guard let keyboards else {
            result.details.append("\(source)：未包含可直接读取的 keyboards 列表。")
            return result
        }
        guard let candidates = keyboards.sequence, candidates.count <= 12 else {
            throw invalid("keyboards 必须是最多 12 套布局的列表；本版不展开外部引用。")
        }
        for (index, candidate) in candidates.enumerated() {
            let name = candidate["name"]?.string?.trimmingCharacters(in: .whitespacesAndNewlines) ?? "布局 \(index + 1)"
            guard !name.isEmpty, name.count <= 60 else { throw invalid("仓鼠布局名称须为 1–60 个字符。") }
            do {
                if let layout = try convert(candidate, name: name, warnings: &result.warnings) {
                    result.layouts.append(layout)
                    result.details.append("\(source)：已转换「\(name)」的普通 26 键主布局，保存为新草稿。")
                } else {
                    result.details.append("\(source)：识别到「\(name)」，它不是完整 26 字母主布局，本版未转换。")
                }
            } catch {
                result.warnings.append("「\(name)」未转换：\(error.localizedDescription)")
            }
        }
        result.warnings.append("仅转换布局；输入方案、词库、Lua、配色、显示标签和全局设置不会被安装。")
        result.warnings = unique(result.warnings)
        return result
    }

    private struct PendingKey {
        var key: CustomKeyboardKey?
        var percentage: Double?
    }

    private static func convert(_ keyboard: Node, name: String, warnings: inout [String]) throws -> CustomKeyboardLayout? {
        guard let sourceRows = keyboard["rows"]?.sequence, (1...6).contains(sourceRows.count) else {
            throw invalid("布局须有 1–6 行。")
        }
        var rows = [[CustomKeyboardKey]](), swipes = 0, callouts = 0, margins = 0
        var orientation = false, layer = false, unhandled = Set<String>()
        for (rowIndex, row) in sourceRows.enumerated() {
            guard let keys = row["keys"]?.sequence, (1...16).contains(keys.count) else {
                throw invalid("第 \(rowIndex + 1) 行须有 1–16 个键。")
            }
            var pending = [PendingKey](), landscapeSum = 0.0
            for node in keys {
                swipes += node["swipe"]?.sequence?.count ?? 0
                callouts += node["callout"]?.sequence?.count ?? 0
                let widthNode = node["width"]
                if widthNode?["landscape"] != nil {
                    orientation = true
                    landscapeSum += widthNode?["landscape"]?["percentage"]?.float ?? 0
                }
                let width = try percentage(widthNode?["portrait"] ?? widthNode)
                let action = node["action"]
                var key: CustomKeyboardKey?
                if let char = action?["character"]?["char"]?.string {
                    guard char.count == 1, !char.unicodeScalars.contains(where: {
                        CharacterSet.whitespacesAndNewlines.contains($0) || CharacterSet.controlCharacters.contains($0)
                    }) else { throw invalid("仅支持单个可见字符键，不支持文本宏或共键字符组。") }
                    key = .init(text: char.lowercased())
                } else if action?["characterMargin"] != nil {
                    margins += 1
                } else if let value = action?["keyboardType"]?.string {
                    if ["123", "numeric", "numericNineGrid"].contains(value) {
                        key = .init(action: .numbers); layer = true
                    } else { unhandled.insert("键盘层 \(value)") }
                } else {
                    let actions: [String: CustomKeyAction] = ["shift": .shift, "backspace": .backspace,
                                                            "space": .space, "enter": .enter]
                    if let value = action?.string, let mapped = actions[value] { key = .init(action: mapped) }
                    else { unhandled.insert(action?.string ?? "自定义功能动作") }
                }
                pending.append(.init(key: key, percentage: width))
            }
            if landscapeSum > 1.0001 { warnings.append("「\(name)」第 \(rowIndex + 1) 行横屏宽度超过 100%，未导入该横屏配置。") }
            let fixed = pending.compactMap(\.percentage).reduce(0, +)
            let flexible = pending.filter { $0.percentage == nil }.count
            guard fixed <= 1.0001, flexible == 0 || fixed < 1 else {
                throw invalid("第 \(rowIndex + 1) 行宽度超过可用空间。")
            }
            let available = flexible > 0 ? (1 - fixed) / Double(flexible) : 0
            let converted = pending.compactMap { value -> CustomKeyboardKey? in
                guard var key = value.key else { return nil }
                key.width = value.percentage ?? available
                return key
            }
            guard !converted.isEmpty else { throw invalid("第 \(rowIndex + 1) 行没有可转换的按键。") }
            rows.append(converted)
        }
        let flat = rows.flatMap { $0 }
        let letters = flat.filter { $0.action == .character }.map(\.text)
        let alphabet = "abcdefghijklmnopqrstuvwxyz".map(String.init)
        guard alphabet.allSatisfy({ letter in letters.filter { $0 == letter }.count == 1 }) else { return nil }
        guard flat.count <= 80, let low = flat.map(\.width).min(), let high = flat.map(\.width).max(),
              low > 0, high / low <= 8 else { throw invalid("键宽比例超出编辑器支持范围。") }
        let scale = min(10, 4 / high)
        guard low * scale >= 0.5 - 0.000001 else { throw invalid("最窄键超出编辑器支持范围。") }
        for rowIndex in rows.indices {
            for keyIndex in rows[rowIndex].indices {
                rows[rowIndex][keyIndex].width = max(0.5, min(4, rows[rowIndex][keyIndex].width * scale))
            }
        }
        var metrics = CustomKeyboardMetrics(stagger: margins > 0)
        if let insets = keyboard["buttonInsets"] {
            let horizontal = insets.float.map { $0 * 2 } ?? ((insets["left"]?.float ?? 0) + (insets["right"]?.float ?? 0))
            guard horizontal.isFinite, (0...12).contains(horizontal) else { throw invalid("键间距超出 0–12 pt。") }
            metrics.gap = horizontal
            warnings.append("「\(name)」的左右内边距已近似转换成键间距；键高、外边距及纵向间距使用 \(ProductBrand.name) 编辑器规则。")
        }
        if swipes > 0 || callouts > 0 { warnings.append("「\(name)」的 \(swipes) 项滑动、\(callouts) 项长按菜单尚未导入，请勿依赖原快捷动作。") }
        if margins > 0 { warnings.append("「\(name)」的 \(margins) 个 characterMargin 边缘动作未保留，使用普通错行居中预览。") }
        if orientation { warnings.append("「\(name)」只采用竖屏宽度；独立横屏参数未导入，横屏将使用 \(ProductBrand.name) 自适应布局。") }
        if layer { warnings.append("「\(name)」的数字切换键连接 \(ProductBrand.name) 内置数字层，包内数字键盘没有一起应用。") }
        if !unhandled.isEmpty { warnings.append("「\(name)」未支持的点击动作：\(unhandled.sorted().joined(separator: "、"))。") }
        let layout = CustomKeyboardLayout(name: name, rows: rows, metrics: metrics)
        do { _ = try layout.validated() }
        catch { warnings.append("「\(name)」须在编辑器补齐后才能应用：\(error.localizedDescription)") }
        return layout
    }

    private static func percentage(_ node: Node?) throws -> Double? {
        guard let node else { return nil }
        if node.string == "available" { return nil }
        guard let value = node["percentage"]?.float, value.isFinite, value > 0, value <= 1 else {
            throw invalid("本版只支持 percentage 和 available 键宽。")
        }
        return value
    }

    private static func checkNode(_ node: Node, depth: Int, budget: inout Int) throws {
        budget -= 1
        guard budget >= 0, depth <= 32 else { throw invalid("YAML 层级或引用展开过于复杂。") }
        let allowed: Set<String> = ["tag:yaml.org,2002:str", "tag:yaml.org,2002:map", "tag:yaml.org,2002:seq",
                                    "tag:yaml.org,2002:bool", "tag:yaml.org,2002:int", "tag:yaml.org,2002:float",
                                    "tag:yaml.org,2002:null", "tag:yaml.org,2002:value"]
        guard allowed.contains(node.tag.description) else { throw invalid("YAML 含不支持的标签类型。") }
        switch node {
        case .scalar: break
        case .sequence(let nodes):
            for value in nodes { try checkNode(value, depth: depth + 1, budget: &budget) }
        case .mapping(let pairs):
            var names = Set<String>()
            for (key, value) in pairs {
                guard case .scalar(let scalar) = key, names.insert(scalar.string).inserted else {
                    throw invalid("YAML 键名重复或不是字符串。")
                }
                try checkNode(key, depth: depth + 1, budget: &budget)
                try checkNode(value, depth: depth + 1, budget: &budget)
            }
        }
    }

    /// Bound nesting before Yams' recursive composer. This importer accepts block YAML and
    /// single-line flow values, not multiline quotes/flow collections. Quotes within plain
    /// scalars (e.g. don't) must not hide later collection delimiters from this screen.
    private static func checkYAMLComplexity(_ text: String) throws {
        let lines = text.split(separator: "\n", omittingEmptySubsequences: false)
        guard lines.count <= 10_000 else { throw invalid("YAML 行数超过限制。") }
        for line in lines {
            guard line.utf8.count <= 16_384, line.prefix(while: { $0 == " " }).count <= 96 else {
                throw invalid("YAML 缩进或单行长度超过限制。")
            }
            let characters = Array(line)
            var collections = [Character](), quote: Character?, plain = false, index = 0, inlineBlocks = 0
            while index < characters.count {
                let character = characters[index]
                let next: Character? = index + 1 < characters.count ? characters[index + 1] : nil
                defer { index += 1 }
                if let current = quote {
                    if character == "\\", current == "\"", next != nil { index += 1 }
                    else if character == "'", current == "'", next == "'" { index += 1 }
                    else if character == current { quote = nil; plain = true }
                    continue
                }
                if character == "#", index == 0 || characters[index - 1].isWhitespace { break }
                if character == "\"" || character == "'" {
                    guard !plain else { throw invalid("含引号的 YAML 文本请整体加引号；本版不支持引号混入未加引号的文本。") }
                    quote = character; continue
                }
                if character == "[" || character == "{" {
                    collections.append(character); plain = false
                } else if character == "]" || character == "}" {
                    if let opener = collections.last {
                        guard (opener == "[" && character == "]") || (opener == "{" && character == "}") else {
                            throw invalid("YAML 集合括号不匹配。")
                        }
                        collections.removeLast()
                    }
                    plain = true
                } else if character == ":", next == nil || next!.isWhitespace || ",[]{}".contains(next!) {
                    plain = false
                } else if character == ",", !collections.isEmpty {
                    plain = false
                } else if !plain && (character == "-" || character == "?"), next?.isWhitespace == true {
                    inlineBlocks += 1
                } else if !character.isWhitespace { plain = true }
                guard collections.count <= 32, inlineBlocks <= 32 else { throw invalid("YAML 嵌套超过 32 层。") }
            }
            guard quote == nil, collections.isEmpty else {
                throw invalid("本版只支持单行引号和单行方括号／花括号写法；请将跨行集合改为缩进式 YAML。")
            }
        }
    }

    private static func unique(_ values: [String]) -> [String] {
        var seen = Set<String>()
        return values.filter { seen.insert($0).inserted }
    }
    private static func invalid(_ text: String) -> CustomLayoutImportError { .invalid(text) }
}
