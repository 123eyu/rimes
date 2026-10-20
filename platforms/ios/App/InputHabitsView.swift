import SwiftUI
import RimesCore

struct InputHabitsView: View {
    @State private var values = InputHabits()
    @State private var selection = ""
    @State private var error: String?
    private struct Profile: Identifiable {
        var id: String
        var title: String
        var file: URL
    }
    private var profiles: [Profile] {
        var result: [Profile] = []
        if let root = Bundle.main.url(forResource: "EngineData", withExtension: nil) {
            for scheme in InputScheme.allCases {
                result.append(.init(id: "builtin:" + scheme.schemaID, title: scheme.title,
                    file: root.appendingPathComponent("build/\(scheme.schemaID).schema.yaml")))
            }
        }
        let store = RimeSchemeStore()
        for package in store.load().packages {
            for scheme in package.schemas {
                if let resolved = store.resolve(.init(packageID: package.id, schemaID: scheme.id)) {
                    result.append(.init(id: package.id + ":" + scheme.id, title: package.name + " · " + scheme.name,
                        file: resolved.resources.appendingPathComponent("build/\(scheme.id).schema.yaml")))
                }
            }
        }
        return result
    }
    private var options: [InputHabitOption] {
        guard let profile = profiles.first(where: { $0.id == selection }),
              let array = RimeMobile.keyboardSchemaInfo(atPath: profile.file.path)["inputOptions"],
              let data = try? JSONSerialization.data(withJSONObject: array),
              let options = try? JSONDecoder().decode([InputHabitOption].self, from: data) else { return [] }
        return options
    }
    var body: some View {
        List {
            Picker(L("输入方案", "Input scheme"), selection: $selection) {
                ForEach(profiles) { Text($0.title).tag($0.id) }
            }
            Section {
                ForEach(options) { option in
                    Picker(option.off + " / " + option.on, selection: Binding(get: {
                        values[selection, option.name].map { $0 ? 2 : 1 } ?? 0
                    }, set: { index in
                        do {
                            try mobileInputHabitsStore().set(index == 0 ? nil : index == 2, schema: selection, option: option.name)
                            values = mobileInputHabitsStore().load()
                        } catch { self.error = L("未能保存，原设置保持不变。", "Could not save. Your previous settings are preserved.") }
                    })) {
                        Text(L("跟随方案", "Use scheme default")).tag(0)
                        Text(option.off).tag(1); Text(option.on).tag(2)
                    }.accessibilityIdentifier("inputHabits.\(option.name)")
                }
                if options.isEmpty { Text(L("此方案未提供可配置的双状态开关。", "This scheme does not expose configurable two-state switches.")) }
            } footer: {
                Text(L("各方案独立保存，下次打开键盘时应用。只显示方案实际提供的开关；原方案文件和用户补丁保持不变。", "Saved separately for each scheme and applied when the keyboard opens. Only supported switches are shown. Your scheme files and custom patches are preserved."))
            }
        }
        .navigationTitle(L("输入习惯", "Input preferences"))
        .onAppear {
            values = mobileInputHabitsStore().load()
            if selection.isEmpty {
                let imported = RimeSchemeStore().load().active
                selection = imported.map { $0.packageID + ":" + $0.schemaID } ?? "builtin:" + ConfigurationStore().load().scheme.schemaID
            }
        }
        .alert(L("无法保存", "Could not save"), isPresented: Binding(get: { error != nil }, set: { if !$0 { error = nil } })) {
            Button(L("好", "OK")) { error = nil }
        } message: { Text(error ?? "") }
    }
}
