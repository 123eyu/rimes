import Cocoa
import SwiftTerm
import ImageIO
import UniformTypeIdentifiers

enum MailboxTerminalRules {
    static let maximumFileBytes = 24 * 1_048_576
    static let maximumTextBytes = 1_048_576
    static let maximumImageDimension = 16000
    static let maximumImagePixels = 32_000_000
    static let maximumFiles = 60
    static let previewCharacters = 360
    static let fontSize: CGFloat = 13
    static let terminalInset: CGFloat = 9
    static let sidebarWidth: CGFloat = 200
    static let sidebarBreakpoint: CGFloat = 720
    static let imageHeight: CGFloat = 168
}

enum MailboxTerminalKind: String, CaseIterable {
    case codex = "Codex", claude = "Claude Code", shell = "Shell"
    func executable() -> URL? {
        if self == .codex { return CodexExecutableLocator.resolve() }
        if self == .shell { return URL(fileURLWithPath: "/bin/zsh") }
        let home = FileManager.default.homeDirectoryForCurrentUser
        let directories = [home.appendingPathComponent(".local/bin").path, "/opt/homebrew/bin", "/usr/local/bin"]
            + (ProcessInfo.processInfo.environment["PATH"] ?? "").split(separator: ":").map(String.init)
        return directories.map { URL(fileURLWithPath: $0).appendingPathComponent("claude") }
            .first { FileManager.default.isExecutableFile(atPath: $0.path) }
    }
}

final class MailboxTerminalSession: NSObject, LocalProcessTerminalViewDelegate {
    let id = UUID()
    let kind: MailboxTerminalKind
    let workspace: URL
    let startedAt = Date()
    let terminal: LocalProcessTerminalView
    private(set) var title: String
    private(set) var running = false
    private(set) var exitCode: Int32?
    var onChange: (() -> Void)?
    init(kind: MailboxTerminalKind, workspace: URL) {
        self.kind = kind; self.workspace = workspace; title = workspace.lastPathComponent
        terminal = LocalProcessTerminalView(frame: NSRect(x: 0, y: 0, width: 640, height: 560))
        super.init()
        terminal.font = .monospacedSystemFont(ofSize: MailboxTerminalRules.fontSize, weight: .regular)
        terminal.nativeBackgroundColor = NSColor(srgbRed: 0.09, green: 0.11, blue: 0.13, alpha: 1)
        terminal.nativeForegroundColor = NSColor(srgbRed: 0.87, green: 0.90, blue: 0.90, alpha: 1)
        terminal.processDelegate = self
    }
    func start(executable: URL, arguments: [String]? = nil) {
        let environment = ProcessInfo.processInfo.environment
        terminal.startProcess(executable: executable.path,
            args: arguments ?? (kind == .shell ? ["-l"] : []),
            environment: CodexProcessEnvironment.variables(
                base: Terminal.getEnvironmentVariables(termName: "xterm-256color"),
                inheritedPath: environment["PATH"], executable: executable, workspace: workspace,
                homeDirectory: FileManager.default.homeDirectoryForCurrentUser, shell: environment["SHELL"]),
            execName: nil, currentDirectory: workspace.path)
        running = terminal.process.running; onChange?()
    }
    func stop() { if running { terminal.terminate() } }
    func sizeChanged(source: LocalProcessTerminalView, newCols: Int, newRows: Int) {}
    func setTerminalTitle(source: LocalProcessTerminalView, title: String) {
        // OSC titles are display data; never use them to associate receipts.
        if !title.isEmpty { self.title = String(title.prefix(100)); onChange?() }
    }
    func hostCurrentDirectoryUpdate(source: TerminalView, directory: String?) {}
    func processTerminated(source: TerminalView, exitCode: Int32?) {
        running = false
        // SwiftTerm's Darwin LocalProcess forwards waitpid's packed status.
        self.exitCode = exitCode.map { status in
            let signal = status & 0x7f
            return signal == 0 ? (status >> 8) & 0xff : 128 + signal
        }
        onChange?()
    }
}

/// File collection is an explicit user action. A filename, OSC title or shared
/// working directory is not proof that a CLI task produced a particular file.
struct MailboxCollectedFile {
    let url: URL
    let body: String
    let pngData: Data?
    static func read(_ url: URL) throws -> MailboxCollectedFile {
        let values = try url.resourceValues(forKeys: [.isRegularFileKey, .fileSizeKey])
        guard values.isRegularFile == true, let size = values.fileSize, size <= MailboxTerminalRules.maximumFileBytes else {
            throw MailboxCollectionError.unsupported("请选择不超过 24 MB 的普通图片或文本文件。")
        }
        let file = try FileHandle(forReadingFrom: url)
        defer { try? file.close() }
        let data = try file.read(upToCount: MailboxTerminalRules.maximumFileBytes + 1) ?? Data()
        guard data.count <= MailboxTerminalRules.maximumFileBytes else { throw MailboxStoreError.oversized }
        if let imageSource = CGImageSourceCreateWithData(data as CFData, nil), CGImageSourceGetType(imageSource) != nil {
            let props = CGImageSourceCopyPropertiesAtIndex(imageSource, 0, nil) as? [CFString: Any]
            let width = (props?[kCGImagePropertyPixelWidth] as? NSNumber)?.intValue ?? 0
            let height = (props?[kCGImagePropertyPixelHeight] as? NSNumber)?.intValue ?? 0
            guard width > 0, height > 0, width <= MailboxTerminalRules.maximumImageDimension, height <= MailboxTerminalRules.maximumImageDimension, width * height <= MailboxTerminalRules.maximumImagePixels,
                  let image = CGImageSourceCreateImageAtIndex(imageSource, 0, nil),
                  let png = NSBitmapImageRep(cgImage: image).representation(using: .png, properties: [:]) else {
                throw MailboxCollectionError.unsupported("这张图片过大或无法读取。")
            }
            return MailboxCollectedFile(url: url, body: "\(url.lastPathComponent)\n\(width) × \(height)", pngData: png)
        }
        guard data.count <= MailboxTerminalRules.maximumTextBytes, let text = String(data: data, encoding: .utf8),
              !text.contains("\0"), !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
            throw MailboxCollectionError.unsupported("暂时支持图片和不超过 1 MB 的 UTF-8 文本。")
        }
        return MailboxCollectedFile(url: url, body: text, pngData: nil)
    }
}
enum MailboxCollectionError: LocalizedError {
    case unsupported(String)
    var errorDescription: String? { if case let .unsupported(message) = self { return message }; return nil }
}

final class MailboxTerminalWorkspace: NSViewController {
    private let store: MailboxStore
    private(set) var sessions: [MailboxTerminalSession] = []
    private(set) var selectedID: UUID?
    private let index = MailboxFlowView()
    private let terminalHost = MailboxSurface(NSColor(srgbRed: 0.09, green: 0.11, blue: 0.13, alpha: 1))
    private let inspector = MailboxSurface(MailboxUI.sidebar)
    private let inspectorBody = MailboxFlowView()
    private let tabs = NSSegmentedControl(labels: ["产物", "文件", "会话"], trackingMode: .selectOne, target: nil, action: nil)
    private let launchKind = NSPopUpButton()
    private let titleLabel = MailboxUI.label("终端", size: 15, weight: .semibold)
    private let pathLabel = MailboxUI.label("", size: 11, secondary: true)
    private let status = MailboxUI.label("", size: 11, secondary: true)
    private lazy var stop = MailboxActionButton("", symbol: "stop") { [weak self] in self?.selectedSession?.stop() }
    private var split: MailboxAdaptiveSplitView!
    private lazy var toggleList = MailboxActionButton("", symbol: "sidebar.left") { [weak self] in self?.split.toggleSidebar() }
    private lazy var toggleInspector = MailboxActionButton("", symbol: "sidebar.right") { [weak self] in self?.split.toggleInspector() }
    private var selection: MailboxCollectedFile?
    private var selectionSessionID: UUID?
    private var files: [URL] = []
    var openReceipt: ((UUID) -> Void)?
    private var selectedSession: MailboxTerminalSession? { sessions.first { $0.id == selectedID } }

    init(store: MailboxStore) { self.store = store; super.init(nibName: nil, bundle: nil) }
    required init?(coder: NSCoder) { fatalError("init(coder:) unavailable") }
    override func loadView() {
        view = MailboxSurface(.textBackgroundColor)
        launchKind.addItems(withTitles: MailboxTerminalKind.allCases.map(\.rawValue))
        launchKind.font = .systemFont(ofSize: 12)
        let new = MailboxActionButton("新建会话", symbol: "plus") { [weak self] in self?.chooseWorkspace() }
        let closeList = MailboxActionButton("", symbol: "chevron.left") { [weak self] in self?.split.toggleSidebar() }
        closeList.isBordered = false; closeList.toolTip = "收起会话列表"; closeList.setAccessibilityLabel("收起会话列表")
        let listTitle = MailboxUI.stack([MailboxUI.label("终端", size: 19, weight: .semibold), MailboxUI.spacer(), closeList], vertical: false)
        let header = MailboxUI.stack([listTitle, launchKind, new], spacing: 12)
        let sidebar = MailboxSurface(MailboxUI.sidebar)
        MailboxUI.pin(MailboxUI.stack([MailboxUI.padded(header, inset: 16), MailboxUI.scroll(index)], spacing: 0), to: sidebar)
        toggleList.toolTip = "显示或隐藏会话列表"; toggleList.setAccessibilityLabel("显示或隐藏会话列表")
        toggleInspector.toolTip = "显示或隐藏扩展区"; toggleInspector.setAccessibilityLabel("显示或隐藏扩展区")
        stop.toolTip = "结束当前终端进程"; stop.setAccessibilityLabel("结束当前终端进程")
        for button in [toggleList, stop, toggleInspector] { button.isBordered = false }
        let titleColumn = MailboxUI.stack([titleLabel, pathLabel], spacing: 4)
        titleColumn.widthAnchor.constraint(lessThanOrEqualToConstant: 400).isActive = true
        titleColumn.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        let toolsRow = MailboxUI.stack([toggleList, stop, toggleInspector], vertical: false, spacing: 4)
        let top = MailboxUI.stack([titleColumn, MailboxUI.spacer(), toolsRow], vertical: false)
        let main = MailboxUI.stack([MailboxUI.padded(top, inset: 16), terminalHost, MailboxUI.padded(status, inset: 10)], spacing: 0)
        tabs.selectedSegment = 0; tabs.target = self; tabs.action = #selector(tabChanged)
        tabs.segmentStyle = .rounded; tabs.font = .systemFont(ofSize: 11)
        let choose = MailboxActionButton("", symbol: "plus") { [weak self] in self?.chooseFile() }
        choose.toolTip = "选择文件"; choose.setAccessibilityLabel("选择文件"); choose.isBordered = false
        let closeInspector = MailboxActionButton("", symbol: "chevron.right") { [weak self] in self?.split.toggleInspector() }
        closeInspector.toolTip = "收起扩展区"; closeInspector.setAccessibilityLabel("收起扩展区"); closeInspector.isBordered = false
        let inspectorTop = MailboxUI.stack([MailboxUI.label("扩展区", size: 13, weight: .semibold), MailboxUI.spacer(), choose, closeInspector], vertical: false, spacing: 4)
        let tools = MailboxUI.stack([inspectorTop, tabs], spacing: 12)
        let right = MailboxUI.stack([MailboxUI.padded(tools, inset: 16), MailboxUI.scroll(inspectorBody)], spacing: 0)
        MailboxUI.pin(right, to: inspector)
        split = MailboxAdaptiveSplitView(sidebar: sidebar, main: main, inspector: inspector, sidebarWidth: MailboxTerminalRules.sidebarWidth, sidebarBreakpoint: MailboxTerminalRules.sidebarBreakpoint)
        split.visibilityChanged = { [weak self] list, inspector in
            self?.toggleList.contentTintColor = list ? MailboxUI.accent : .secondaryLabelColor
            self?.toggleInspector.contentTintColor = inspector ? MailboxUI.accent : .secondaryLabelColor
        }
        MailboxUI.pin(split, to: view)
        mountTerminal(); refresh()
    }
    private func chooseWorkspace() {
        guard let window = view.window else { return }
        let panel = NSOpenPanel(); panel.canChooseDirectories = true; panel.canChooseFiles = false
        panel.allowsMultipleSelection = false; panel.prompt = "启动会话"
        panel.message = "选择此终端会话的工作目录"
        panel.directoryURL = CodexSessionWorkspaceRules.preferredWorkspace()
        let kind = MailboxTerminalKind.allCases[max(0, launchKind.indexOfSelectedItem)]
        panel.beginSheetModal(for: window) { [weak self] result in
            guard result == .OK, let url = panel.url, let self else { return }
            guard let executable = kind.executable() else {
                self.status.stringValue = "未找到 \(kind.rawValue)，请先安装相应 CLI。"; return
            }
            CodexSessionWorkspaceRules.remember(url)
            self.launch(kind: kind, workspace: url, executable: executable)
        }
    }
    @discardableResult func launch(kind: MailboxTerminalKind, workspace: URL, executable: URL,
                                   arguments: [String]? = nil) -> MailboxTerminalSession {
        _ = view
        let session = MailboxTerminalSession(kind: kind, workspace: workspace)
        sessions.insert(session, at: 0); selectedID = session.id
        split.dismissOverlays()
        session.onChange = { [weak self] in self?.refresh() }
        mountTerminal(); view.layoutSubtreeIfNeeded()
        session.start(executable: executable, arguments: arguments)
        refresh(); view.window?.makeFirstResponder(session.terminal)
        return session
    }
    @discardableResult func selectSession(_ id: UUID) -> Bool {
        guard sessions.contains(where: { $0.id == id }) else { return false }
        _ = view
        selectedID = id; mountTerminal(); refresh(); split.dismissOverlays(); return true
    }
    private func mountTerminal() {
        terminalHost.subviews.forEach { $0.removeFromSuperview() }
        selection = nil; selectionSessionID = nil; files = []
        if let session = selectedSession {
            MailboxUI.pin(session.terminal, to: terminalHost, inset: MailboxTerminalRules.terminalInset)
        } else {
            let title = MailboxUI.label("你的终端，也有一个工作台", size: 24, weight: .medium)
            title.textColor = NSColor(white: 0.86, alpha: 1)
            let hint = MailboxUI.paragraph("从左侧选择 Codex、Claude Code 或 Shell，\n新建会话后即可直接操作终端。\n\n右侧可以预览文件，把选中的成果收入 Mailbox。", size: 14)
            hint.textColor = NSColor(white: 0.58, alpha: 1)
            let content = MailboxUI.stack([title, hint, MailboxUI.spacer()], spacing: 18)
            MailboxUI.pin(content, to: terminalHost, inset: 30)
        }
    }
    func refresh() {
        guard isViewLoaded else { return }
        MailboxUI.clear(index)
        for session in sessions {
            index.addArrangedSubview(MailboxListButton(title: session.title,
                subtitle: session.workspace.path, metadata: "\(session.kind.rawValue) · \(session.running ? "运行中" : "已结束")",
                selected: session.id == selectedID) { [weak self] in self?.selectSession(session.id) })
        }
        if sessions.isEmpty {
            index.addArrangedSubview(MailboxUI.padded(MailboxUI.paragraph("会话将在这里保留。\n切换工作区不会中断运行。", size: 12), inset: 18))
        }
        titleLabel.stringValue = selectedSession.map { "\($0.kind.rawValue) · \($0.title)" } ?? "终端"
        pathLabel.stringValue = selectedSession?.workspace.path ?? "交互式命令行工作区"
        titleLabel.toolTip = titleLabel.stringValue; pathLabel.toolTip = pathLabel.stringValue
        stop.isEnabled = selectedSession?.running == true
        if let session = selectedSession {
            status.stringValue = session.running ? "● 终端运行中 · 关闭窗口后继续运行" : "终端已结束 · \(session.exitCode.map { "退出状态 \($0)" } ?? "未取得退出状态")"
        } else { status.stringValue = "会话在本次 RIMES 运行期间保留" }
        renderInspector()
    }
    @objc private func tabChanged() {
        if tabs.selectedSegment == 1 { refreshFiles() }
        renderInspector()
    }
    private func refreshFiles() {
        guard let session = selectedSession else { files = []; return }
        do {
            files = try FileManager.default.contentsOfDirectory(at: session.workspace,
                includingPropertiesForKeys: [.isRegularFileKey], options: [.skipsHiddenFiles])
                .filter { (try? $0.resourceValues(forKeys: [.isRegularFileKey]).isRegularFile) == true }
                .sorted { $0.lastPathComponent.localizedStandardCompare($1.lastPathComponent) == .orderedAscending }
            files = Array(files.prefix(MailboxTerminalRules.maximumFiles))
        } catch { status.stringValue = error.localizedDescription; files = [] }
    }
    private func renderInspector() {
        MailboxUI.clear(inspectorBody)
        let body = MailboxUI.stack([], spacing: 14)
        if let selection, selectionSessionID == selectedID, tabs.selectedSegment != 2 {
            body.addArrangedSubview(MailboxUI.label(selection.url.lastPathComponent, size: 13, weight: .semibold))
            if let data = selection.pngData, let image = NSImage(data: data) {
                let preview = NSImageView(); preview.image = image; preview.imageScaling = .scaleProportionallyUpOrDown
                preview.translatesAutoresizingMaskIntoConstraints = false
                preview.heightAnchor.constraint(equalToConstant: MailboxTerminalRules.imageHeight).isActive = true
                body.addArrangedSubview(preview)
            } else { body.addArrangedSubview(MailboxUI.paragraph(String(selection.body.prefix(MailboxTerminalRules.previewCharacters)), size: 12)) }
            body.addArrangedSubview(MailboxActionButton("收入收件箱", symbol: "tray.and.arrow.down") { [weak self] in self?.collectSelection() })
            body.addArrangedSubview(MailboxUI.line())
        }
        switch tabs.selectedSegment {
        case 1:
            body.addArrangedSubview(MailboxUI.label("工作目录 · 最多显示 60 个文件", size: 11, secondary: true))
            body.addArrangedSubview(MailboxActionButton("刷新文件", symbol: "arrow.clockwise") { [weak self] in self?.refreshFiles(); self?.renderInspector() })
            for url in files {
                let button = MailboxActionButton(url.lastPathComponent, symbol: "doc") { [weak self] in self?.preview(url) }
                button.isBordered = false; button.alignment = .left
                button.toolTip = url.lastPathComponent
                body.addArrangedSubview(button)
            }
            if files.isEmpty { body.addArrangedSubview(MailboxUI.paragraph("此目录暂无文件。也可以用上方的“选择文件”打开子目录中的产物。", size: 12)) }
        case 2:
            if let session = selectedSession {
                body.addArrangedSubview(MailboxUI.label(session.kind.rawValue, size: 17, weight: .medium))
                body.addArrangedSubview(MailboxUI.paragraph("启动于 \(MailboxUI.time(session.startedAt))\n\n\(session.workspace.path)\n\n\(session.running ? "进程正在运行" : "进程已结束")\n会话 ID · \(session.id.uuidString.prefix(8))", size: 12))
                body.addArrangedSubview(MailboxActionButton("在 Finder 中打开", symbol: "folder") {
                    NSWorkspace.shared.activateFileViewerSelecting([session.workspace])
                })
                body.addArrangedSubview(MailboxUI.paragraph("终端进程状态不代表某项任务已完成。选中要保存的文件后，可手动收取成果。", size: 12))
            } else { body.addArrangedSubview(MailboxUI.paragraph("新建一个终端会话后，查看工作目录和运行状态。", size: 12)) }
        default:
            let receipts = store.snapshot.threads.filter { $0.terminalSessionID != nil && $0.terminalSessionID == selectedID }
            body.addArrangedSubview(MailboxUI.label("已收取 · \(receipts.count)", size: 12, weight: .medium))
            for receipt in receipts {
                let button = MailboxActionButton(receipt.displayTitle, symbol: "tray") { [weak self] in self?.openReceipt?(receipt.id) }
                button.alignment = .left; button.toolTip = receipt.displayTitle
                body.addArrangedSubview(button)
            }
            if receipts.isEmpty {
                body.addArrangedSubview(MailboxUI.paragraph("选择本次工作的图片或文本，预览后收入收件箱。收件会保留与此终端的关联。", size: 12))
            }
            body.addArrangedSubview(MailboxUI.paragraph("切换工作区后，终端会继续运行。\n已收取的内容会保存在本地。", size: 11))
        }
        inspectorBody.addArrangedSubview(MailboxUI.padded(body, inset: 16))
    }
    private func chooseFile() {
        guard let window = view.window, let session = selectedSession else { status.stringValue = "请先新建终端会话"; return }
        let panel = NSOpenPanel(); panel.canChooseDirectories = false; panel.allowsMultipleSelection = false
        panel.directoryURL = session.workspace; panel.prompt = "预览"
        panel.message = "选择此会话的图片或文本成果"
        let sessionID = session.id
        panel.beginSheetModal(for: window) { [weak self] result in
            guard result == .OK, let url = panel.url, self?.selectedID == sessionID else { return }
            self?.preview(url)
        }
    }
    func preview(_ url: URL) {
        guard selectedSession != nil else { return }
        do { selection = try MailboxCollectedFile.read(url); selectionSessionID = selectedID; tabs.selectedSegment = 0; renderInspector() }
        catch { status.stringValue = error.localizedDescription }
    }
    func collectSelection() {
        guard let session = selectedSession, let selection, selectionSessionID == session.id else { return }
        do {
            let receipt = try store.importTerminalResult(sessionID: session.id, sourceName: "\(session.kind.rawValue) · \(session.workspace.lastPathComponent)",
                title: selection.url.lastPathComponent, body: selection.body, pngData: selection.pngData)
            self.selection = nil; self.selectionSessionID = nil
            renderInspector(); status.stringValue = "已收入收件箱 · \(receipt.displayTitle)"
        } catch { status.stringValue = error.localizedDescription }
    }
}
