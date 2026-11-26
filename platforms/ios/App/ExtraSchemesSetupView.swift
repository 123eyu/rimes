import AVFoundation
import Speech
import SwiftUI

/// Opt-in for the keyboard's experimental voice and handwriting schemes. The keyboard cannot
/// show permission prompts, so voice input's two permissions are requested here once.
struct ExtraSchemesSetupView: View {
    @AppStorage(ExtraSchemeSettings.enabledKey, store: ExtraSchemeSettings.store) private var enabled = false
    @AppStorage(ExtraSchemeSettings.handwritingKey, store: ExtraSchemeSettings.store) private var handwriting = false
    @State private var microphone = AVAudioApplication.shared.recordPermission
    @State private var speech = SFSpeechRecognizer.authorizationStatus()
    private var onDevice: Bool { SFSpeechRecognizer(locale: Locale(identifier: "zh-CN"))?.supportsOnDeviceRecognition ?? false }

    var body: some View {
        List {
            Section {
                Toggle(L("手写方案", "Handwriting scheme"), isOn: $handwriting)
            } footer: {
                Text(L("在键盘“输入方案”菜单里选择“手写”。在书写区写字，候选出现后点选上屏；停笔片刻后接着写，会先上屏第一个候选。识别在本机完成，不联网，不需要完全访问。", "Choose Handwriting in the keyboard's Input scheme menu. Write on the pad and tap a candidate to insert it; after a short pause, writing again inserts the first candidate. Recognition runs on the device, offline, without Full Access."))
            }
            Section {
                Toggle(L("语音方案", "Voice scheme"), isOn: $enabled)
            } footer: {
                Text(L("在键盘“输入方案”菜单里选择“语音”。点麦克风开始说话，再点一次上屏，不需要跳回 App。需要完全访问和下面两项权限。它借助系统 WebKit 录音，iOS 更新后可能失效。", "Choose Voice in the keyboard's Input scheme menu. Tap the microphone to speak and tap again to insert, without switching to this app. Needs Full Access and both permissions below. It records through the system's WebKit and may stop working after an iOS update."))
            }
            Section(L("语音权限", "Voice permissions")) {
                permissionRow(L("麦克风", "Microphone"), granted: microphone == .granted, denied: microphone == .denied) {
                    AVAudioApplication.requestRecordPermission { _ in Task { @MainActor in microphone = AVAudioApplication.shared.recordPermission } }
                }
                permissionRow(L("语音识别", "Speech recognition"), granted: speech == .authorized, denied: speech == .denied || speech == .restricted) {
                    SFSpeechRecognizer.requestAuthorization { status in Task { @MainActor in speech = status } }
                }
            }
            Section(L("语音识别方式", "Speech recognition")) {
                Text(onDevice
                     ? L("本机支持中文离线识别，录音不离开设备。", "This device recognizes Chinese offline; audio stays on the device.")
                     : L("本机暂不支持中文离线识别，识别时音频会发送给 Apple。", "This device cannot recognize Chinese offline yet; audio is sent to Apple for recognition."))
            }
        }
        .navigationTitle(L("语音与手写（实验）", "Voice & handwriting (experimental)"))
    }

    @ViewBuilder private func permissionRow(_ title: String, granted: Bool, denied: Bool, request: @escaping () -> Void) -> some View {
        HStack {
            Text(title)
            Spacer()
            if granted { Label(L("已授权", "Granted"), systemImage: "checkmark.circle.fill").foregroundStyle(.green) }
            else if denied { Button(L("去设置开启", "Open Settings")) { if let url = URL(string: UIApplication.openSettingsURLString) { UIApplication.shared.open(url) } } }
            else { Button(L("授权", "Allow"), action: request) }
        }
    }
}

/// Shared with the keyboard's `ExtraScheme.enabledKey`; only the app writes these.
enum ExtraSchemeSettings {
    static let enabledKey = "voiceDictation.enabled"
    static let handwritingKey = "handwriting.enabled"
    static var store: UserDefaults? { UserDefaults(suiteName: ConfigurationStore.groupID) }
}
