import SwiftUI

struct DataManagementView: View {
    @EnvironmentObject private var model: SettingsModel
    @State private var confirmingReset = false
    @State private var requested = false
    var body: some View {
        Form {
            Section {
                Text(L("联想学习会记住词语之间的接续关系，让常用搭配优先出现。记录只保存在本机键盘中。", "Learned associations remember which words you type together, putting familiar continuations first. Records stay in the keyboard on this device."))
                Button(role: .destructive) { confirmingReset = true } label: {
                    Label(L("清除联想学习记录", "Clear learned associations"), systemImage: "text.badge.xmark").foregroundStyle(.red)
                }.accessibilityIdentifier("dataManagement.clearAssociations")
                if requested {
                    Text(L("清除请求已保存，下次打开 \(ProductBrand.name) 键盘时生效。", "Request saved. It takes effect the next time you open the \(ProductBrand.name) keyboard."))
                        .font(.footnote).foregroundStyle(.secondary)
                        .accessibilityIdentifier("dataManagement.resetRequested")
                }
            } header: { Text(L("联想学习", "Learned associations")) }
              footer: { Text(L("清除后会重新开始学习；不会删除输入方案、词库或键位配置。", "Learning starts again after clearing. Input schemes, dictionaries and key layouts are kept.")) }
        }
        .navigationTitle(L("数据管理", "Data management"))
        .alert(L("清除联想学习记录？", "Clear learned associations?"), isPresented: $confirmingReset) {
            Button(L("清除记录", "Clear records"), role: .destructive) { requestReset() }
                .accessibilityIdentifier("dataManagement.confirmClearAssociations")
            Button(L("取消", "Cancel"), role: .cancel) { }
        } message: {
            Text(L("此操作无法撤销。仅清除学习到的词语接续关系，下次打开 \(ProductBrand.name) 键盘时执行。", "This cannot be undone. Only learned word continuations are cleared, when you next open the \(ProductBrand.name) keyboard."))
        }
    }
    private func requestReset() {
        var next = ConfigurationStore().load()
        next.associationResetRevision = UUID()
        do {
            try ConfigurationStore().save(next)
            model.value = next; requested = true
        } catch { model.error = error.localizedDescription }
    }
}
