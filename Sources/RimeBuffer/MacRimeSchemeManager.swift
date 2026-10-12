import Cocoa
import UniformTypeIdentifiers

private final class SchemeManagerButton: NSButton {
    private let handler: () -> Void
    init(_ title: String, action: @escaping () -> Void) {
        handler = action
        super.init(frame: .zero)
        self.title = title; bezelStyle = .rounded; target = self; self.action = #selector(invoke)
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }
    @objc private func invoke() { handler() }
}

final class MacRimeSchemeManager: NSWindowController {
    static let shared = MacRimeSchemeManager()
    private let library = MacRimeSchemeLibrary.shared
    private var busy = false
    private var message = ""
    private init() {
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 680, height: 560),
            styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
        window.title = "Rime 方案与词库"; window.minSize = NSSize(width: 540, height: 400)
        super.init(window: window)
        window.center()
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }
    func show() { render(); showWindow(nil); window?.makeKeyAndOrderFront(nil) }
    private func render() {
        guard let window else { return }
        let scroll = NSScrollView(); scroll.hasVerticalScroller = true; scroll.drawsBackground = false
        let column = NSStackView(); column.orientation = .vertical; column.alignment = .leading; column.spacing = 12
        column.edgeInsets = NSEdgeInsets(top: 20, left: 20, bottom: 20, right: 20)
        column.translatesAutoresizingMaskIntoConstraints = false
        scroll.documentView = column; window.contentView = scroll
        column.widthAnchor.constraint(equalTo: scroll.contentView.widthAnchor).isActive = true
        func label(_ text: String, title: Bool = false) {
            let field = NSTextField(wrappingLabelWithString: text)
            field.font = .systemFont(ofSize: title ? 17 : 12, weight: title ? .semibold : .regular)
            column.addArrangedSubview(field)
            field.widthAnchor.constraint(equalTo: column.widthAnchor, constant: -40).isActive = true
        }
        func button(_ text: String, _ action: @escaping () -> Void) {
            let control = SchemeManagerButton(text, action: action); control.isEnabled = !busy; column.addArrangedSubview(control)
        }
        label("方案与词库", title: true)
        label("支持上游 Rime ZIP 和带编码的 UTF-8 TXT / TSV。每行词条、编码、可选整数权重以制表符分隔。原文件保持不变，编译成功后再启用。")
        label("当前：" + (library.runtimePackage?.name ?? "内置方案"))
        if library.runtimePackage != nil {
            button("恢复内置方案并重启输入法") { [weak self] in self?.activate(nil, schema: nil) }
        }
        button(busy ? "正在处理…" : "导入方案或文本词库") { [weak self] in self?.chooseFile() }
        if !message.isEmpty { label(message) }
        for package in library.packages() {
            label(package.name, title: true)
            for schema in package.schemas {
                button("启用“\(schema.name)”并重启输入法") { [weak self] in self?.activate(package, schema: schema.id) }
            }
            if package.id != library.runtimePackage?.id {
                button("删除“\(package.name)”") { [weak self] in
                    guard let self else { return }
                    let alert = NSAlert(); alert.messageText = "删除“\(package.name)”？"
                    alert.informativeText = "删除其源文件和编译缓存，保留学习词库。"
                    alert.addButton(withTitle: "删除"); alert.addButton(withTitle: "取消")
                    guard alert.runModal() == .alertFirstButtonReturn else { return }
                    do { try self.library.remove(package); self.message = "已删除方案包。" }
                    catch { self.message = error.localizedDescription }
                    self.render()
                }
            }
        }
    }
    private func chooseFile() {
        let panel = NSOpenPanel(); panel.allowedContentTypes = [.zip, .plainText, .tabSeparatedText]
        panel.allowsMultipleSelection = false; panel.canChooseDirectories = false
        guard panel.runModal() == .OK, let url = panel.url else { return }
        busy = true; message = "正在检查文件…"; render()
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            let result = Result { try RimeSchemeImportService.inspect(url: url) }
            DispatchQueue.main.async {
                guard let self else { return }; self.busy = false
                switch result {
                case .success(let review): self.review(review)
                case .failure(let error): self.message = error.localizedDescription; self.render()
                }
            }
        }
    }
    private func review(_ review: RimeSchemeImportReview) {
        let alert = NSAlert(); alert.messageText = "选择需要的输入方案"
        alert.informativeText = (review.details + review.warnings + review.unsupportedFeatures).joined(separator: "\n")
        var choices = [NSButton]()
        let column = NSStackView(); column.orientation = .vertical; column.alignment = .leading
        for schema in review.schemes {
            let button = NSButton(checkboxWithTitle: schema.name + " · " + schema.id, target: nil, action: nil)
            button.isEnabled = schema.blockingIssues.isEmpty
            button.state = schema.recommended && button.isEnabled ? .on : .off
            button.toolTip = (schema.blockingIssues + schema.warnings).joined(separator: "\n")
            choices.append(button); column.addArrangedSubview(button)
        }
        column.frame = NSRect(x: 0, y: 0, width: 460, height: max(32, choices.count * 26))
        let scroll = NSScrollView(frame: NSRect(x: 0, y: 0, width: 480, height: min(280, max(40, choices.count * 26))))
        scroll.documentView = column; scroll.hasVerticalScroller = true; alert.accessoryView = scroll
        alert.addButton(withTitle: "编译并安装"); alert.addButton(withTitle: "取消")
        guard alert.runModal() == .alertFirstButtonReturn else { render(); return }
        let ids = zip(review.schemes, choices).filter { $0.1.state == .on }.map { $0.0.id }
        guard !ids.isEmpty else { message = "请至少选择一个可用方案。"; render(); return }
        busy = true; message = "正在编译和验证，大词库首次部署可能需要几分钟…"; render()
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self else { return }
            let result = Result { try self.library.install(review, selected: ids) }
            DispatchQueue.main.async {
                self.busy = false
                switch result {
                case .success: self.message = "已安装。选择启用后生效，当前输入环境未改变。"
                case .failure(let error): self.message = error.localizedDescription
                }
                self.render()
            }
        }
    }
    private func activate(_ package: MacRimeSchemePackage?, schema: String?) {
        busy = true; message = "正在准备输入环境…"; render()
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            guard let self else { return }
            do {
                try self.library.activate(package, schema: schema)
                DispatchQueue.main.async { InputMetricsPersistence.saveNow(); exit(0) }
            } catch {
                DispatchQueue.main.async { self.busy = false; self.message = error.localizedDescription; self.render() }
            }
        }
    }
}
