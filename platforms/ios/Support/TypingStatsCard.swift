import UIKit
import ImageIO
import UniformTypeIdentifiers
import RimesCore

/// Point-in-time aggregates; never includes the Buffer or host document.
struct TypingCardSnapshot: Equatable {
    let characters: Int
    let average: Double?
    let peak: Double?
    let codeLength: Double?
    let keysPerSecond: Double?
    init(characters: Int, average: Double?, peak: Double?, codeLength: Double?, keysPerSecond: Double?) {
        self.characters = max(0, characters); self.average = average; self.peak = peak
        self.codeLength = codeLength; self.keysPerSecond = keysPerSecond
    }
    init?(session: TypingSessionTotals) {
        guard !session.isEmpty else { return nil }
        self.init(characters: session.characters, average: session.charactersPerMinute,
                  peak: session.peakCharactersPerMinute > 0 ? session.peakCharactersPerMinute : nil,
                  codeLength: session.codeLength, keysPerSecond: session.keysPerSecond)
    }
    static func number(_ value: Double?, decimals: Int) -> String {
        guard let value, value.isFinite, value >= 0 else { return "—" }
        if value >= 1e9 { return String(format: "%.2g", locale: Locale(identifier: "en_US_POSIX"), value) }
        return String(format: "%.*f", locale: Locale(identifier: "en_US_POSIX"), decimals, value)
    }
}

enum TypingCardError: LocalizedError {
    case render, unavailable, invalidImage
    var errorDescription: String? {
        switch self {
        case .render: return L("图片生成失败，请重试。", "Could not generate the image. Try again.")
        case .unavailable: return L("无法访问共享目录，请确认已开启完全访问。", "The shared folder is unavailable. Check Full Access.")
        case .invalidImage: return L("统计图片无法读取，请从键盘重新生成。", "Could not read the card. Generate it again in the keyboard.")
        }
    }
}

/// Each row is eight Emoji cells. Long values continue downwards, keeping the frame narrow.
enum TypingStatsText {
    static func matrix(_ stats: TypingCardSnapshot, chinese: Bool = L("zh", "en") == "zh") -> String {
        let digits = ["0️⃣", "1️⃣", "2️⃣", "3️⃣", "4️⃣", "5️⃣", "6️⃣", "7️⃣", "8️⃣", "9️⃣"]
        func number(_ value: Double?, decimals: Int) -> String {
            guard let value, value.isFinite, value >= 0 else { return "—" }
            return String(format: "%.*f", locale: Locale(identifier: "en_US_POSIX"), decimals, value)
        }
        var rows = [["🆓", "⌨️", "⬛", "⬛", "⬛", "⬛"]]
        for (icon, value) in [("📝", String(stats.characters)), ("📊", number(stats.average, decimals: 0)),
                              ("🚀", number(stats.peak, decimals: 0)), ("⌨️", number(stats.codeLength, decimals: 2)),
                              ("⏱️", number(stats.keysPerSecond, decimals: 1))] {
            let cells = value.map { character -> String in
                if let digit = character.wholeNumberValue, (0...9).contains(digit) { return digits[digit] }
                return character == "." ? "⏺️" : "➖"
            }
            for start in stride(from: 0, to: cells.count, by: 5) {
                var row = [start == 0 ? icon : "↪️"] + Array(cells[start..<min(start + 5, cells.count)])
                row += Array(repeating: "⬛", count: 6 - row.count); rows.append(row)
            }
        }
        let edge = String(repeating: "⬜", count: 8)
        let matrix = ([edge] + rows.map { "⬜" + $0.joined() + "⬜" } + [edge]).joined(separator: "\n")
        let title = chinese ? "\(ProductBrand.name) · 免费开源输入法" : "\(ProductBrand.name) · Free & open source"
        let legend = chinese ? "📝字数 · 📊均速 · 🚀峰速（字/分）\n⌨️码长（键/字）· ⏱️击键（键/秒）"
            : "📝characters · 📊avg · 🚀peak (cpm)\n⌨️keys/char · ⏱️keys/sec"
        return title + "\n" + matrix + "\n" + legend + "\nhttps://github.com/scholay/xsime"
    }
}

@MainActor enum TypingStatsCard {
    static let github = "https://github.com/scholay/xsime"
    static func render(_ stats: TypingCardSnapshot, chinese: Bool = L("zh", "en") == "zh") throws -> Data {
        let format = UIGraphicsImageRendererFormat(); format.scale = 1; format.opaque = true
        let renderer = UIGraphicsImageRenderer(size: CGSize(width: 1024, height: 1024), format: format)
        let ink = UIColor(red: 0.16, green: 0.17, blue: 0.17, alpha: 1)
        let ivory = UIColor(red: 0.97, green: 0.96, blue: 0.93, alpha: 1)
        let gray = UIColor(red: 0.88, green: 0.89, blue: 0.88, alpha: 1)
        let orange = UIColor(red: 1, green: 0.49, blue: 0.24, alpha: 1)
        let image = renderer.image { context in
            let cg = context.cgContext
            ivory.setFill(); cg.fill(CGRect(x: 0, y: 0, width: 1024, height: 1024))
            // Eight columns and eight rows; metric blocks span whole cells.
            func block(_ column: Int, _ row: Int, _ width: Int, _ height: Int, _ color: UIColor) -> CGRect {
                let rect = CGRect(x: 32 + column * 120, y: 32 + row * 120, width: width * 120, height: height * 120)
                color.setFill(); cg.fill(rect); ink.setStroke(); cg.setLineWidth(3); cg.stroke(rect)
                return rect
            }
            func text(_ value: String, in rect: CGRect, size: CGFloat, weight: UIFont.Weight = .regular,
                      color: UIColor = ink, alignment: NSTextAlignment = .left, numeric: Bool = false) {
                var font = numeric ? UIFont.monospacedDigitSystemFont(ofSize: size, weight: weight) : UIFont.systemFont(ofSize: size, weight: weight)
                let measured = (value as NSString).size(withAttributes: [.font: font]).width
                if measured > rect.width {
                    let fitted = max(8, size * rect.width / measured)
                    font = numeric ? .monospacedDigitSystemFont(ofSize: fitted, weight: weight) : .systemFont(ofSize: fitted, weight: weight)
                }
                let style = NSMutableParagraphStyle(); style.alignment = alignment; style.lineBreakMode = .byClipping
                (value as NSString).draw(in: CGRect(x: rect.minX, y: rect.midY - font.lineHeight / 2, width: rect.width, height: font.lineHeight + 2),
                                        withAttributes: [.font: font, .foregroundColor: color, .paragraphStyle: style])
            }
            let header = block(0, 0, 8, 1, ink)
            text("\(ProductBrand.name)", in: CGRect(x: header.minX + 28, y: header.minY, width: 300, height: 120), size: 54, weight: .bold, color: ivory)
            text(chinese ? "免费开源输入法" : "FREE & OPEN SOURCE", in: CGRect(x: 398, y: 32, width: 566, height: 120), size: 28, weight: .medium, color: ivory, alignment: .right)
            func metric(_ rect: CGRect, _ value: String, _ label: String, _ unit: String, size: CGFloat) {
                let inner = rect.insetBy(dx: 26, dy: 0)
                text(label, in: CGRect(x: inner.minX, y: rect.minY + 22, width: inner.width, height: 48), size: 28, weight: .medium)
                text(value, in: CGRect(x: inner.minX, y: rect.minY + 84, width: inner.width, height: 166), size: size, weight: .semibold, numeric: true)
                text(unit, in: CGRect(x: inner.minX, y: rect.maxY - 78, width: inner.width, height: 48), size: 25)
            }
            metric(block(0, 1, 4, 3, orange), String(stats.characters), chinese ? "总字数" : "CHARACTERS", chinese ? "字" : "characters", size: 140)
            metric(block(4, 1, 4, 3, ivory), TypingCardSnapshot.number(stats.average, decimals: 0), chinese ? "平均速度" : "AVERAGE", chinese ? "字 / 分钟" : "characters / min", size: 140)
            metric(block(0, 4, 3, 3, ivory), TypingCardSnapshot.number(stats.peak, decimals: 0), chinese ? "峰值速度" : "PEAK", chinese ? "字 / 分钟" : "characters / min", size: 106)
            metric(block(3, 4, 3, 3, gray), TypingCardSnapshot.number(stats.codeLength, decimals: 2), chinese ? "平均码长" : "KEYS / CHAR", chinese ? "键 / 字" : "keys / character", size: 106)
            metric(block(6, 4, 2, 3, orange), TypingCardSnapshot.number(stats.keysPerSecond, decimals: 1), chinese ? "击键频率" : "KEY RATE", chinese ? "键 / 秒" : "keys / sec", size: 90)
            let footer = block(0, 7, 8, 1, ivory)
            text("github.com/scholay/xsime", in: footer.insetBy(dx: 28, dy: 0), size: 44, weight: .medium)
            ink.setStroke(); cg.setLineWidth(6); cg.stroke(CGRect(x: 32, y: 32, width: 960, height: 960))
        }
        guard let data = image.pngData() else { throw TypingCardError.render }; return data
    }
    static func copy(_ png: Data, to pasteboard: UIPasteboard = .general) throws {
        try TypingCardStore.validate(png)
        // A PNG item only: mixing in a URL makes some hosts paste text instead.
        pasteboard.setItems([[UTType.png.identifier: png]], options: [.localOnly: true])
    }
}

struct TypingCardStore {
    var directory: URL?
    private func fileURL() throws -> URL {
        let root: URL
        if let directory { root = directory }
        else {
            guard let group = FileManager.default.containerURL(forSecurityApplicationGroupIdentifier: ConfigurationStore.groupID) else { throw TypingCardError.unavailable }
            root = group.appendingPathComponent("Library/Application Support/TypingCards", isDirectory: true)
        }
        return root.appendingPathComponent("latest.png")
    }
    static func validate(_ data: Data) throws {
        guard data.count <= 8 * 1024 * 1024, let source = CGImageSourceCreateWithData(data as CFData, nil),
              CGImageSourceGetType(source) as String? == UTType.png.identifier,
              let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any],
              properties[kCGImagePropertyPixelWidth] as? Int == 1024, properties[kCGImagePropertyPixelHeight] as? Int == 1024,
              CGImageSourceCreateImageAtIndex(source, 0, nil) != nil else { throw TypingCardError.invalidImage }
    }
    func save(_ data: Data) throws {
        try Self.validate(data); let file = try fileURL(); var folder = file.deletingLastPathComponent()
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        var values = URLResourceValues(); values.isExcludedFromBackup = true; try folder.setResourceValues(values)
        try data.write(to: file, options: [.atomic, .completeFileProtectionUntilFirstUserAuthentication])
    }
    func load() throws -> Data? {
        let file = try fileURL(); guard FileManager.default.fileExists(atPath: file.path) else { return nil }
        guard (try file.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0) <= 8 * 1024 * 1024 else { throw TypingCardError.invalidImage }
        let data = try Data(contentsOf: file); try Self.validate(data); return data
    }
    func remove() throws { let file = try fileURL(); if FileManager.default.fileExists(atPath: file.path) { try FileManager.default.removeItem(at: file) } }
}
