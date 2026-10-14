import SwiftUI
import UniformTypeIdentifiers
import RimesCore

/// Import, deployment and activation are distinct choices. A package never replaces chord settings.
struct RimeSchemesView: View {
    @State private var importing = false
    @State private var inspecting = false
    @State private var review: RimeSchemeImportReview?
    @State private var message = ""
    @State private var error: String?
    @State private var packages: [RimeSchemePackage] = []
    @State private var active: RimeSchemeSelection?
    @State private var deploying = false
    @State private var deploymentStatus = ""
    @State private var packageToDelete: RimeSchemePackage?
    private let store = RimeSchemeStore()

    var body: some View {
        List {
            Section {
                Label("把完整方案带到 iPhone", systemImage: "shippingbox")
                    .font(.headline)
                Text("导入方案、词典与支持的 Lua 扩展，部署成功后再选择启用。只导入文件不会切换当前输入方式。")
                    .font(.subheadline).foregroundStyle(.secondary)
            }
            Section {
                LabeledContent("当前输入", value: activeName)
                if active != nil {
                    Button("停用导入方案，恢复内置") {
                        do { try store.activate(nil); reload(); message = "已恢复原来的内置输入方式。" }
                        catch { self.error = error.localizedDescription }
                    }.accessibilityIdentifier("rimeSchemes.deactivate")
                }
            } footer: {
                Text("启用导入方案会在下次唤起键盘时生效。原有并击选择和键位仍被保留，停用后恢复。")
            }
            Section {
                if packages.isEmpty {
                    Text("还没有安装的方案包。可以导入 Rime ZIP 或带编码的 UTF-8 文本词库。")
                        .font(.subheadline).foregroundStyle(.secondary)
                }
                ForEach(packages, id: \.id) { package in
                    HStack {
                        VStack(alignment: .leading, spacing: 4) {
                            Text(package.name).font(.headline)
                            Text(package.importedAt, style: .date).font(.caption).foregroundStyle(.secondary)
                        }
                        Spacer()
                        Button(role: .destructive) { packageToDelete = package } label: { Image(systemName: "trash") }
                            .buttonStyle(.borderless).disabled(deploying)
                            .accessibilityLabel("删除方案包“\(package.name)”")
                            .accessibilityIdentifier("rimeSchemes.delete.\(package.id)")
                    }
                    ForEach(package.schemas, id: \.id) { schema in
                        installedScheme(schema, package: package)
                    }
                    if !package.warnings.isEmpty {
                        DisclosureGroup("部署与兼容说明") {
                            ForEach(Array(package.warnings.enumerated()), id: \.offset) { item in
                                Text(item.element).font(.caption).foregroundStyle(.secondary)
                            }
                        }
                    }
                }
            } header: { Text("已安装的方案") }
            Section {
                Button { importing = true } label: {
                    Label("导入方案或文本词库", systemImage: "square.and.arrow.down")
                }
                .disabled(inspecting || deploying)
                .accessibilityIdentifier("rimeSchemes.import")
                if inspecting {
                    HStack(spacing: 10) {
                        ProgressView()
                        Text("正在检查方案与依赖…").font(.subheadline)
                    }
                }
            } footer: {
                Text("支持上游 Rime ZIP，以及每行“词条⇥编码⇥可选权重”的 UTF-8 TXT / TSV。保留词典子目录、Lua 和 OpenCC；其他输入法的键盘布局不覆盖当前键位。")
            }
            if !message.isEmpty {
                Section { Label(message, systemImage: "checkmark.circle").font(.subheadline).foregroundStyle(.secondary) }
            }
        }
        .navigationTitle("Rime 方案包")
        .tint(.indigo)
        .onAppear(perform: reload)
        .confirmationDialog("删除方案包？", isPresented: Binding(get: { packageToDelete != nil }, set: { if !$0 { packageToDelete = nil } }), titleVisibility: .visible, presenting: packageToDelete) { package in
            Button("删除“\(package.name)”", role: .destructive) { remove(package) }
            Button("取消", role: .cancel) { packageToDelete = nil }
        } message: { package in
            Text("将移除包内的 \(package.schemas.count) 个输入方案。若正在使用其中的方案，下次唤起键盘时恢复原来的内置输入方式。已学习的用户词库会保留。")
        }
        .fileImporter(isPresented: $importing, allowedContentTypes: [.zip, .plainText, .tabSeparatedText], allowsMultipleSelection: false) { result in
            switch result {
            case .success(let urls): if let url = urls.first { inspect(url) }
            case .failure(let failure): error = failure.localizedDescription
            }
        }
        .sheet(isPresented: Binding(get: { review != nil }, set: { if !$0 { review = nil } })) {
            if let review {
                RimeSchemeImportReviewView(review: review, deploying: deploying, status: deploymentStatus) { selected in
                    deploy(review, selected: selected)
                }
                .interactiveDismissDisabled(deploying)
            }
        }
        .alert("操作未完成", isPresented: Binding(get: { error != nil }, set: { if !$0 { error = nil } })) {
            Button("好") { error = nil }
        } message: { Text(error ?? "") }
    }
    private var activeName: String {
        guard let active,
              let package = packages.first(where: { $0.id == active.packageID }),
              let schema = package.schemas.first(where: { $0.id == active.schemaID }) else { return "内置方案" }
        return schema.name
    }
    private func reload() {
        let library = store.load()
        packages = library.packages
        active = library.active
    }
    private func remove(_ package: RimeSchemePackage) {
        do {
            try store.remove(packageID: package.id)
            packageToDelete = nil; reload()
            message = "已删除“\(package.name)”。"
        } catch { self.error = error.localizedDescription }
    }
    private func installedScheme(_ schema: ImportedRimeSchema, package: RimeSchemePackage) -> some View {
        let selection = RimeSchemeSelection(packageID: package.id, schemaID: schema.id)
        return VStack(alignment: .leading, spacing: 10) {
            HStack {
                VStack(alignment: .leading, spacing: 4) {
                    Text(schema.name)
                    Text(schema.id).font(.caption.monospaced()).foregroundStyle(.secondary)
                }
                Spacer()
                if active == selection {
                    Label("已启用", systemImage: "checkmark.circle.fill").font(.caption).foregroundStyle(.indigo)
                }
            }
            HStack {
                NavigationLink {
                    ImportedRimeSchemeTestView(selection: selection, name: schema.name)
                } label: { Label("测试候选", systemImage: "text.magnifyingglass") }
                .accessibilityIdentifier("rimeSchemes.test.\(schema.id)")
                Spacer()
                if active != selection {
                    Button("启用此方案") {
                        do {
                            try store.activate(selection)
                            reload()
                            message = "已启用“\(schema.name)”，请重新唤起键盘。"
                        } catch { self.error = error.localizedDescription }
                    }
                    .buttonStyle(.borderless)
                    .accessibilityIdentifier("rimeSchemes.activate.\(schema.id)")
                }
            }.font(.subheadline)
        }.padding(.vertical, 5)
    }
    private func deploy(_ review: RimeSchemeImportReview, selected: [String]) {
        guard !deploying else { return }
        deploying = true
        deploymentStatus = "准备导入方案与依赖…"
        Task { @MainActor in
            defer { deploying = false }
            do {
                let package = try await RimeSchemeInstaller.install(review: review, selectedSchemaIDs: selected) { progress in
                    Task { @MainActor in deploymentStatus = progress }
                }
                self.review = nil
                reload()
                message = "“\(package.name)”已部署。请先测试候选，再选择要启用的方案。"
            } catch {
                deploymentStatus = "部署未完成：\(error.localizedDescription)"
            }
        }
    }
    private func inspect(_ url: URL) {
        inspecting = true
        deploymentStatus = ""
        Task { @MainActor in
            defer { inspecting = false }
            do {
                review = try await Task.detached(priority: .userInitiated) {
                    try RimeSchemeImportService.inspect(url: url)
                }.value
            } catch { self.error = error.localizedDescription }
        }
    }
}

private struct RimeSchemeImportReviewView: View {
    @Environment(\.dismiss) private var dismiss
    let review: RimeSchemeImportReview
    let deploying: Bool
    let status: String
    let onImport: ([String]) -> Void
    @State private var selectedIDs = Set<String>()

    var body: some View {
        NavigationStack {
            List {
                Section {
                    Text(review.sourceName).font(.headline)
                    LabeledContent("文件数量", value: "\(review.fileCount) 项")
                    LabeledContent("解压后大小", value: ByteCountFormatter.string(fromByteCount: Int64(review.expandedBytes), countStyle: .file))
                }
                Section {
                    ForEach(review.schemes, id: \.id) { scheme in
                        VStack(alignment: .leading, spacing: 8) {
                            Toggle(isOn: Binding(get: { selectedIDs.contains(scheme.id) }, set: { on in
                                if on { selectedIDs.insert(scheme.id) } else { selectedIDs.remove(scheme.id) }
                            })) {
                                VStack(alignment: .leading, spacing: 4) {
                                    HStack {
                                        Text(scheme.name)
                                        if scheme.recommended { Text("推荐").font(.caption2).foregroundStyle(.indigo) }
                                    }
                                    Text(scheme.id).font(.caption.monospaced()).foregroundStyle(.secondary)
                                }
                            }
                            .disabled(deploying || !scheme.blockingIssues.isEmpty)
                            .accessibilityIdentifier("rimeSchemes.review.\(scheme.id)")
                            if !scheme.dependencies.isEmpty {
                                Text("依赖：" + scheme.dependencies.joined(separator: "、"))
                                    .font(.caption).foregroundStyle(.secondary)
                            }
                            ForEach(Array(scheme.warnings.enumerated()), id: \.offset) { item in
                                Label(item.element, systemImage: "info.circle").font(.caption).foregroundStyle(.secondary)
                            }
                            ForEach(Array(scheme.blockingIssues.enumerated()), id: \.offset) { item in
                                Label(item.element, systemImage: "xmark.octagon").font(.caption).foregroundStyle(.red)
                            }
                        }.padding(.vertical, 3)
                    }
                } header: { Text("选择要安装的方案") } footer: {
                    Text("包内依赖会一并准备。部署完成后仍需在列表里明确启用，不会自动替换当前并击。")
                }
                if !review.warnings.isEmpty {
                    Section("检查与依赖说明") {
                        ForEach(Array(review.warnings.enumerated()), id: \.offset) { item in
                            Label(item.element, systemImage: "info.circle")
                                .font(.subheadline).foregroundStyle(.secondary)
                        }
                    }
                }
                if !review.unsupportedFeatures.isEmpty {
                    Section("兼容性限制") {
                        ForEach(Array(review.unsupportedFeatures.enumerated()), id: \.offset) { item in
                            Label(item.element, systemImage: "exclamationmark.circle")
                                .font(.subheadline).foregroundStyle(.orange)
                        }
                    }
                }
                Section {
                    if !status.isEmpty {
                        HStack(alignment: .top, spacing: 10) {
                            if deploying { ProgressView() }
                            Text(status).font(.subheadline).foregroundStyle(.secondary)
                        }.accessibilityIdentifier("rimeSchemes.deploymentStatus")
                    }
                    Button { onImport(Array(selectedIDs).sorted()) } label: {
                        Label("导入并部署所选方案", systemImage: "arrow.down.doc")
                    }
                    .disabled(selectedIDs.isEmpty || deploying || review.schemes.contains { selectedIDs.contains($0.id) && !$0.blockingIssues.isEmpty })
                    .accessibilityIdentifier("rimeSchemes.deploy")
                } footer: {
                    Text("只运行支持的输入引擎扩展。键盘主题、仓鼠专用操作和未支持功能以兼容性说明为准。")
                }
            }
            .navigationTitle("检查方案包")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar { ToolbarItem(placement: .cancellationAction) { Button("取消") { dismiss() }.disabled(deploying) } }
            .tint(.indigo)
            .onAppear {
                if selectedIDs.isEmpty {
                    let available = review.schemes.filter { $0.blockingIssues.isEmpty }
                    let recommended = available.filter(\.recommended)
                    if !recommended.isEmpty { selectedIDs = Set(recommended.map(\.id)) }
                    else if let starcat = available.first(where: { $0.id == "xmjd6" }) { selectedIDs = [starcat.id] }
                    else if let first = available.first { selectedIDs = [first.id] }
                }
            }
        }
    }
}


private struct ImportedRimeSchemeTestView: View {
    let selection: RimeSchemeSelection
    let name: String
    @State private var code = ""
    @State private var preedit = ""
    @State private var candidates: [String] = []
    @State private var committed = ""
    @State private var message = ""
    @State private var testing = false

    var body: some View {
        Form {
            Section {
                Text(name).font(.headline)
                Text(selection.schemaID).font(.caption.monospaced()).foregroundStyle(.secondary)
                Text("这里直接检查已部署方案的引擎结果，不更改正在使用的键盘。可先输入已知编码确认候选，再返回启用。")
                    .font(.subheadline).foregroundStyle(.secondary)
            }
            Section {
                TextField("输入编码，例如方案中的字词编码", text: $code)
                    .textInputAutocapitalization(.never).autocorrectionDisabled()
                    .accessibilityIdentifier("rimeSchemes.test.code")
                Button {
                    test()
                } label: {
                    Label(testing ? "检查中…" : "检查候选", systemImage: "play.fill")
                }
                .disabled(testing || code.isEmpty)
                .accessibilityIdentifier("rimeSchemes.test.run")
            } header: { Text("本机引擎检查") }
            if !preedit.isEmpty { Section("组字") { Text(preedit).textSelection(.enabled) } }
            if !committed.isEmpty { Section("引擎提交结果") { Text(committed).textSelection(.enabled) } }
            if !candidates.isEmpty {
                Section("候选") {
                    ForEach(Array(candidates.enumerated()), id: \.offset) { item in
                        HStack { Text("\(item.offset + 1)").foregroundStyle(.secondary); Text(item.element) }
                    }
                }.accessibilityIdentifier("rimeSchemes.test.candidates")
            }
            if !message.isEmpty { Section { Text(message).font(.subheadline).foregroundStyle(.secondary) } }
            Section {
                NavigationLink { PlaygroundView() } label: { Label("前往真实键盘输入体验", systemImage: "keyboard") }
            } footer: { Text("真实键盘体验使用当前启用的方案。此页的候选检查不会自动启用本方案。") }
        }
        .navigationTitle("测试方案")
        .navigationBarTitleDisplayMode(.inline)
    }
    private func test() {
        guard code.unicodeScalars.count <= 64 else {
            message = "候选检查每次最多输入 64 个字符。"
            return
        }
        testing = true
        message = ""; candidates = []; committed = ""; preedit = ""
        let input = code
        let selected = selection
        Task { @MainActor in
            defer { testing = false }
            let result = await Task.detached(priority: .userInitiated) { () -> ImportedRimeProbeResult in
                let engine = MobileEngine()
                guard engine.selectImported(selected) else { return .init(loaded: false, error: engine.lastError) }
                defer { engine.clear() }
                engine.clear()
                var snapshot = EngineSnapshot(), committed = ""
                for scalar in input.unicodeScalars {
                    snapshot = engine.process(key: Int32(scalar.value))
                    committed += snapshot.commit
                    if !engine.lastError.isEmpty {
                        return .init(loaded: true, preedit: snapshot.preedit, candidates: snapshot.candidates, committed: committed, error: engine.lastError)
                    }
                }
                return .init(loaded: true, preedit: snapshot.preedit, candidates: snapshot.candidates, committed: committed)
            }.value
            guard result.loaded else {
                message = result.error.isEmpty ? "方案未能加载，请返回检查部署记录。当前输入方式没有变化。" : "方案加载失败：\(result.error)"
                return
            }
            preedit = result.preedit
            candidates = result.candidates
            committed = result.committed
            if !result.error.isEmpty {
                message = "输入引擎报告错误：\(result.error)"
            } else if preedit.isEmpty && candidates.isEmpty && committed.isEmpty {
                message = "引擎已加载，此编码没有产生候选；请尝试该方案支持的其他编码。"
            } else { message = "本机引擎检查完成，尚未因此切换当前方案。" }
        }
    }
}

private struct ImportedRimeProbeResult: Sendable {
    var loaded: Bool
    var preedit = ""
    var candidates: [String] = []
    var committed = ""
    var error = ""
}
