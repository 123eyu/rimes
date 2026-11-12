import AppKit

/// Review of learned words, grouped by the day they were learned, with
/// deletion and undo (steps 1–2 of docs/plans/learned-words-review.md).
final class LearnedWordsReviewWindowController: NSObject, NSWindowDelegate,
                                                NSTableViewDataSource, NSTableViewDelegate {
    static let shared = LearnedWordsReviewWindowController(ledger: .shared)

    enum Section: Equatable {
        case day(String)
        case all
        case deletion(String)
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
        var isDeleted = false

        var key: LearnedWordKey { LearnedWordKey(word: word, code: code) }
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
    private var rounds: [LearnedWordsDeletionRound] = []
    private var deletedKeys: Set<LearnedWordKey> = []
    /// Result of the last delete or undo, shown in the footer.
    private(set) var notice: String?
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
    private let wordTable = LearnedWordsTableView()
    private let deleteButton = NSButton(title: "删除所选…", target: nil, action: nil)
    private let undoButton = NSButton(title: "撤销这一轮", target: nil, action: nil)
    private let footerLabel = NSTextField(labelWithString: "")
    private static let defaultFooter = "选中词后按 Delete 键或点“删除所选…”。删除前会自动备份，可以撤销。"

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
        if kind != self.kind { notice = nil }
        self.kind = kind
        if let index = UserLexiconKind.allCases.firstIndex(of: kind) {
            dictionaryPopUp.selectItem(at: index)
        }
        reload()
    }

    // MARK: Data

    var selectedSection: Section? {
        sections.indices.contains(sectionTable.selectedRow)
            ? sections[sectionTable.selectedRow].section : nil
    }

    func reload() {
        let previous = selectedSection
        days = ledger.days(for: kind)
        rounds = ledger.deletionRounds(for: kind)
        deletedKeys = ledger.currentlyDeleted(for: kind)
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
        built += rounds.map { round in
            SectionRow(section: .deletion(round.id),
                       title: "删除 · \(Self.timestampFormatter.string(from: round.deletedAt))",
                       detail: round.isUndone ? "已撤销 · \(round.words.count.formatted()) 个词"
                                              : "\(round.words.count.formatted()) 个词")
        }
        sections = built
        recordSwitch.state = preferences.recordsDaily ? .on : .off
        updateStatus()
        sectionTable.reloadData()
        let index = previous.flatMap { section in sections.firstIndex { $0.section == section } }
            ?? (sections.isEmpty ? nil : 0)
        if let index {
            sectionTable.selectRowIndexes(IndexSet(integer: index), byExtendingSelection: false)
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
                markDeleted(WordRow(word: $0.word, code: $0.code, count: $0.count,
                                    change: "新学", isNew: true))
            }
            let reused = day.reused.map {
                markDeleted(WordRow(word: $0.word, code: $0.code, count: $0.toCount,
                                    change: "\($0.fromCount) → \($0.toCount)", isNew: false))
            }
            allRows = added.sorted(by: Self.byWord) + reused.sorted(by: Self.byWord)
        case .all:
            allRows = ledger.currentEntries(for: kind)
                .map { WordRow(word: $0.word, code: $0.code, count: $0.count, change: "", isNew: false) }
                .sorted { ($0.count, $0.word) < ($1.count, $1.word) }
        case .deletion(let id):
            guard let round = rounds.first(where: { $0.id == id }) else { allRows = []; break }
            allRows = round.words.map { word in
                let deleted = deletedKeys.contains(word.key)
                let change = round.isUndone ? "已撤销" : (deleted ? "已删除" : "已重新学会")
                return WordRow(word: word.word, code: word.code, count: word.count,
                               change: change, isNew: false, isDeleted: deleted)
            }.sorted(by: Self.byWord)
        }
        applyFilter()
    }

    private func markDeleted(_ row: WordRow) -> WordRow {
        guard deletedKeys.contains(row.key) else { return row }
        return WordRow(word: row.word, code: row.code, count: row.count,
                       change: "已删除", isNew: false, isDeleted: true)
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
        footerLabel.stringValue = notice ?? Self.defaultFooter
        updateActionButtons()
    }

    private func updateActionButtons() {
        var roundIsOpen: Bool?
        if case .deletion(let id)? = selectedSection {
            roundIsOpen = rounds.first(where: { $0.id == id }).map { !$0.isUndone } ?? false
        }
        deleteButton.isHidden = roundIsOpen != nil
        deleteButton.isEnabled = !deletableSelection().isEmpty
        undoButton.isHidden = roundIsOpen == nil
        undoButton.isEnabled = roundIsOpen == true
    }

    private func deletableSelection() -> [LearnedWordKey] {
        if case .deletion? = selectedSection { return [] }
        return wordTable.selectedRowIndexes.compactMap { index in
            rows.indices.contains(index) && !rows[index].isDeleted ? rows[index].key : nil
        }
    }

    // MARK: Selection (also used by the smoke test)

    func selectWords(at indexes: IndexSet) {
        wordTable.selectRowIndexes(indexes, byExtendingSelection: false)
        updateActionButtons()
    }

    var canDeleteSelection: Bool { !deleteButton.isHidden && deleteButton.isEnabled }
    var canUndoSelectedRound: Bool { !undoButton.isHidden && undoButton.isEnabled }

    private func emptyMessage() -> String {
        if sections.isEmpty {
            return preferences.recordsDaily
                ? "还没有记录。点“立即记录”建立第一份记录；之后每天会自动比较一次。"
                : "打开上方的开关后，每天会把新学的词记下来。词语只保存在本机，不会发送到任何地方。"
        }
        if allRows.isEmpty { return "这里没有词。" }
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

    @objc private func deleteButtonPressed(_ sender: Any?) {
        requestDeletion(skipConfirmation: false)
    }

    /// Asks first unless `skipConfirmation` is set (the smoke test does).
    func requestDeletion(skipConfirmation: Bool) {
        let keys = deletableSelection()
        guard !keys.isEmpty else { return }
        if skipConfirmation {
            performDeletion(keys)
            return
        }
        guard let window else { return }
        let alert = NSAlert()
        alert.messageText = "删除 \(keys.count.formatted()) 个学习词？"
        alert.informativeText = "删除前会自动备份当前学习词库，之后可以在左侧的“删除”记录里撤销。\n以后再输入这些词时，Rime 会按原来的次数把它们恢复。"
        alert.alertStyle = .warning
        alert.addButton(withTitle: "删除").hasDestructiveAction = true
        alert.addButton(withTitle: "取消")
        alert.beginSheetModal(for: window) { [weak self] response in
            if response == .alertFirstButtonReturn { self?.performDeletion(keys) }
        }
    }

    private func performDeletion(_ keys: [LearnedWordKey]) {
        do {
            let result = try ledger.delete(keys, from: kind)
            var text = "已删除 \(result.round.words.count.formatted()) 个词，可以在左侧“删除”记录里撤销。"
            if result.notFound > 0 { text += "另有 \(result.notFound) 个已不在学习词库里。" }
            if result.notDeleted > 0 { text += "还有 \(result.notDeleted) 个没有删掉，请再试一次。" }
            notice = text
        } catch {
            presentError(error, title: "无法删除学习词")
        }
        reload()
    }

    @objc func undoSelectedRound(_ sender: Any?) {
        guard case .deletion(let id)? = selectedSection else { return }
        do {
            let round = try ledger.undo(roundID: id, for: kind)
            notice = "已恢复 \(round.words.count.formatted()) 个词，次数和删除前一样。"
        } catch {
            presentError(error, title: "无法撤销删除")
        }
        reload()
    }

    private func presentError(_ error: Error, title: String = "无法记录学习词") {
        guard let window else { return }
        let alert = NSAlert()
        alert.messageText = title
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
            cell.textField?.textColor = item.isDeleted ? RimeUI.textMuted
                : (identifier.rawValue == "change" && item.isNew ? RimeUI.accentTextColor
                                                                 : RimeUI.textPrimary)
        }
        return cell
    }

    func tableView(_ tableView: NSTableView, heightOfRow row: Int) -> CGFloat {
        tableView === sectionTable ? 40 : 22
    }

    func tableViewSelectionDidChange(_ notification: Notification) {
        if (notification.object as? NSTableView) === sectionTable {
            showSelectedSection()
        } else {
            updateActionButtons()
        }
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
        for (button, action, id) in [(deleteButton, #selector(deleteButtonPressed(_:)), "delete"),
                                     (undoButton, #selector(undoSelectedRound(_:)), "undo")] {
            button.target = self
            button.action = action
            button.controlSize = .small
            button.bezelStyle = .rounded
            button.identifier = NSUserInterfaceItemIdentifier("lexicon-review.\(id)")
        }
        undoButton.isHidden = true
        wordTable.onDelete = { [weak self] in self?.requestDeletion(skipConfirmation: false) }
        let toolbar = NSStackView(views: [filterControl, NSView(), countLabel, deleteButton, undoButton])
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

        footerLabel.font = .systemFont(ofSize: 10)
        footerLabel.textColor = RimeUI.textMuted
        footerLabel.lineBreakMode = .byTruncatingTail
        footerLabel.stringValue = Self.defaultFooter

        let right = NSStackView(views: [toolbar, wordScroll, footerLabel])
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

/// Lets the Delete and Forward Delete keys act on the selected words.
final class LearnedWordsTableView: NSTableView {
    var onDelete: (() -> Void)?

    override func keyDown(with event: NSEvent) {
        if event.keyCode == 51 || event.keyCode == 117, let onDelete {
            onDelete()
            return
        }
        super.keyDown(with: event)
    }
}
