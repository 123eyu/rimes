import SwiftUI
import UIKit
import UniformTypeIdentifiers
import RimesCore

/// Native editing keeps a draft separate from the snapshot used by the extension.
struct CustomKeyboardLayoutsView: View {
    @State private var layouts: [CustomKeyboardLayout] = []
    @State private var active: CustomKeyboardLayout?
    @State private var importing = false
    @State private var inspecting = false
    @State private var importReview: CustomLayoutImportResult?
    @State private var message = ""
    @State private var error: String?
    @State private var pendingDelete: CustomKeyboardLayout?
    private let store = CustomLayoutStore()

    var body: some View {
        List {
            Section {
                Label("普通键盘，自由排列", systemImage: "keyboard")
                    .font(.headline)
                Text("自定义布局只用于普通逐键输入。并击继续使用原来的键位与方案。")
                    .font(.subheadline).foregroundStyle(.secondary)
                HStack {
                    Text("当前应用")
                    Spacer()
                    Text(active?.name ?? "系统默认布局").foregroundStyle(.secondary)
                }
                if active != nil {
                    Button("恢复默认普通键盘") {
                        do { try store.apply(nil); reload(); message = "已恢复默认普通键盘，并击设置保持不变。" }
                        catch { self.error = error.localizedDescription }
                    }
                }
            }
            Section {
                ForEach(CustomKeyboardLayout.templates, id: \.id) { template in
                    NavigationLink {
                        CustomKeyboardLayoutEditor(layout: copyOfTemplate(template), onSaved: reload)
                    } label: {
                        VStack(alignment: .leading, spacing: 5) {
                            Text(template.name)
                            Text(templateSubtitle(template)).font(.caption).foregroundStyle(.secondary)
                        }.padding(.vertical, 3)
                    }
                }
                Text("自由排列支持 26 键逐键输入。全拼 9 键可在上一级“键盘布局与换肤”中启用。")
                    .font(.caption).foregroundStyle(.secondary)
            } header: { Text("从模板开始") }
            Section {
                if layouts.isEmpty {
                    Text("调整一套模板，保存你的第一份布局。")
                        .font(.subheadline).foregroundStyle(.secondary)
                }
                ForEach(layouts, id: \.id) { layout in
                    NavigationLink {
                        CustomKeyboardLayoutEditor(layout: layout, onSaved: reload)
                    } label: {
                        HStack {
                            VStack(alignment: .leading, spacing: 4) {
                                Text(layout.name)
                                Text("\(layout.rows.flatMap { $0 }.filter { $0.action == .character }.count) 个字母槽位")
                                    .font(.caption).foregroundStyle(.secondary)
                            }
                            Spacer()
                            if active?.id == layout.id {
                                Text(active == layout ? "已应用" : "草稿有修改").font(.caption).foregroundStyle(.indigo)
                            }
                        }
                    }
                    .swipeActions {
                        Button(role: .destructive) { pendingDelete = layout } label: {
                            Label("删除", systemImage: "trash")
                        }
                    }
                }
            } header: { Text("我的布局") } footer: {
                Text("保存草稿不会更换键盘。进入布局后点击“应用到普通键盘”，才会更新正在使用的布局。")
            }
            Section {
                Button { importing = true } label: {
                    Label("从文件导入布局", systemImage: "square.and.arrow.down")
                }.disabled(inspecting)
                if inspecting { HStack { ProgressView(); Text("正在读取并检查文件…").font(.subheadline) } }
                Text("支持布局 JSON，以及可识别的 ZIP / YAML 键盘配置。导入前会列出可用布局和兼容性说明。")
                    .font(.caption).foregroundStyle(.secondary)
            }
            if !message.isEmpty {
                Section { Label(message, systemImage: "checkmark.circle").font(.subheadline).foregroundStyle(.secondary) }
            }
        }
        .navigationTitle("普通键盘布局")
        .tint(.indigo)
        .onAppear(perform: reload)
        .fileImporter(isPresented: $importing,
                      allowedContentTypes: [.json, .zip, UTType(filenameExtension: "yaml") ?? .data,
                                            UTType(filenameExtension: "yml") ?? .data],
                      allowsMultipleSelection: false) { result in
            switch result {
            case .success(let urls): if let url = urls.first { inspect(url) }
            case .failure(let failure): error = failure.localizedDescription
            }
        }
        .sheet(isPresented: Binding(get: { importReview != nil }, set: { if !$0 { importReview = nil } })) {
            if let result = importReview {
                CustomLayoutImportReview(result: result) { imported in
                    do {
                        var draft = imported
                        draft.id = UUID().uuidString
                        try store.saveDraft(draft)
                        importReview = nil
                        reload()
                        message = "已导入“\(draft.name)”为草稿，尚未应用到键盘。"
                    } catch { self.error = error.localizedDescription }
                }
            }
        }
        .alert("操作未完成", isPresented: Binding(get: { error != nil }, set: { if !$0 { error = nil } })) {
            Button("好") { error = nil }
        } message: { Text(error ?? "") }
        .confirmationDialog("删除这份布局？", isPresented: Binding(get: { pendingDelete != nil }, set: { if !$0 { pendingDelete = nil } }), titleVisibility: .visible) {
            Button("删除布局", role: .destructive) {
                guard let layout = pendingDelete else { return }
                do { try store.remove(id: layout.id); reload(); pendingDelete = nil }
                catch { self.error = error.localizedDescription }
            }
            Button("取消", role: .cancel) { pendingDelete = nil }
        } message: {
            Text(pendingDelete?.id == active?.id ? "这份布局正在使用。删除后将恢复默认普通键盘。" : "删除保存的草稿不会改变并击设置。")
        }
    }

    private func reload() {
        let library = store.load()
        layouts = library.layouts
        active = library.active
    }
    private func copyOfTemplate(_ template: CustomKeyboardLayout) -> CustomKeyboardLayout {
        var copy = template
        copy.id = UUID().uuidString
        copy.name += " · 我的布局"
        return copy
    }
    private func templateSubtitle(_ template: CustomKeyboardLayout) -> String {
        template.metrics.split ? "左右分区，保留独立字母键" : template.metrics.stagger ? "熟悉的 QWERTY 错行排列" : "共享列宽，上下对齐"
    }
    private func inspect(_ url: URL) {
        inspecting = true
        Task { @MainActor in
            defer { inspecting = false }
            do {
                let result = try await Task.detached(priority: .userInitiated) {
                    try CustomLayoutImportService.inspect(url: url)
                }.value
                importReview = result
            } catch { self.error = error.localizedDescription }
        }
    }
}

private struct CustomKeyboardLayoutEditor: View {
    @State private var draft: CustomKeyboardLayout
    @State private var selectedID: String?
    @State private var rowPattern: String
    @State private var history: [CustomKeyboardLayout] = []
    @State private var future: [CustomKeyboardLayout] = []
    @State private var message = ""
    @State private var error: String?
    @State private var showFunctions = false
    @State private var sliderEditing = false
    @State private var exporting = false
    @State private var exportDocument: CustomLayoutJSONDocument?
    @State private var resetConfirmation = false
    private let original: CustomKeyboardLayout
    private let onSaved: () -> Void
    private let letters = Array("abcdefghijklmnopqrstuvwxyz").map(String.init)
    private let store = CustomLayoutStore()

    init(layout: CustomKeyboardLayout, onSaved: @escaping () -> Void) {
        _draft = State(initialValue: layout)
        _rowPattern = State(initialValue: layout.rows.map { String($0.count) }.joined(separator: ","))
        original = layout
        self.onSaved = onSaved
    }

    var body: some View {
        Form {
            Section {
                TextField("布局名称", text: $draft.name)
                    .accessibilityIdentifier("customLayout.name")
                Text("修改只影响这里的预览。保存草稿或明确应用后，才会写入配置。")
                    .font(.caption).foregroundStyle(.secondary)
            }
            Section {
                CustomLayoutKeyboardPreview(layout: draft, selectedID: selectedID,
                    onSelect: { selectedID = $0 }, onDrop: handleDrop)
                    .padding(.vertical, 8)
                HStack {
                    Label("拖动交换键位，或选中后从键位库放入", systemImage: "hand.draw")
                        .font(.caption).foregroundStyle(.secondary)
                    Spacer(minLength: 0)
                }
                HStack {
                    Button { undo() } label: { Label("撤销", systemImage: "arrow.uturn.backward") }
                        .disabled(history.isEmpty)
                    Spacer()
                    Button { redo() } label: { Label("重做", systemImage: "arrow.uturn.forward") }
                        .disabled(future.isEmpty)
                    Spacer()
                    Button("重置") { resetConfirmation = true }
                }.font(.subheadline).buttonStyle(.borderless)
                if let warning = validationMessage {
                    Label(warning, systemImage: "exclamationmark.circle")
                        .font(.caption).foregroundStyle(.orange)
                }
            } header: { Text("实时预览") }
            keyLibrarySection
            slotSection
            sizeSection
            selectedKeySection
            Section {
                Button { saveDraft() } label: {
                    Label("保存草稿", systemImage: "square.and.arrow.down")
                }.accessibilityIdentifier("customLayout.saveDraft")
                Button { apply() } label: {
                    Label("应用到普通键盘", systemImage: "checkmark.circle.fill")
                }.accessibilityIdentifier("customLayout.apply")
                Button { prepareExport() } label: {
                    Label("导出布局 JSON", systemImage: "square.and.arrow.up")
                }
                if !message.isEmpty {
                    Label(message, systemImage: "checkmark.circle").font(.caption).foregroundStyle(.secondary)
                }
            } footer: {
                Text("应用前会检查字母是否齐全、功能键是否可用。并击的方案与键位保持原样。")
            }
        }
        .navigationTitle("编辑布局")
        .navigationBarTitleDisplayMode(.inline)
        .tint(.indigo)
        .toolbar {
            ToolbarItem(placement: .topBarTrailing) { Button("保存") { saveDraft() } }
        }
        .alert("操作未完成", isPresented: Binding(get: { error != nil }, set: { if !$0 { error = nil } })) {
            Button("好") { error = nil }
        } message: { Text(error ?? "") }
        .confirmationDialog("恢复进入编辑器时的布局？", isPresented: $resetConfirmation, titleVisibility: .visible) {
            Button("重置本次修改") { mutate { $0 = original }; syncRows(); selectedID = nil }
            Button("取消", role: .cancel) { }
        } message: { Text("可以用撤销恢复重置前的修改。") }
        .fileExporter(isPresented: $exporting, document: exportDocument, contentType: .json,
                      defaultFilename: draft.name.isEmpty ? "XSIME-layout" : draft.name) { result in
            switch result {
            case .success: message = "布局已导出。"
            case .failure(let failure): error = failure.localizedDescription
            }
        }
    }

    private var keyLibrarySection: some View {
        Section {
            Picker("键位类别", selection: $showFunctions) {
                Text("字母").tag(false)
                Text("功能键").tag(true)
            }.pickerStyle(.segmented)
            LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 6), count: showFunctions ? 4 : 7), spacing: 7) {
                if showFunctions {
                    ForEach(functions, id: \.rawValue) { action in
                        libraryButton(action: action, text: "")
                    }
                } else {
                    ForEach(characters, id: \.self) { letter in
                        libraryButton(action: .character, text: letter)
                    }
                }
            }.padding(.vertical, 5)
        } header: { Text("键位库") } footer: {
            Text("先选中一个槽位再轻点，也可以长按拖入。移动已放入的字母不会产生重复键；空出的字母可重新放回。")
        }
    }
    private var slotSection: some View {
        Section {
            HStack {
                TextField("每行槽位，例如 10,9,7,7", text: $rowPattern)
                    .keyboardType(.numbersAndPunctuation)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                    .accessibilityIdentifier("customLayout.rowCounts")
                Button("调整") { repartition() }.buttonStyle(.borderless)
                    .accessibilityIdentifier("customLayout.repartition")
            }
            HStack {
                Button("经典四行") { rowPattern = "10,9,7,7"; repartition() }
                Spacer()
                Button("紧凑五行") { rowPattern = "7,7,6,6,7"; repartition() }
            }.font(.caption).buttonStyle(.borderless)
        } header: { Text("槽位结构") } footer: {
            Text("这里包含所有行，每行 1–16 个槽位、最多 6 行，总计不超过 80 槽。字母和功能键都能自由放置；减少槽位不会自动合并字母。")
        }
    }
    private var sizeSection: some View {
        Section {
            metricSlider("键高", keyPath: \.keyHeight, range: 30...72)
            metricSlider("键间距", keyPath: \.gap, range: 0...12)
            metricSlider("左右边距", keyPath: \.padding, range: 0...28)
            Toggle("左右分区", isOn: metricToggle(\.split))
            if draft.metrics.split { metricSlider("分区间距", keyPath: \.splitGap, range: 0...60) }
            Toggle("传统错行", isOn: metricToggle(\.stagger))
            Button("使用紧凑尺寸") {
                mutate { $0.metrics.keyHeight = 38; $0.metrics.gap = 4; $0.metrics.padding = 4; $0.metrics.splitGap = 10 }
            }
        } header: { Text("尺寸与间距") }
    }
    @ViewBuilder private var selectedKeySection: some View {
        Section {
            if let selected = selectedKey {
                HStack {
                    Text(selected.text.isEmpty && selected.action == .character ? "空槽位" : selected.label)
                        .font(.title3.monospaced()).foregroundStyle(.indigo)
                    Spacer()
                    Text(selectedPosition).font(.caption).foregroundStyle(.secondary)
                }
                Picker("放入功能键", selection: Binding(get: { selectedKey?.action ?? .character }, set: { action in
                    if action != .character { assign(action: action, text: "") }
                })) {
                    Text("字母 / 空槽位").tag(CustomKeyAction.character)
                    ForEach(functions, id: \.rawValue) { Text(actionTitle($0)).tag($0) }
                }
                VStack(alignment: .leading, spacing: 5) {
                    HStack {
                        Text("宽度比例")
                        Spacer()
                        Text(String(format: "%.2g×", selected.width)).monospacedDigit().foregroundStyle(.secondary)
                    }.font(.subheadline)
                    Slider(value: selectedWidth, in: 0.5...4, step: 0.25, onEditingChanged: trackSlider)
                        .accessibilityIdentifier("customLayout.keyWidth")
                }
                HStack(spacing: 20) {
                    moveButton("arrow.left", row: 0, column: -1, label: "向左交换")
                    moveButton("arrow.up", row: -1, column: 0, label: "向上交换")
                    moveButton("arrow.down", row: 1, column: 0, label: "向下交换")
                    moveButton("arrow.right", row: 0, column: 1, label: "向右交换")
                    Spacer()
                    Button("清空") { assign(action: .character, text: "") }
                }.buttonStyle(.borderless)
            } else {
                Text("轻点预览中的一个键位，调整内容、宽度或位置。")
                    .font(.subheadline).foregroundStyle(.secondary)
            }
        } header: { Text("选中键位") }
    }

    private var characters: [String] {
        var result = letters
        for key in (original.rows + draft.rows).flatMap({ $0 }) where key.action == .character && key.text.count == 1 {
            if !result.contains(key.text) { result.append(key.text) }
        }
        return result
    }
    private var functions: [CustomKeyAction] { [.space, .backspace, .enter, .shift, .numbers, .language, .emoji, .buffer] }
    private var selectedKey: CustomKeyboardKey? { draft.rows.flatMap { $0 }.first { $0.id == selectedID } }
    private var selectedPosition: String {
        guard let p = position(selectedID) else { return "" }
        return "第 \(p.row + 1) 行 · 第 \(p.column + 1) 槽"
    }
    private var validationMessage: String? {
        do { _ = try draft.validated(); return nil }
        catch { return error.localizedDescription }
    }
    private func actionTitle(_ action: CustomKeyAction) -> String {
        switch action {
        case .character: return "字母"
        case .space: return "空格"
        case .backspace: return "删除"
        case .enter: return "回车"
        case .shift: return "大写切换"
        case .numbers: return "数字切换"
        case .language: return "中英切换"
        case .emoji: return "表情"
        case .buffer: return "Buffer"
        }
    }
    private func libraryButton(action: CustomKeyAction, text: String) -> some View {
        let placed = draft.rows.flatMap { $0 }.contains { $0.action == action && (action != .character || $0.text == text) }
        let token = "palette:\(action.rawValue):\(text)"
        return Button { assign(action: action, text: text) } label: {
            Text(action == .character ? text.uppercased() : actionTitle(action))
                .font(action == .character ? .body.monospaced() : .caption)
                .frame(maxWidth: .infinity, minHeight: 38)
                .background(placed ? Color(uiColor: .tertiarySystemFill) : Color.indigo.opacity(0.1), in: RoundedRectangle(cornerRadius: 7))
                .foregroundStyle(placed ? Color.secondary : Color.indigo)
        }
        .buttonStyle(.plain)
        .onDrag { NSItemProvider(object: token as NSString) }
        .accessibilityLabel(action == .character ? text.uppercased() : actionTitle(action))
        .accessibilityIdentifier("customLayout.palette.\(action == .character ? text : action.rawValue)")
    }
    private func metricSlider(_ title: String, keyPath: WritableKeyPath<CustomKeyboardMetrics, Double>, range: ClosedRange<Double>) -> some View {
        VStack(alignment: .leading, spacing: 5) {
            HStack {
                Text(title)
                Spacer()
                Text("\(Int(draft.metrics[keyPath: keyPath])) pt").monospacedDigit().foregroundStyle(.secondary)
            }.font(.subheadline)
            Slider(value: Binding(get: { draft.metrics[keyPath: keyPath] }, set: { draft.metrics[keyPath: keyPath] = $0; message = "" }),
                   in: range, step: 1, onEditingChanged: trackSlider)
        }
    }
    private func metricToggle(_ keyPath: WritableKeyPath<CustomKeyboardMetrics, Bool>) -> Binding<Bool> {
        Binding(get: { draft.metrics[keyPath: keyPath] }, set: { value in mutate { $0.metrics[keyPath: keyPath] = value } })
    }
    private var selectedWidth: Binding<Double> {
        Binding(get: { selectedKey?.width ?? 1 }, set: { value in
            guard let p = position(selectedID) else { return }
            draft.rows[p.row][p.column].width = value
            message = ""
        })
    }
    private func moveButton(_ symbol: String, row: Int, column: Int, label: String) -> some View {
        Button { move(row: row, column: column) } label: { Image(systemName: symbol).frame(minWidth: 24, minHeight: 32) }
            .accessibilityLabel(label)
    }
    private func position(_ id: String?) -> (row: Int, column: Int)? {
        guard let id else { return nil }
        for r in draft.rows.indices {
            if let c = draft.rows[r].firstIndex(where: { $0.id == id }) { return (r, c) }
        }
        return nil
    }
    private func record() {
        history.append(draft)
        if history.count > 60 { history.removeFirst() }
        future.removeAll()
    }
    private func mutate(_ mutation: (inout CustomKeyboardLayout) -> Void) {
        record()
        mutation(&draft)
        message = ""
    }
    private func trackSlider(_ editing: Bool) {
        if editing && !sliderEditing { record() }
        sliderEditing = editing
    }
    private func undo() {
        guard let previous = history.popLast() else { return }
        future.append(draft); draft = previous; syncRows(); message = ""
    }
    private func redo() {
        guard let next = future.popLast() else { return }
        history.append(draft); draft = next; syncRows(); message = ""
    }
    private func syncRows() {
        rowPattern = draft.rows.map { String($0.count) }.joined(separator: ",")
        if selectedKey == nil { selectedID = nil }
    }
    private func assign(action: CustomKeyAction, text: String, to target: String? = nil) {
        let destination = target ?? selectedID ?? draft.rows.flatMap { $0 }.first(where: { $0.action == .character && $0.text.isEmpty })?.id
        guard let p = position(destination) else { message = "请先在预览里选中一个槽位。"; return }
        if draft.rows[p.row][p.column].action == action && draft.rows[p.row][p.column].text == text { return }
        mutate { layout in
            if action != .character || !text.isEmpty {
                for r in layout.rows.indices {
                    for c in layout.rows[r].indices where layout.rows[r][c].action == action && (action != .character || layout.rows[r][c].text == text) {
                        layout.rows[r][c].action = .character
                        layout.rows[r][c].text = ""
                    }
                }
            }
            layout.rows[p.row][p.column].action = action
            layout.rows[p.row][p.column].text = text
        }
        selectedID = destination
    }
    private func swapKeys(_ firstID: String, _ secondID: String) -> Bool {
        guard firstID != secondID, let first = position(firstID), let second = position(secondID) else { return false }
        let firstKey = draft.rows[first.row][first.column], secondKey = draft.rows[second.row][second.column]
        mutate {
            $0.rows[first.row][first.column].action = secondKey.action
            $0.rows[first.row][first.column].text = secondKey.text
            $0.rows[second.row][second.column].action = firstKey.action
            $0.rows[second.row][second.column].text = firstKey.text
        }
        selectedID = secondID
        return true
    }
    private func handleDrop(_ source: String, _ destination: String) -> Bool {
        if source.hasPrefix("palette:") {
            let parts = source.split(separator: ":", maxSplits: 2, omittingEmptySubsequences: false)
            guard parts.count == 3, let action = CustomKeyAction(rawValue: String(parts[1])),
                  action != .character || characters.contains(String(parts[2])) else { return false }
            assign(action: action, text: String(parts[2]), to: destination)
            return true
        }
        return swapKeys(source, destination)
    }
    private func move(row: Int, column: Int) {
        guard let current = position(selectedID) else { return }
        let nextRow = current.row + row
        guard draft.rows.indices.contains(nextRow) else { return }
        let nextColumn = row == 0 ? current.column + column : min(current.column, draft.rows[nextRow].count - 1)
        guard draft.rows[nextRow].indices.contains(nextColumn), let selectedID else { return }
        _ = swapKeys(selectedID, draft.rows[nextRow][nextColumn].id)
    }
    private func repartition() {
        let components = rowPattern.components(separatedBy: CharacterSet(charactersIn: ",，/／ ")).filter { !$0.isEmpty }
        let counts = components.compactMap(Int.init)
        guard !counts.isEmpty, counts.count == components.count, counts.count <= 6,
              counts.allSatisfy({ (1...16).contains($0) }), counts.reduce(0, +) <= 80 else {
            error = "请输入 1–6 行，每行 1–16 个槽位、总数不超过 80，例如 10,9,7,7。"
            return
        }
        let existing = draft.rows.flatMap { $0 }
        mutate { layout in
            var cursor = 0
            layout.rows = counts.map { count in
                (0..<count).map { _ in
                    defer { cursor += 1 }
                    return cursor < existing.count ? existing[cursor] : CustomKeyboardKey()
                }
            }
        }
        selectedID = nil
        syncRows()
        message = "槽位已更新。未容纳的字母可从键位库重新放入。"
    }
    private func saveDraft() {
        do { try store.saveDraft(draft); onSaved(); message = "草稿已保存，正在使用的键盘保持不变。" }
        catch { self.error = error.localizedDescription }
    }
    private func apply() {
        do {
            _ = try draft.validated()
            try store.saveDraft(draft)
            try store.apply(draft)
            onSaved()
            message = "已应用到普通键盘；并击仍使用原来的键位。"
        } catch { self.error = error.localizedDescription }
    }
    private func prepareExport() {
        do { exportDocument = CustomLayoutJSONDocument(data: try draft.exportData()); exporting = true }
        catch { self.error = error.localizedDescription }
    }
}

private struct CustomLayoutKeyboardPreview: UIViewRepresentable {
    let layout: CustomKeyboardLayout
    var selectedID: String? = nil
    var onSelect: ((String) -> Void)? = nil
    var onDrop: ((String, String) -> Bool)? = nil

    func makeUIView(context: Context) -> CustomLayoutPreviewUIView {
        CustomLayoutPreviewUIView()
    }
    func updateUIView(_ view: CustomLayoutPreviewUIView, context: Context) {
        view.configure(layout: layout, selectedID: selectedID, onSelect: onSelect, onDrop: onDrop)
    }
    func sizeThatFits(_ proposal: ProposedViewSize, uiView: CustomLayoutPreviewUIView, context: Context) -> CGSize? {
        let width = max(1, proposal.width ?? 393)
        return CGSize(width: width, height: layout.geometry(width: Double(width), landscape: width > 590).height)
    }
}

/// A direct long-press drag keeps rearranging reliable inside Form's scrolling cells.
/// External palette drags still use UIKit item providers; both paths resolve the same frames.
private final class CustomLayoutPreviewUIView: UIView, UIDropInteractionDelegate {
    private var layout: CustomKeyboardLayout?
    private var frames: [CustomKeyboardKeyFrame] = []
    private var keys: [String: CustomLayoutPreviewKey] = [:]
    private var selectedID: String?
    private var onSelect: ((String) -> Void)?
    private var onDrop: ((String, String) -> Bool)?
    private var draggedID: String?
    private var destinationID: String?
    private var ghost: UILabel?
    private weak var lockedScroll: UIScrollView?
    private var previousScrollEnabled = true
    private lazy var dragGesture = UILongPressGestureRecognizer(target: self, action: #selector(dragged(_:)))

    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .secondarySystemFill
        layer.cornerRadius = 9
        isAccessibilityElement = false
        dragGesture.minimumPressDuration = 0.25
        dragGesture.allowableMovement = 20
        dragGesture.cancelsTouchesInView = true
        addGestureRecognizer(dragGesture)
        addInteraction(UIDropInteraction(delegate: self))
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }

    func configure(layout: CustomKeyboardLayout, selectedID: String?,
                   onSelect: ((String) -> Void)?, onDrop: ((String, String) -> Bool)?) {
        self.layout = layout
        self.selectedID = selectedID
        self.onSelect = onSelect
        self.onDrop = onDrop
        dragGesture.isEnabled = onDrop != nil
        let retainedIDs = Set(layout.rows.flatMap { $0 }.map(\.id))
        for id in Array(keys.keys) where !retainedIDs.contains(id) { keys.removeValue(forKey: id)?.removeFromSuperview() }
        for key in layout.rows.flatMap({ $0 }) {
            let control: CustomLayoutPreviewKey
            if let existing = keys[key.id] { control = existing }
            else {
                control = CustomLayoutPreviewKey()
                control.keyID = key.id
                control.addTarget(self, action: #selector(keyTapped(_:)), for: .touchUpInside)
                keys[key.id] = control
                addSubview(control)
            }
            control.title.text = key.label
            control.title.font = key.action == .character
                ? .monospacedSystemFont(ofSize: 17, weight: .medium)
                : .systemFont(ofSize: 12, weight: .medium)
            control.accessibilityLabel = key.label
            control.accessibilityIdentifier = "customLayout.key.\(key.id)"
            control.accessibilityHint = onDrop == nil ? nil : "轻点选择，按住后拖到另一个键位交换"
            control.isUserInteractionEnabled = onSelect != nil
        }
        refreshAppearance()
        invalidateIntrinsicContentSize()
        setNeedsLayout()
    }
    override var intrinsicContentSize: CGSize {
        CGSize(width: UIView.noIntrinsicMetric,
               height: layout?.geometry(width: Double(max(1, bounds.width)), landscape: bounds.width > 590).height ?? 1)
    }
    override func layoutSubviews() {
        super.layoutSubviews()
        guard let layout else { return }
        frames = layout.geometry(width: Double(bounds.width), landscape: bounds.width > 590).frames
        for frame in frames {
            keys[frame.key.id]?.frame = CGRect(x: frame.x, y: frame.y, width: max(1, frame.width), height: max(1, frame.height))
        }
        accessibilityElements = frames.compactMap { keys[$0.key.id] }
        if let ghost { bringSubviewToFront(ghost) }
    }
    override func didMoveToWindow() {
        super.didMoveToWindow()
        if window == nil { endInternalDrag() }
    }
    @objc private func keyTapped(_ sender: CustomLayoutPreviewKey) { onSelect?(sender.keyID) }
    private func key(at point: CGPoint) -> String? {
        frames.first { CGRect(x: $0.x, y: $0.y, width: $0.width, height: $0.height).contains(point) }?.key.id
    }
    private func refreshAppearance() {
        for (id, control) in keys {
            let selected = id == selectedID
            let target = id == destinationID && id != draggedID
            control.backgroundColor = selected || target ? UIColor.systemIndigo.withAlphaComponent(0.16) : .systemBackground
            control.title.textColor = selected || target ? .systemIndigo : .label
            control.layer.borderColor = selected || target ? UIColor.systemIndigo.cgColor : UIColor.label.withAlphaComponent(0.08).cgColor
            control.layer.borderWidth = target ? 2.5 : selected ? 2 : 0.5
            control.alpha = id == draggedID ? 0.4 : 1
        }
    }
    @objc private func dragged(_ recognizer: UILongPressGestureRecognizer) {
        let point = recognizer.location(in: self)
        switch recognizer.state {
        case .began:
            guard onDrop != nil, let source = key(at: point), let keyView = keys[source] else { return }
            draggedID = source
            destinationID = source
            var ancestor = superview
            while let current = ancestor {
                if let scroll = current as? UIScrollView {
                    lockedScroll = scroll
                    previousScrollEnabled = scroll.isScrollEnabled
                    scroll.isScrollEnabled = false
                    break
                }
                ancestor = current.superview
            }
            let label = UILabel(frame: keyView.frame)
            label.text = keyView.title.text
            label.font = keyView.title.font
            label.textAlignment = .center
            label.textColor = .white
            label.backgroundColor = .systemIndigo
            label.layer.cornerRadius = 7
            label.layer.masksToBounds = true
            label.isUserInteractionEnabled = false
            label.isAccessibilityElement = false
            addSubview(label)
            ghost = label
            updateDrag(point)
            UISelectionFeedbackGenerator().selectionChanged()
        case .changed:
            guard draggedID != nil else { return }
            updateDrag(point)
        case .ended:
            let source = draggedID
            let target = key(at: point)
            endInternalDrag()
            if let source, let target, source != target { _ = onDrop?(source, target) }
        case .cancelled, .failed:
            endInternalDrag()
        default: break
        }
    }
    private func updateDrag(_ point: CGPoint) {
        destinationID = key(at: point)
        ghost?.center = CGPoint(x: point.x, y: point.y - 22)
        refreshAppearance()
    }
    private func endInternalDrag() {
        ghost?.removeFromSuperview(); ghost = nil
        draggedID = nil; destinationID = nil
        if let lockedScroll { lockedScroll.isScrollEnabled = previousScrollEnabled }
        lockedScroll = nil
        refreshAppearance()
    }
    func dropInteraction(_ interaction: UIDropInteraction, canHandle session: UIDropSession) -> Bool {
        onDrop != nil && session.canLoadObjects(ofClass: NSString.self)
    }
    func dropInteraction(_ interaction: UIDropInteraction, sessionDidUpdate session: UIDropSession) -> UIDropProposal {
        destinationID = key(at: session.location(in: self))
        refreshAppearance()
        return UIDropProposal(operation: destinationID == nil ? .cancel : .move)
    }
    func dropInteraction(_ interaction: UIDropInteraction, performDrop session: UIDropSession) {
        guard let destination = key(at: session.location(in: self)), let action = onDrop,
              let item = session.items.first else { return }
        destinationID = nil
        refreshAppearance()
        if let source = item.localObject as? String { _ = action(source, destination); return }
        item.itemProvider.loadObject(ofClass: NSString.self) { object, _ in
            guard let source = object as? String else { return }
            DispatchQueue.main.async { _ = action(source, destination) }
        }
    }
    func dropInteraction(_ interaction: UIDropInteraction, sessionDidExit session: UIDropSession) {
        destinationID = nil; refreshAppearance()
    }
    func dropInteraction(_ interaction: UIDropInteraction, sessionDidEnd session: UIDropSession) {
        destinationID = nil; refreshAppearance()
    }
}

private final class CustomLayoutPreviewKey: UIControl {
    var keyID = ""
    let title = UILabel()
    override init(frame: CGRect) {
        super.init(frame: frame)
        layer.cornerRadius = 5
        title.textAlignment = .center
        title.adjustsFontSizeToFitWidth = true
        title.minimumScaleFactor = 0.55
        title.isUserInteractionEnabled = false
        addSubview(title)
        isAccessibilityElement = true
        accessibilityTraits = [.keyboardKey, .button]
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }
    override func accessibilityActivate() -> Bool {
        guard isUserInteractionEnabled else { return false }
        sendActions(for: .touchUpInside); return true
    }
    override func layoutSubviews() {
        super.layoutSubviews()
        title.frame = bounds.insetBy(dx: 2, dy: 1)
    }
}

private struct CustomLayoutImportReview: View {
    @Environment(\.dismiss) private var dismiss
    let result: CustomLayoutImportResult
    let onImport: (CustomKeyboardLayout) -> Void
    @State private var selectedIndex = 0

    var body: some View {
        NavigationStack {
            List {
                Section {
                    Text(result.title).font(.headline)
                    ForEach(Array(result.details.enumerated()), id: \.offset) { item in
                        Text(item.element).font(.subheadline).foregroundStyle(.secondary)
                    }
                }
                if !result.warnings.isEmpty {
                    Section("兼容性说明") {
                        ForEach(Array(result.warnings.enumerated()), id: \.offset) { item in
                            Label(item.element, systemImage: "exclamationmark.circle")
                                .font(.subheadline).foregroundStyle(.orange)
                        }
                    }
                }
                if result.layouts.isEmpty {
                    Section {
                        Text("文件中没有可以导入的布局。当前键盘没有变化。")
                            .foregroundStyle(.secondary)
                    }
                } else {
                    Section("选择要导入的布局") {
                        ForEach(Array(result.layouts.enumerated()), id: \.offset) { item in
                            Button { selectedIndex = item.offset } label: {
                                HStack {
                                    Text(item.element.name).foregroundStyle(.primary)
                                    Spacer()
                                    if selectedIndex == item.offset { Image(systemName: "checkmark.circle.fill") }
                                }
                            }
                        }
                    }
                    if result.layouts.indices.contains(selectedIndex) {
                        Section("候选布局预览") {
                            CustomLayoutKeyboardPreview(layout: result.layouts[selectedIndex])
                                .padding(.vertical, 8)
                        }
                        Section {
                            Button { onImport(result.layouts[selectedIndex]) } label: {
                                Label("导入为草稿", systemImage: "square.and.arrow.down")
                            }.accessibilityIdentifier("customLayout.importDraft")
                        } footer: {
                            Text("只保存所选布局的副本，不自动应用，也不会修改并击方案。")
                        }
                    }
                }
            }
            .navigationTitle("检查导入布局")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar { ToolbarItem(placement: .cancellationAction) { Button("取消") { dismiss() } } }
            .tint(.indigo)
        }
    }
}

private struct CustomLayoutJSONDocument: FileDocument {
    static var readableContentTypes: [UTType] { [.json] }
    var data: Data
    init(data: Data) { self.data = data }
    init(configuration: ReadConfiguration) throws {
        guard let data = configuration.file.regularFileContents else { throw CocoaError(.fileReadCorruptFile) }
        self.data = data
    }
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        FileWrapper(regularFileWithContents: data)
    }
}
