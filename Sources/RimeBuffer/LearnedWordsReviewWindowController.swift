import AppKit

/// Read-only review of learned words, grouped by the day they were learned.
/// Step 1 of docs/plans/learned-words-review.md: it shows, it never deletes.
final class LearnedWordsReviewWindowController: NSObject, NSWindowDelegate,
                                                NSTableViewDataSource, NSTableViewDelegate {
    static let shared = LearnedWordsReviewWindowController(ledger: .shared)

    enum Section: Equatable {
        case day(String)
        case all
    }

    enum Filter: Int, CaseIterable {
        case everything
        case usedOnce
        case longPhrases

        var title: String {
            switch self {
            case .everything: return "全部"
            case .usedOnce: return "只用过一次"
            case .longPhrases: return "5 字以上"
            }
        }
    }

    struct SectionRow: Equatable {
        let section: Section
        let title: String
        let detail: String
    }

    struct WordRow: Equatable {
        let word: String
        let code: String
        let count: Int
        let change: String
        let isNew: Bool
    }

    private let ledger: LearnedWordsLedger
    private let preferences: LearnedWordsReviewPreferences
    private(set) var window: NSWindow?
    private(set) var kind: UserLexiconKind = .chinese
    private(set) var sections: [SectionRow] = []
    private(set) var rows: [WordRow] = []
    private(set) var filter: Filter = .everything
    private var allRows: [WordRow] = []
    private var days: [LearnedWordsDay] = []
    private var observers: [NSObjectProtocol] = []

    private let dictionaryPopUp = NSPopUpButton()
    private let recordSwitch = NSSwitch()
    private let recordLabel = NSTextField(labelWithString: "每天记录新学的词（只保存在本机）")
    private let captureButton = NSButton(title: "立即记录", target: nil, action: nil)
    private let statusLabel = NSTextField(labelWithString: "")
    private let filterControl = NSSegmentedControl()
    private let countLabel = NSTextField(labelWithString: "")
    private let emptyLabel = NSTextField(wrappingLabelWithString: "")
    private let sectionTable = NSTableView()
    private let wordTable = NSTableView()

    init(ledger: LearnedWordsLedger,
         preferences: LearnedWordsReviewPreferences = .shared) {
        self.ledger = ledger
        self.preferences = preferences
        super.init()
    }

    deinit { observers.forEach(NotificationCenter.default.removeObserver) }

    func show(kind: UserLexiconKind) {
        if window == nil { build() }
        select(kind: kind)
        NSApp.activate(ignoringOtherApps: true)
        window?.makeKeyAndOrderFront(nil)
    }

    func select(kind: UserLexiconKind) {
        self.kind = kind
        if let index = UserLexiconKind.allCases.firstIndex(of: kind) {
            dictionaryPopUp.selectItem(at: index)
        }
        reload()
    }

    // MARK: Data

    func reload() {
        days = ledger.days(for: kind)
        let entries = ledger.currentEntries(for: kind)
        var built = days.map { day in
            SectionRow(section: .day(day.day),
                       title: Self.dayTitle(day.day, today: ledger.dayString(for: Date())),
                       detail: "新学 \(day.added.count) · 再用 \(day.reused.count)")
        }
        if !entries.isEmpty {
            built.append(SectionRow(section: .all, title: "全部学习词",
                                    detail: "\(entries.count.formatted()) 个"))
        }
        sections = built
        recordSwitch.state = preferences.recordsDaily ? .on : .off
        updateStatus()
        sectionTable.reloadData()
        if !sections.isEmpty {
            sectionTable.selectRowIndexes(IndexSet(integer: 0), byExtendingSelection: false)
        }
        showSelectedSection()
    }

    func showSection(at index: Int) {
        guard sections.indices.contains(index) else { return }
        sectionTable.selectRowIndexes(IndexSet(integer: index), byExtendingSelection: false)
        showSelectedSection()
    }

    func setFilter(_ filter: Filter) {
        self.filter = filter
        filterControl.selectedSegment = filter.rawValue
        applyFilter()
    }

    private func showSelectedSection() {
        let index = sectionTable.selectedRow
        guard sections.indices.contains(index) else {
            allRows = []
            applyFilter()
            return
        }
        switch sections[index].section {
        case .day(let name):
            guard let day = days.first(where: { $0.day == name }) else { allRows = []; break }
            let added = day.added.map {
                WordRow(word: $0.word, code: $0.code, count: $0.count, change: "新学", isNew: true)
            }
            let reused = day.reused.map {
                WordRow(word: $0.word, code: $0.code, count: $0.toCount,
                        change: "\($0.fromCount) → \($0.toCount)", isNew: false)
            }
            allRows = added.sorted(by: Self.byWord) + reused.sorted(by: Self.byWord)
        case .all:
            allRows = ledger.currentEntries(for: kind)
                .map { WordRow(word: $0.word, code: $0.code, count: $0.count, change: "", isNew: false) }
                .sorted { ($0.count, $0.word) < ($1.count, $1.word) }
        }
        applyFilter()
    }

    private func applyFilter() {
        switch filter {
        case .everything: rows = allRows
        case .usedOnce: rows = allRows.filter { $0.count == 1 }
        case .longPhrases: rows = allRows.filter { $0.word.count >= 5 }
        }
        if let descriptor = wordTable.sortDescriptors.first {
            rows = Self.sorted(rows, by: descriptor)
        }
        wordTable.reloadData()
        countLabel.stringValue = "\(rows.count.formatted()) 个词"
        emptyLabel.stringValue = emptyMessage()
        emptyLabel.isHidden = !rows.isEmpty
    }

    private func emptyMessage() -> String {
        if sections.isEmpty {
            return preferences.recordsDaily
                ? "还没有记录。点“立即记录”建立第一份记录；之后每天会自动比较一次。"
                : "打开上方的开关后，每天会把新学的词记下来。词语只保存在本机，不会发送到任何地方。"
        }
        if allRows.isEmpty { return "这一天没有新学或再用的词。" }
        return "没有符合筛选条件的词。"
    }

    private func updateStatus() {
        if let date = ledger.lastCapturedAt(for: kind) {
            statusLabel.stringValue = "上次记录：\(Self.timestampFormatter.string(from: date))"
        } else {
            statusLabel.stringValue = preferences.recordsDaily ? "尚未记录" : "未开启"
        }
        captureButton.isEnabled = preferences.recordsDaily
    }

    private static let timestampFormatter: DateFormatter = {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "zh_CN")
        formatter.dateFormat = "M 月 d 日 HH:mm"
        return formatter
    }()

    static func dayTitle(_ day: String, today: String) -> String {
        if day == today { return "今天" }
        let parts = day.split(separator: "-")
        guard parts.count == 3, let month = Int(parts[1]), let dayOfMonth = Int(parts[2]) else {
            return day
        }
        return "\(parts[0]) 年 \(month) 月 \(dayOfMonth) 日"
    }

    private static func byWord(_ lhs: WordRow, _ rhs: WordRow) -> Bool {
        lhs.word.localizedStandardCompare(rhs.word) == .orderedAscending
    }

    private static func sorted(_ rows: [WordRow], by descriptor: NSSortDescriptor) -> [WordRow] {
        let ascending = descriptor.ascending
        return rows.sorted { lhs, rhs in
            let result: ComparisonResult
            switch descriptor.key {
            case "code": result = lhs.code.localizedStandardCompare(rhs.code)
            case "count":
                result = lhs.count == rhs.count ? .orderedSame
                    : (lhs.count < rhs.count ? .orderedAscending : .orderedDescending)
            case "change": result = lhs.change.localizedStandardCompare(rhs.change)
            default: result = lhs.word.localizedStandardCompare(rhs.word)
            }
            return ascending ? result == .orderedAscending : result == .orderedDescending
        }
    }

    // MARK: Actions

    @objc private func dictionaryChanged(_ sender: NSPopUpButton) {
        let all = UserLexiconKind.allCases
        guard all.indices.contains(sender.indexOfSelectedItem) else { return }
        select(kind: all[sender.indexOfSelectedItem])
    }

    @objc private func recordSwitchChanged(_ sender: NSSwitch) {
        preferences.setRecordsDaily(sender.state == .on)
        if sender.state == .on { captureNow(nil) } else { reload() }
    }

    @objc func captureNow(_ sender: Any?) {
        do {
            let result = try ledger.capture(kind)
            IMELog.write("lexicon-review: manual capture \(kind.rawValue) \(result)")
        } catch {
            presentError(error)
        }
        reload()
    }

    @objc private func filterChanged(_ sender: NSSegmentedControl) {
        setFilter(Filter(rawValue: sender.selectedSegment) ?? .everything)
    }

    private func presentError(_ error: Error) {
        guard let window else { return }
        let alert = NSAlert()
        alert.messageText = "无法记录学习词"
        alert.informativeText = error.localizedDescription
        alert.alertStyle = .warning
        alert.beginSheetModal(for: window)
    }

    // MARK: Table

    func numberOfRows(in tableView: NSTableView) -> Int {
        tableView === sectionTable ? sections.count : rows.count
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int) -> NSView? {
        let identifier = tableColumn?.identifier ?? NSUserInterfaceItemIdentifier("section")
        let cell = tableView.makeView(withIdentifier: identifier, owner: self) as? NSTableCellView
            ?? makeCell(identifier: identifier)
        if tableView === sectionTable {
            let item = sections[row]
            cell.textField?.attributedStringValue = Self.sectionText(item)
        } else {
            let item = rows[row]
            switch identifier.rawValue {
            case "code": cell.textField?.stringValue = item.code
            case "count": cell.textField?.stringValue = "\(item.count)"
            case "change": cell.textField?.stringValue = item.change
            default: cell.textField?.stringValue = item.word
            }
            cell.textField?.textColor = identifier.rawValue == "change" && item.isNew
                ? RimeUI.accentTextColor : RimeUI.textPrimary
        }
        return cell
    }

    func tableView(_ tableView: NSTableView, heightOfRow row: Int) -> CGFloat {
        tableView === sectionTable ? 40 : 22
    }

    func tableViewSelectionDidChange(_ notification: Notification) {
        guard (notification.object as? NSTableView) === sectionTable else { return }
        showSelectedSection()
    }

    func tableView(_ tableView: NSTableView, sortDescriptorsDidChange oldDescriptors: [NSSortDescriptor]) {
        guard tableView === wordTable else { return }
        applyFilter()
    }

    private static func sectionText(_ item: SectionRow) -> NSAttributedString {
        let text = NSMutableAttributedString(
            string: item.title + "\n",
            attributes: [.font: NSFont.systemFont(ofSize: 12, weight: .semibold),
                         .foregroundColor: RimeUI.textPrimary]
        )
        text.append(NSAttributedString(
            string: item.detail,
            attributes: [.font: NSFont.systemFont(ofSize: 10),
                         .foregroundColor: RimeUI.textMuted]
        ))
        return text
    }

    private func makeCell(identifier: NSUserInterfaceItemIdentifier) -> NSTableCellView {
        let cell = NSTableCellView()
        cell.identifier = identifier
        let field = NSTextField(labelWithString: "")
        field.translatesAutoresizingMaskIntoConstraints = false
        field.lineBreakMode = .byTruncatingTail
        field.maximumNumberOfLines = identifier.rawValue == "section" ? 2 : 1
        field.font = .systemFont(ofSize: 12)
        cell.addSubview(field)
        cell.textField = field
        NSLayoutConstraint.activate([
            field.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 6),
            field.trailingAnchor.constraint(equalTo: cell.trailingAnchor, constant: -6),
            field.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
        ])
        return cell
    }

    // MARK: Build

    private func build() {
        let win = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 860, height: 560),
                           styleMask: [.titled, .closable, .resizable, .miniaturizable],
                           backing: .buffered, defer: false)
        win.title = "学习词回顾"
        win.isReleasedWhenClosed = false
        win.delegate = self
        win.minSize = NSSize(width: 680, height: 420)
        win.appearance = RimeUI.appKitAppearance
        win.backgroundColor = RimeUI.surface
        win.identifier = NSUserInterfaceItemIdentifier("lexicon-review.window")

        for kind in UserLexiconKind.allCases { dictionaryPopUp.addItem(withTitle: kind.displayName) }
        dictionaryPopUp.target = self
        dictionaryPopUp.action = #selector(dictionaryChanged(_:))
        dictionaryPopUp.controlSize = .small

        recordSwitch.target = self
        recordSwitch.action = #selector(recordSwitchChanged(_:))
        recordSwitch.controlSize = .small
        recordSwitch.identifier = NSUserInterfaceItemIdentifier("lexicon-review.record-switch")
        recordLabel.font = .systemFont(ofSize: 12)
        recordLabel.textColor = RimeUI.textPrimary

        captureButton.target = self
        captureButton.action = #selector(captureNow(_:))
        captureButton.controlSize = .small
        captureButton.bezelStyle = .rounded

        statusLabel.font = .systemFont(ofSize: 11)
        statusLabel.textColor = RimeUI.textMuted

        let header = NSStackView(views: [dictionaryPopUp, recordSwitch, recordLabel,
                                         NSView(), statusLabel, captureButton])
        header.orientation = .horizontal
        header.alignment = .centerY
        header.spacing = 8
        header.edgeInsets = NSEdgeInsets(top: 12, left: 14, bottom: 10, right: 14)
        header.views[3].setContentHuggingPriority(.defaultLow, for: .horizontal)

        configure(sectionTable, columns: [("section", "日期", 200)], sortable: false)
        sectionTable.headerView = nil
        sectionTable.identifier = NSUserInterfaceItemIdentifier("lexicon-review.sections")
        configure(wordTable, columns: [("word", "词", 220), ("code", "编码", 220),
                                       ("count", "次数", 70), ("change", "变化", 110)],
                  sortable: true)
        wordTable.identifier = NSUserInterfaceItemIdentifier("lexicon-review.words")

        filterControl.segmentCount = Filter.allCases.count
        for item in Filter.allCases {
            filterControl.setLabel(item.title, forSegment: item.rawValue)
        }
        filterControl.selectedSegment = filter.rawValue
        filterControl.trackingMode = .selectOne
        filterControl.controlSize = .small
        filterControl.target = self
        filterControl.action = #selector(filterChanged(_:))

        countLabel.font = .systemFont(ofSize: 11)
        countLabel.textColor = RimeUI.textMuted
        let toolbar = NSStackView(views: [filterControl, NSView(), countLabel])
        toolbar.orientation = .horizontal
        toolbar.alignment = .centerY
        toolbar.views[1].setContentHuggingPriority(.defaultLow, for: .horizontal)

        let sectionScroll = scrollView(for: sectionTable)
        let wordScroll = scrollView(for: wordTable)

        emptyLabel.font = .systemFont(ofSize: 12)
        emptyLabel.textColor = RimeUI.textMuted
        emptyLabel.alignment = .center
        emptyLabel.translatesAutoresizingMaskIntoConstraints = false
        wordScroll.addSubview(emptyLabel)
        NSLayoutConstraint.activate([
            emptyLabel.centerXAnchor.constraint(equalTo: wordScroll.centerXAnchor),
            emptyLabel.centerYAnchor.constraint(equalTo: wordScroll.centerYAnchor),
            emptyLabel.widthAnchor.constraint(lessThanOrEqualTo: wordScroll.widthAnchor, constant: -60),
        ])

        let footer = NSTextField(labelWithString:
            "这里只用来查看。删除、评分会在后续版本加入；学习词库本身没有被改动。")
        footer.font = .systemFont(ofSize: 10)
        footer.textColor = RimeUI.textMuted

        let right = NSStackView(views: [toolbar, wordScroll, footer])
        right.orientation = .vertical
        right.alignment = .leading
        right.spacing = 8
        let content = NSStackView(views: [sectionScroll, right])
        content.orientation = .horizontal
        content.alignment = .top
        content.spacing = 12
        content.edgeInsets = NSEdgeInsets(top: 0, left: 14, bottom: 14, right: 14)

        let root = NSStackView(views: [header, content])
        root.orientation = .vertical
        root.alignment = .leading
        root.spacing = 0
        root.translatesAutoresizingMaskIntoConstraints = false
        let container = NSView()
        container.addSubview(root)
        win.contentView = container
        NSLayoutConstraint.activate([
            root.leadingAnchor.constraint(equalTo: container.leadingAnchor),
            root.trailingAnchor.constraint(equalTo: container.trailingAnchor),
            root.topAnchor.constraint(equalTo: container.topAnchor),
            root.bottomAnchor.constraint(equalTo: container.bottomAnchor),
            header.widthAnchor.constraint(equalTo: root.widthAnchor),
            content.widthAnchor.constraint(equalTo: root.widthAnchor),
            sectionScroll.widthAnchor.constraint(equalToConstant: 200),
            sectionScroll.heightAnchor.constraint(equalTo: right.heightAnchor),
            toolbar.widthAnchor.constraint(equalTo: right.widthAnchor),
            wordScroll.widthAnchor.constraint(equalTo: right.widthAnchor),
        ])
        win.center()
        window = win

        observers.append(NotificationCenter.default.addObserver(
            forName: .rimeAppearanceDidChange, object: nil, queue: .main
        ) { [weak self] _ in
            self?.window?.appearance = RimeUI.appKitAppearance
            self?.window?.backgroundColor = RimeUI.surface
            self?.sectionTable.reloadData()
            self?.wordTable.reloadData()
        })
    }

    private func configure(_ table: NSTableView,
                           columns: [(String, String, CGFloat)],
                           sortable: Bool) {
        for (id, title, width) in columns {
            let column = NSTableColumn(identifier: NSUserInterfaceItemIdentifier(id))
            column.title = title
            column.width = width
            column.minWidth = 50
            if sortable {
                column.sortDescriptorPrototype = NSSortDescriptor(key: id, ascending: true)
            }
            table.addTableColumn(column)
        }
        table.dataSource = self
        table.delegate = self
        table.usesAlternatingRowBackgroundColors = table !== sectionTable
        table.allowsMultipleSelection = table !== sectionTable
        table.style = .inset
        table.backgroundColor = .clear
        table.columnAutoresizingStyle = .lastColumnOnlyAutoresizingStyle
    }

    private func scrollView(for table: NSTableView) -> NSScrollView {
        let scroll = NSScrollView()
        scroll.documentView = table
        scroll.hasVerticalScroller = true
        scroll.autohidesScrollers = true
        scroll.borderType = .noBorder
        scroll.drawsBackground = true
        scroll.backgroundColor = RimeUI.surface2
        scroll.wantsLayer = true
        scroll.layer?.cornerRadius = 8
        scroll.translatesAutoresizingMaskIntoConstraints = false
        return scroll
    }
}
