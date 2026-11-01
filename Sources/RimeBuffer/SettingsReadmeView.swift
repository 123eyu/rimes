import AppKit

/// The project README as a native settings page: the full text of the README
/// shipped inside the app, in each language it is published in, rendered from
/// Markdown without a web view. Links open in the browser.
final class SettingsReadmeView: NSStackView {
    struct Language {
        let code: String       // README suffix: "" for the Chinese original
        let title: String
        var resource: String { code.isEmpty ? "README" : "README.\(code)" }
    }
    static let languages = [
        Language(code: "", title: "中文"), Language(code: "en", title: "English"),
        Language(code: "ja", title: "日本語"), Language(code: "ko", title: "한국어"),
        Language(code: "es", title: "Español"),
    ]
    static let repositoryBase = "https://github.com/scholay-xsime/xsime/blob/main/"

    private let body = NSStackView()
    private let picker = NSSegmentedControl()
    private(set) var selectedLanguage = 0

    init(language: Int? = nil) {
        super.init(frame: .zero)
        orientation = .vertical
        alignment = .leading
        spacing = 10
        selectedLanguage = language ?? Self.defaultLanguage()
        picker.segmentCount = Self.languages.count
        for (index, language) in Self.languages.enumerated() { picker.setLabel(language.title, forSegment: index) }
        picker.segmentStyle = .rounded
        picker.controlSize = .small
        picker.trackingMode = .selectOne
        picker.target = self
        picker.action = #selector(languageChosen)
        picker.selectedSegment = selectedLanguage
        picker.setAccessibilityLabel("README 语言")
        body.orientation = .vertical
        body.alignment = .leading
        body.spacing = 8
        addArrangedSubview(picker)
        addArrangedSubview(body)
        identifier = .init("settings.rimes.readme")
        render()
    }
    required init?(coder: NSCoder) { nil }

    /// The README in the user's own language when one exists, otherwise the Chinese original.
    static func defaultLanguage() -> Int {
        for preferred in Locale.preferredLanguages {
            let code = String(preferred.prefix(2))
            if let index = languages.firstIndex(where: { $0.code == code }) { return index }
            if code == "zh" { return 0 }
        }
        return 0
    }

    @objc private func languageChosen() {
        selectedLanguage = max(0, picker.selectedSegment)
        render()
    }

    private func render() {
        body.arrangedSubviews.forEach { $0.removeFromSuperview() }
        let text = Self.readmeText(Self.languages[selectedLanguage])
        for block in Self.blocks(from: text) {
            body.addArrangedSubview(block)
            if block is NSTextField { block.widthAnchor.constraint(lessThanOrEqualToConstant: 640).isActive = true }
        }
    }

    static func readmeText(_ language: Language) -> String {
        let candidates = [
            Bundle.main.url(forResource: language.resource, withExtension: "md"),
            URL(fileURLWithPath: FileManager.default.currentDirectoryPath).appendingPathComponent(language.resource + ".md"),
        ].compactMap { $0 }
        for url in candidates {
            if let text = try? String(contentsOf: url, encoding: .utf8), !text.isEmpty { return text }
        }
        return "未能读取随应用附带的 README。完整文档见 https://github.com/scholay-xsime/xsime"
    }

    // MARK: Markdown → views

    static func blocks(from markdown: String) -> [NSView] {
        var views: [NSView] = []
        var paragraph: [String] = []
        var lines = markdown.replacingOccurrences(of: "\r\n", with: "\n").components(separatedBy: "\n")[...]
        func flushParagraph() {
            guard !paragraph.isEmpty else { return }
            views.append(label(joined(paragraph), size: 13, color: RimeUI.textPrimary))
            paragraph = []
        }
        var skippedTitle = false
        while let raw = lines.popFirst() {
            let line = raw.trimmingCharacters(in: .whitespaces)
            if line.isEmpty { flushParagraph(); continue }
            if line.hasPrefix("```") {
                flushParagraph()
                var code: [String] = []
                while let next = lines.popFirst(), !next.trimmingCharacters(in: .whitespaces).hasPrefix("```") { code.append(next) }
                views.append(codeBlock(code.joined(separator: "\n")))
                continue
            }
            if isImageOnly(line) || line.hasPrefix("<!--") || line == "---" || line == "***" { flushParagraph(); continue }
            if line.hasPrefix("#") {
                flushParagraph()
                let level = line.prefix { $0 == "#" }.count
                let title = String(line.drop { $0 == "#" }).trimmingCharacters(in: .whitespaces)
                // The page already shows the product name; skip the document's own H1.
                if level == 1, !skippedTitle { skippedTitle = true; continue }
                views.append(spaceView(level <= 2 ? 10 : 4))
                views.append(label(title, size: level <= 2 ? 15 : 13, color: level <= 2 ? RimeUI.textPrimary : RimeUI.textSecondary,
                                   weight: .semibold, inline: false))
                continue
            }
            if line.hasPrefix("|") {
                flushParagraph()
                var table = [line]
                while let next = lines.first, next.trimmingCharacters(in: .whitespaces).hasPrefix("|") {
                    table.append(next.trimmingCharacters(in: .whitespaces)); lines.removeFirst()
                }
                views.append(contentsOf: tableViews(table))
                continue
            }
            if let item = listItem(line) {
                flushParagraph()
                views.append(label(item.marker + "\t" + item.text, size: 13, color: RimeUI.textPrimary, indent: 20))
                continue
            }
            if line.hasPrefix(">") {
                flushParagraph()
                var quote = [String(line.drop { $0 == ">" }).trimmingCharacters(in: .whitespaces)]
                while let next = lines.first, next.trimmingCharacters(in: .whitespaces).hasPrefix(">") {
                    quote.append(String(next.trimmingCharacters(in: .whitespaces).drop { $0 == ">" }).trimmingCharacters(in: .whitespaces))
                    lines.removeFirst()
                }
                views.append(label(joined(quote.filter { !$0.isEmpty }), size: 12, color: RimeUI.textSecondary, indent: 10))
                continue
            }
            paragraph.append(line)
        }
        flushParagraph()
        return views
    }

    private static func isImageOnly(_ line: String) -> Bool {
        var rest = line
        while let range = rest.range(of: #"\[?!\[[^\]]*\]\([^)]*\)(\]\([^)]*\))?"#, options: .regularExpression) {
            rest.removeSubrange(range)
        }
        return rest.trimmingCharacters(in: .whitespaces).isEmpty && line.contains("![")
    }

    private static func listItem(_ line: String) -> (marker: String, text: String)? {
        if line.hasPrefix("- ") || line.hasPrefix("* ") { return ("•", String(line.dropFirst(2))) }
        if let range = line.range(of: #"^\d+[.)]\s+"#, options: .regularExpression) {
            return (String(line[range]).trimmingCharacters(in: .whitespaces), String(line[range.upperBound...]))
        }
        return nil
    }

    /// Soft line breaks join: without a space between ideographs, with one elsewhere.
    private static func joined(_ lines: [String]) -> String {
        var out = ""
        for line in lines {
            if let last = out.unicodeScalars.last, let first = line.unicodeScalars.first,
               !(isWide(last) || isWide(first)) { out += " " }
            out += line
        }
        return out
    }
    private static func isWide(_ scalar: Unicode.Scalar) -> Bool {
        (0x2E80...0x9FFF).contains(scalar.value) || (0xAC00...0xD7AF).contains(scalar.value)
            || (0xFF00...0xFFEF).contains(scalar.value) || (0x3000...0x303F).contains(scalar.value)
    }

    /// A table becomes one block per row: the first cell is the row's name and each
    /// other cell follows under its column heading.
    private static func tableViews(_ rows: [String]) -> [NSView] {
        func cells(_ row: String) -> [String] {
            var parts = row.split(separator: "|", omittingEmptySubsequences: false).map { $0.trimmingCharacters(in: .whitespaces) }
            if parts.first == "" { parts.removeFirst() }
            if parts.last == "" { parts.removeLast() }
            return parts
        }
        guard rows.count >= 2 else { return [] }
        let header = cells(rows[0])
        let data = rows.dropFirst(2).map(cells)
        var views: [NSView] = []
        for row in data where !row.isEmpty {
            let name = row[0]
            let rest = row.dropFirst().enumerated().compactMap { index, cell -> String? in
                let value = cell.trimmingCharacters(in: .whitespaces)
                if value.isEmpty || value == "—" || value == "-" { return nil }
                return header.indices.contains(index + 1) && header.count > 2 ? "\(header[index + 1])：\(value)" : value
            }
            var text = "**\(name)**"
            if header.count == 2, let value = rest.first { text += "　" + value }
            else if !rest.isEmpty { text += "\n" + rest.joined(separator: "\n") }
            let card = label(text, size: 12.5, color: RimeUI.textPrimary, inset: true)
            views.append(card)
        }
        return views
    }

    // MARK: Pieces

    private static func spaceView(_ height: CGFloat) -> NSView {
        let view = NSView()
        view.translatesAutoresizingMaskIntoConstraints = false
        view.heightAnchor.constraint(equalToConstant: height).isActive = true
        return view
    }

    private static func codeBlock(_ code: String) -> NSView {
        let field = NSTextField(wrappingLabelWithString: code)
        field.font = .monospacedSystemFont(ofSize: 11.5, weight: .regular)
        field.textColor = RimeUI.textSecondary
        field.isSelectable = true
        field.preferredMaxLayoutWidth = 600
        return field
    }

    /// A wrapping, selectable label whose Markdown links open in the browser.
    static func label(_ markdown: String, size: CGFloat, color: NSColor, weight: NSFont.Weight = .regular,
                      indent: CGFloat = 0, inset: Bool = false, inline: Bool = true) -> NSTextField {
        let field = NSTextField(labelWithAttributedString: attributed(markdown, size: size, color: color, weight: weight,
                                                                      indent: indent, inline: inline))
        field.lineBreakMode = .byWordWrapping
        field.maximumNumberOfLines = 0
        field.isSelectable = true
        field.allowsEditingTextAttributes = true
        field.preferredMaxLayoutWidth = 600
        field.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        return field
    }

    static func attributed(_ markdown: String, size: CGFloat, color: NSColor, weight: NSFont.Weight = .regular,
                           indent: CGFloat = 0, inline: Bool = true) -> NSAttributedString {
        let base = NSFont.systemFont(ofSize: size, weight: weight)
        let style = NSMutableParagraphStyle()
        style.lineSpacing = 3
        style.lineBreakMode = .byWordWrapping
        if indent > 0 {
            style.firstLineHeadIndent = 0
            style.headIndent = indent
            style.tabStops = [NSTextTab(textAlignment: .left, location: indent)]
        }
        guard inline else {
            return NSAttributedString(string: markdown, attributes: [.font: base, .foregroundColor: color, .paragraphStyle: style])
        }
        var text = markdown.replacingOccurrences(of: #"<[^>]+>"#, with: "", options: .regularExpression)
        // Links to files in the repository open their page on GitHub.
        text = text.replacingOccurrences(of: #"\]\((?!https?:|mailto:|#)([^)\s]+)\)"#,
                                         with: "](\(repositoryBase)$1)", options: .regularExpression)
        var attributedText: AttributedString
        do {
            attributedText = try AttributedString(markdown: text, options: .init(
                interpretedSyntax: .inlineOnlyPreservingWhitespace, failurePolicy: .returnPartiallyParsedIfPossible))
        } catch { attributedText = AttributedString(text) }
        for run in attributedText.runs {
            let intent = run.inlinePresentationIntent ?? []
            var font = base
            if intent.contains(.code) { font = .monospacedSystemFont(ofSize: size - 0.5, weight: .regular) }
            else if intent.contains(.stronglyEmphasized) { font = .systemFont(ofSize: size, weight: .semibold) }
            attributedText[run.range].font = font
            if run.link != nil {
                attributedText[run.range].foregroundColor = RimeUI.accentTextColor
                attributedText[run.range].underlineStyle = .single
            } else {
                attributedText[run.range].foregroundColor = color
            }
        }
        let result = NSMutableAttributedString(attributedText)
        result.addAttribute(.paragraphStyle, value: style, range: NSRange(location: 0, length: result.length))
        return result
    }
}
