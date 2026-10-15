import SwiftUI
import UniformTypeIdentifiers

struct TypingStatsCardView: View {
    @Environment(\.scenePhase) private var phase
    @State private var png: Data?
    @State private var message = ""
    @State private var sharing = false
    @State private var exporting = false
    @State private var savingPhoto = false
    private let store = TypingCardStore()
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 20) {
                if let png, let image = UIImage(data: png) {
                    Image(uiImage: image).resizable().scaledToFit().accessibilityLabel(L("最新打字统计卡片", "Latest typing stats card"))
                    Button {
                        guard !savingPhoto else { return }; savingPhoto = true
                        Task { defer { savingPhoto = false }
                            do { try await TypingCardPhotos().save(png); message = L("已保存到相册，可在聊天中选择这张照片。", "Saved to Photos. Select this image in your chat.") }
                            catch { message = error.localizedDescription }
                        }
                    } label: { Label(savingPhoto ? L("正在保存…", "Saving…") : L("保存到相册", "Save to Photos"), systemImage: "photo.badge.arrow.down") }
                        .buttonStyle(.borderedProminent).disabled(savingPhoto).accessibilityIdentifier("typingCard.photos")
                    HStack {
                        Button { sharing = true } label: { Label(L("分享图片", "Share image"), systemImage: "square.and.arrow.up") }.accessibilityIdentifier("typingCard.share")
                        Button { exporting = true } label: { Label(L("保存到文件", "Save to Files"), systemImage: "folder") }.accessibilityIdentifier("typingCard.export")
                    }.buttonStyle(.bordered)
                    Button { do { try TypingStatsCard.copy(png); message = L("图片已复制，可到聊天输入框粘贴。", "Image copied. Paste it into a chat field.") } catch { message = error.localizedDescription } } label: {
                        Label(L("复制图片", "Copy image"), systemImage: "doc.on.doc")
                    }.accessibilityIdentifier("typingCard.copy")
                    Button { UIPasteboard.general.setItems([[UTType.utf8PlainText.identifier: TypingStatsCard.github]], options: [.localOnly: true]); message = L("GitHub 地址已复制。", "GitHub link copied.") } label: {
                        Label(L("单独复制 GitHub 地址", "Copy GitHub link separately"), systemImage: "link")
                    }
                    Text(L("图片内的地址用于展示；需要可点击的链接时，请单独复制地址。", "The address in the image is visual. Copy the link separately for a clickable URL.")).font(.footnote).foregroundStyle(.secondary)
                    Button(role: .destructive) { do { try store.remove(); self.png = nil; message = "" } catch { message = error.localizedDescription } } label: { Label(L("删除这张卡片", "Delete this card"), systemImage: "trash") }
                } else {
                    ContentUnavailableView(L("还没有统计卡片", "No stats card yet"), systemImage: "square.grid.3x3", description: Text(L("在默认 Buffer 中打几个字，点击统计状态框，切换到“图片”，再选“保存到 App”。", "Type a little in Default Buffer, tap the stats readout, select Image, then choose Save to app.")))
                }
                if !message.isEmpty { Text(message).font(.callout).accessibilityIdentifier("typingCard.message") }
                Text(L("图片是可选的分享方式。只保存你主动导出的最新一张统计图片，不含输入正文。", "Images are an optional sharing format. Only the latest card you explicitly save is kept, without typed text.")).font(.footnote).foregroundStyle(.secondary)
            }.padding()
        }.navigationTitle(L("打字统计卡片", "Typing stats card"))
        .task { reload() }.onChange(of: phase) { _, value in if value == .active { reload() } }
        .sheet(isPresented: $sharing) { if let png, let image = UIImage(data: png) { TypingCardShareSheet(image: image) } }
        .fileExporter(isPresented: $exporting, document: TypingCardDocument(png: png ?? Data()), contentType: .png, defaultFilename: "XSIME-typing-card") { result in
            switch result { case .success: message = L("图片已保存。", "Image saved."); case .failure(let error): message = error.localizedDescription }
        }
    }
    private func reload() { do { png = try store.load() } catch { png = nil; message = error.localizedDescription } }
}
private struct TypingCardDocument: FileDocument {
    static var readableContentTypes: [UTType] { [.png] }
    let png: Data
    init(png: Data) { self.png = png }
    init(configuration: ReadConfiguration) throws { png = configuration.file.regularFileContents ?? Data(); try TypingCardStore.validate(png) }
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper { try TypingCardStore.validate(png); return FileWrapper(regularFileWithContents: png) }
}
private struct TypingCardShareSheet: UIViewControllerRepresentable {
    let image: UIImage
    func makeUIViewController(context: Context) -> UIActivityViewController { UIActivityViewController(activityItems: [image], applicationActivities: nil) }
    func updateUIViewController(_ controller: UIActivityViewController, context: Context) {}
}
