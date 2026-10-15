import UIKit
#if KEYBOARD_LAYOUT_TESTS
@testable import RIMES
#endif

/// One entry from the Default Buffer readout, contained in the keyboard's frame.
final class TypingCardPreview: UIView {
    enum Format: Int { case blocks, text, image }
    var onClose: (() -> Void)?
    var onCopy: (() -> Void)?
    var onSave: (() -> Void)?
    var onText: ((String) -> Void)?
    var onCopyText: ((String) -> Void)?
    var onPhotos: (() -> Void)?
    private let formats = UISegmentedControl(items: [L("方块文字", "Blocks"), L("普通文字", "Text"), L("图片", "Image")])
    private let message = UILabel()
    private let image = UIImageView()
    private let text = UITextView()
    private let primary = UIButton(type: .system)
    private let secondary = UIButton(type: .system)
    private let more = UIButton(type: .system)
    private let fullAccess: Bool
    private let blockText: String
    private let plainText: String
    private var saving = false
    private var isImage: Bool { formats.selectedSegmentIndex == Format.image.rawValue }
    private var selectedText: String { formats.selectedSegmentIndex == Format.blocks.rawValue ? blockText : plainText }

    init(png: Data, fullAccess: Bool, blockText: String, plainText: String, initialFormat: Format = .blocks) {
        self.fullAccess = fullAccess; self.blockText = blockText; self.plainText = plainText
        super.init(frame: .zero)
        accessibilityIdentifier = "keyboard.typingCard.preview"; accessibilityViewIsModal = true
        backgroundColor = .secondarySystemBackground
        let title = UILabel(); title.text = L("打字统计", "Typing stats"); title.font = .systemFont(ofSize: 16, weight: .semibold)
        let close = UIButton(type: .system); close.setImage(UIImage(systemName: "xmark.circle.fill"), for: .normal)
        close.accessibilityLabel = L("关闭统计预览", "Close stats preview"); close.accessibilityIdentifier = "keyboard.typingCard.close"
        close.addAction(UIAction { [weak self] _ in self?.onClose?() }, for: .touchUpInside)
        let header = UIStackView(arrangedSubviews: [title, close]); close.widthAnchor.constraint(equalToConstant: 44).isActive = true
        formats.selectedSegmentIndex = initialFormat.rawValue
        formats.accessibilityIdentifier = "keyboard.typingCard.formats"
        formats.addAction(UIAction { [weak self] _ in self?.refreshFormat() }, for: .valueChanged)
        image.image = UIImage(data: png); image.contentMode = .scaleAspectFit
        image.accessibilityIdentifier = "keyboard.typingCard.image"; image.isAccessibilityElement = true
        image.accessibilityLabel = L("带完整边框的统计图片", "Statistics image with a complete frame")
        text.isEditable = false; text.isSelectable = false; text.backgroundColor = .clear
        text.font = .systemFont(ofSize: 14); text.textColor = .label
        text.textContainerInset = .zero; text.textContainer.lineFragmentPadding = 0
        text.accessibilityIdentifier = "keyboard.typingCard.textPreview"
        for (button, id) in [(primary, "primary"), (secondary, "secondary"), (more, "more")] {
            button.accessibilityIdentifier = "keyboard.typingCard.\(id)"
            button.heightAnchor.constraint(greaterThanOrEqualToConstant: 40).isActive = true
        }
        primary.addAction(UIAction { [weak self] _ in
            guard let self else { return }
            if self.isImage { self.onPhotos?() } else { self.onText?(self.selectedText) }
        }, for: .touchUpInside)
        secondary.addAction(UIAction { [weak self] _ in
            guard let self else { return }
            if self.isImage { self.onSave?() } else { self.onCopyText?(self.selectedText) }
        }, for: .touchUpInside)
        more.showsMenuAsPrimaryAction = true
        more.menu = UIMenu(children: [UIAction(title: L("复制图片", "Copy image"), image: UIImage(systemName: "doc.on.doc"), attributes: fullAccess ? [] : [.disabled]) { [weak self] _ in self?.onCopy?() }])
        style(more, L("更多", "More"), icon: "ellipsis")
        let actions = UIStackView(arrangedSubviews: [primary, secondary, more]); actions.axis = .vertical; actions.spacing = 6
        actions.accessibilityIdentifier = "keyboard.typingCard.actions"
        let scroll = UIScrollView(); scroll.addSubview(actions)
        message.numberOfLines = 2; message.font = .systemFont(ofSize: 12); message.textColor = .secondaryLabel
        message.accessibilityIdentifier = "keyboard.typingCard.message"
        [header, formats, image, text, scroll, actions, message].forEach { $0.translatesAutoresizingMaskIntoConstraints = false }
        [header, formats, image, text, scroll, message].forEach(addSubview)
        NSLayoutConstraint.activate([
            header.topAnchor.constraint(equalTo: topAnchor, constant: 4), header.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 12),
            header.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -8), header.heightAnchor.constraint(equalToConstant: 36),
            formats.topAnchor.constraint(equalTo: header.bottomAnchor, constant: 2), formats.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 12),
            formats.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -12), formats.heightAnchor.constraint(equalToConstant: 30),
            message.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 12), message.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -12),
            message.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -8), message.heightAnchor.constraint(equalToConstant: 32),
            image.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 12), image.topAnchor.constraint(equalTo: formats.bottomAnchor, constant: 10),
            image.bottomAnchor.constraint(equalTo: message.topAnchor, constant: -8), image.widthAnchor.constraint(equalTo: widthAnchor, multiplier: 0.55),
            text.leadingAnchor.constraint(equalTo: image.leadingAnchor), text.trailingAnchor.constraint(equalTo: image.trailingAnchor),
            text.topAnchor.constraint(equalTo: image.topAnchor), text.bottomAnchor.constraint(equalTo: image.bottomAnchor),
            scroll.leadingAnchor.constraint(equalTo: image.trailingAnchor, constant: 12), scroll.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -12),
            scroll.topAnchor.constraint(equalTo: image.topAnchor), scroll.bottomAnchor.constraint(equalTo: image.bottomAnchor),
            actions.leadingAnchor.constraint(equalTo: scroll.contentLayoutGuide.leadingAnchor), actions.trailingAnchor.constraint(equalTo: scroll.contentLayoutGuide.trailingAnchor),
            actions.topAnchor.constraint(equalTo: scroll.contentLayoutGuide.topAnchor), actions.bottomAnchor.constraint(equalTo: scroll.contentLayoutGuide.bottomAnchor),
            actions.widthAnchor.constraint(equalTo: scroll.frameLayoutGuide.widthAnchor)
        ])
        refreshFormat()
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }
    func showMessage(_ value: String) { message.text = value; UIAccessibility.post(notification: .announcement, argument: value) }
    func setSaving(_ saving: Bool) { self.saving = saving; primary.isEnabled = !isImage || (fullAccess && !saving) }
    private func refreshFormat() {
        image.isHidden = !isImage; text.isHidden = isImage; more.isHidden = !isImage
        text.text = selectedText; text.setContentOffset(.zero, animated: false)
        style(primary, isImage ? L("保存到相册", "Save to Photos") : L("追加到输入框", "Insert text"), icon: isImage ? "photo.badge.arrow.down" : "text.append", prominent: true)
        style(secondary, isImage ? L("保存到 App", "Save to app") : L("复制文字", "Copy text"), icon: isImage ? "square.and.arrow.down" : "doc.on.doc")
        primary.isEnabled = !isImage || (fullAccess && !saving); secondary.isEnabled = fullAccess
        message.text = isImage
            ? (fullAccess ? L("保存到相册后，可在聊天中选择照片发送。", "Save to Photos, then choose the image in your chat.") : L("开启 \(ProductBrand.name) 完全访问后，可保存图片。", "Enable Full Access for \(ProductBrand.name) to save images."))
            : L("文字追加到输入框后，可编辑再发送。", "Insert into the field, then edit or send when ready.")
    }
    private func style(_ button: UIButton, _ title: String, icon: String, prominent: Bool = false) {
        var configuration = prominent ? UIButton.Configuration.filled() : .tinted()
        configuration.title = title; configuration.image = UIImage(systemName: icon); configuration.imagePadding = 5
        configuration.cornerStyle = .small
        configuration.titleTextAttributesTransformer = UIConfigurationTextAttributesTransformer { incoming in
            var result = incoming; result.font = .systemFont(ofSize: 14, weight: .medium); return result
        }
        button.configuration = configuration
    }
}
