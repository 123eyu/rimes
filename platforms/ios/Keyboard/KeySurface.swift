import UIKit
#if KEYBOARD_LAYOUT_TESTS
@testable import RIMES
#endif
import RimesCore

final class KeySurface: UIView {
    let feedback = KeyboardFeedback()
    private var reachable = ChordReachability(profile: .builtIn)
    var onTypingPress: (() -> Void)?
    var onKey: ((String) -> Void)?
    var onAlternate: ((String) -> Void)?
    var onChord: ((ChordResolution?) -> Void)?
    var onPreview: ((String) -> Void)?
    var onEmoji: ((String) -> Void)?
    var onLanguageToggle: (() -> Void)?
    var profile = ChordProfile.builtIn { didSet { if oldValue != profile { reachable = ChordReachability(profile: profile) }; retire(); setNeedsLayout() } }
    var chordMode = false { didSet { retire(); setNeedsLayout() } }
    var numeric = false { didSet { retire(); setNeedsLayout() } }
    var shifted = false { didSet { if oldValue != shifted { retire() }; setNeedsDisplay() } }
    var englishInput = false { didSet { if oldValue != englishInput { retire() }; updateLanguageButton(); setNeedsDisplay() } }
    var directSwipeSymbols = false { didSet { if oldValue != directSwipeSymbols { cancel() } } }
    var numericNineGrid = false { didSet { if oldValue != numericNineGrid { cancel(); setNeedsLayout() } } }
    var longPressSwipeSymbols = false { didSet { if oldValue != longPressSwipeSymbols { cancel(); setNeedsLayout() } } }
    var chordLayout: ChordLayout = .orthogonal { didSet { if oldValue != chordLayout { retire(); setNeedsLayout() } } }
    /// False draws the orthogonal grid but types each key by tap (schemes without chord tables).
    var chordResolves = true { didSet { if oldValue != chordResolves { retire(); setNeedsLayout() } } }
    var resolvesChords: Bool { chordMode && chordResolves && !numeric && !emojiMode && !shifted && !englishInput }
    var hasUtilityCells: Bool { chordMode && !numeric }
    var onModeChanged: (() -> Void)?
    /// Used only by ordinary input. Chord mode, including EN/Shift, keeps its original geometry.
    var customLayout: CustomKeyboardLayout? {
        didSet { if oldValue != customLayout { cancel(); touchIDs.removeAll(); setNeedsLayout() } }
    }
    var customFunctionViews: [CustomKeyAction: UIView] = [:]
    var usesCustomLayout: Bool { customLayout != nil && !chordMode && !numeric && !emojiMode }
    var standardMode: StandardKeyboardMode? { didSet { if oldValue != standardMode { cancel(); touchIDs.removeAll(); setNeedsLayout() } } }
    var standardFunctionViews: [StandardKeyControl: UIView] = [:]
    /// Chinese input shows Chinese marks on the standard number and symbol pages.
    var chineseNumberPages = false { didSet { if oldValue != chineseNumberPages { cancel(); touchIDs.removeAll(); setNeedsLayout() } } }
    var usesStandardLayout: Bool { standardMode != nil && !chordMode && !emojiMode }
    var usesManagedLayout: Bool { usesCustomLayout || usesStandardLayout }
    /// Legacy Apple look: a thin shadow under each key (see KeyboardKeyStyle).
    var legacyKeys = false { didSet { if oldValue != legacyKeys { for button in [emojiButton, languageButton, backButton] + emojiButtons { button.legacyKey = legacyKeys }; setNeedsDisplay() } } }
    var theme: StatusSkin = .rhino {
        didSet {
            guard oldValue != theme else { return }
            for button in [emojiButton, languageButton, backButton] + emojiButtons { button.theme = theme }
            setNeedsDisplay()
        }
    }
    var skin: KeyboardSkin { theme.keyboardStyle }
    private var labels: [String: String] = [:]
    /// When set, replaces the 中/EN utility cell in chord mode (the controller puts Delete here).
    var languageCellView: UIView? {
        didSet { oldValue?.removeFromSuperview(); if let languageCellView { addSubview(languageCellView); languageCellView.isHidden = true }; setNeedsLayout() }
    }
    private(set) var emojiMode = false { didSet { cancel(); setNeedsLayout(); if oldValue != emojiMode { onModeChanged?() } } }
    private let emojiButton = UtilityKeycapButton(), languageButton = UtilityKeycapButton(), backButton = UtilityKeycapButton()
    private var emojiButtons: [UtilityKeycapButton] = []
    private var utilityGeneration = UUID()
    private var boxes: [(String, CGRect)] = []
    private var gesture = ChordGesture()
    var isChordActive: Bool { resolvesChords && gesture.active }
    var handPreview: ChordHandPreview? { resolvesChords ? gesture.handPreview(in: profile) : nil }
    private var touchIDs: [ObjectIdentifier: Int] = [:]
    private var nextID = 0
    private var ordinary: [ObjectIdentifier: OrdinaryKeyGesture] = [:]
    private var holdTimers: [ObjectIdentifier: Timer] = [:]
    private let alternatePreview = UILabel()
    override init(frame: CGRect) {
        super.init(frame: frame); isMultipleTouchEnabled = true; backgroundColor = .clear
        alternatePreview.isHidden = true; alternatePreview.isUserInteractionEnabled = false
        alternatePreview.accessibilityElementsHidden = true
        alternatePreview.textAlignment = .center; alternatePreview.font = .systemFont(ofSize: 28)
        alternatePreview.layer.cornerRadius = 8; alternatePreview.clipsToBounds = true
        addSubview(alternatePreview)
        accessibilityLabel = L("字母键盘", "Letter keyboard")
        emojiButton.symbol("face.smiling", label: L("表情", "Emoji"))
        emojiButton.accessibilityIdentifier = "keyboard.emoji"
        languageButton.accessibilityIdentifier = "keyboard.mode"
        languageButton.titleLabel?.font = .systemFont(ofSize: 18, weight: .medium)
        installUtility(emojiButton) { [weak self] in self?.emojiMode = true }
        installUtility(languageButton) { [weak self] in self?.onLanguageToggle?() }
        backButton.symbol("arrow.uturn.backward", label: L("返回字母键盘", "Back to letters"))
        backButton.accessibilityIdentifier = "keyboard.emoji.back"
        let emojis = ["😀", "😄", "😂", "🥹", "😊", "😍", "😘", "😎", "🤔", "😅",
                      "😭", "🥲", "😮", "😴", "😡", "🥳", "👍", "👎", "👏", "🙏",
                      "👋", "👌", "💪", "❤️", "💔", "🔥", "🎉", "✅", "🌹"]
        for (index, emoji) in emojis.enumerated() {
            let button = UtilityKeycapButton(); button.setTitle(emoji, for: .normal)
            button.titleLabel?.font = .systemFont(ofSize: 21)
            button.accessibilityIdentifier = "keyboard.emoji.preset.\(index)"
            installUtility(button) { [weak self] in self?.onEmoji?(emoji) }
            button.titleHorizontalInset = 0.5
            emojiButtons.append(button)
        }
        installUtility(backButton) { [weak self] in self?.emojiMode = false }
        updateLanguageButton()
        registerForTraitChanges([UITraitUserInterfaceStyle.self]) { (surface: KeySurface, _: UITraitCollection) in surface.setNeedsDisplay() }
    }
    required init?(coder: NSCoder) { fatalError() }
    private func installUtility(_ button: UtilityKeycapButton, action: @escaping () -> Void) {
        addSubview(button); button.isHidden = true; button.isExclusiveTouch = true; button.titleHorizontalInset = 2
        // A utility tap cannot change mode in the middle of a two-thumb chord.
        // Sliding onto a utility preserves the last letter endpoint and never
        // activates that utility; only a fresh tap may invoke it.
        var beganIn: UUID?
        button.addAction(UIAction { [weak self] _ in
            guard let self else { return }
            beganIn = self.touchIDs.isEmpty && !self.gesture.active && self.ordinary.isEmpty ? self.utilityGeneration : nil
            if beganIn != nil { self.feedback.send(.press) }
        }, for: .touchDown)
        button.addAction(UIAction { [weak self] _ in
            defer { beganIn = nil }
            guard let self, beganIn == self.utilityGeneration, !self.gesture.active, self.touchIDs.isEmpty else { return }
            action()
        }, for: .touchUpInside)
        button.addAction(UIAction { _ in beganIn = nil }, for: [.touchCancel, .touchUpOutside])
        button.onAccessibilityActivate = { [weak self, weak button] in
            guard let self, button?.isHidden == false, !self.gesture.active, self.touchIDs.isEmpty else { return false }
            self.feedback.send(.press); action(); return true
        }
    }
    private func updateLanguageButton() {
        languageButton.setTitle(englishInput ? "EN" : "中", for: .normal)
        languageButton.isSelected = englishInput
        languageButton.accessibilityLabel = englishInput ? L("英文，切换中文", "English; switch to Chinese") : L("中文，切换英文", "Chinese; switch to English")
    }
    func showEmoji() { retire(); emojiMode = true }

    private func redraw() {
        let dimmed = resolvesChords && gesture.active
        [emojiButton, languageButton].forEach { $0.alpha = dimmed ? 0.3 : 1 }
        setNeedsDisplay()
    }
    private func clearOrdinaryTouches() {
        holdTimers.values.forEach { $0.invalidate() }; holdTimers.removeAll(); ordinary.removeAll()
        alternatePreview.isHidden = true
    }
    func cancel() { utilityGeneration = UUID(); feedback.reset(); gesture.cancel(); clearOrdinaryTouches(); onPreview?(""); redraw() }
    /// Context loss may never deliver matching touch-up events. Retire old IDs so
    /// late callbacks cannot commit and a fresh keyboard session is not stuck.
    func retire() { feedback.reset(); gesture.reset(); touchIDs.removeAll(); clearOrdinaryTouches(); emojiMode = false; onPreview?(""); redraw() }
    override func layoutSubviews() {
        super.layoutSubviews(); boxes.removeAll(); labels.removeAll()
        standardFunctionViews.values.forEach { $0.isHidden = true }
        let rowHeight = bounds.height / 3
        let utilitiesVisible = hasUtilityCells && !emojiMode
        emojiButton.isHidden = !utilitiesVisible; languageButton.isHidden = !utilitiesVisible || languageCellView != nil
        languageCellView?.isHidden = !utilitiesVisible
        (emojiButtons + [backButton]).forEach { $0.isHidden = !emojiMode }
        if emojiMode {
            let unit = bounds.width / 10
            for (index, button) in (emojiButtons + [backButton]).enumerated() {
                button.frame = CGRect(x: CGFloat(index % 10) * unit + 2, y: CGFloat(index / 10) * rowHeight + 3, width: unit - 4, height: rowHeight - 6)
            }
        } else if usesCustomLayout, let customLayout {
            let geometry = customLayout.geometry(width: Double(bounds.width), landscape: bounds.width > 590)
            customFunctionViews.values.forEach { $0.isHidden = true }
            for item in geometry.frames {
                let frame = CGRect(x: item.x, y: item.y, width: item.width, height: item.height)
                if item.key.action == .character { boxes.append((item.key.text, frame)) }
                else if let button = customFunctionViews[item.key.action] { button.frame = frame; button.isHidden = false }
            }
        } else if usesStandardLayout, let standardMode {
            let geometry = StandardKeyboardGeometry.make(width: bounds.width, mode: standardMode, landscape: bounds.width > 590, chinese: chineseNumberPages, numericNineGrid: numericNineGrid, availableHeight: bounds.height)
            boxes = geometry.keys.map { ($0.text, $0.frame) }
            labels = Dictionary(uniqueKeysWithValues: geometry.keys.map { ($0.text, $0.label) })
            standardFunctionViews.values.forEach { $0.isHidden = true }
            for (action, frame) in geometry.controls {
                standardFunctionViews[action]?.frame = frame; standardFunctionViews[action]?.isHidden = false
            }
        } else {
            let geometry = KeyboardGeometry.make(size: bounds.size, profile: profile, chord: chordMode, numeric: numeric, layout: chordLayout)
            boxes = geometry.keys
            emojiButton.isHidden = geometry.emoji == nil; languageButton.isHidden = geometry.language == nil || languageCellView != nil
            emojiButton.frame = geometry.emoji ?? .zero; languageButton.frame = geometry.language ?? .zero
            languageCellView?.isHidden = geometry.language == nil; languageCellView?.frame = geometry.language ?? .zero
            emojiButton.compactCap = chordMode && !numeric; languageButton.compactCap = chordMode && !numeric
        }
        let letters: [Any] = boxes.map { key, rect in
            let item = KeyAccessibility(accessibilityContainer: self); item.accessibilityLabel = labels[key] ?? key.uppercased(); item.accessibilityTraits = .keyboardKey; item.accessibilityFrameInContainerSpace = rect
            item.activate = { [weak self] in guard let self else { return }; self.feedback.send(.press); self.onTypingPress?(); self.onKey?(self.shifted && !self.numeric ? key.uppercased() : key) }
            if let alternate = alternate(for: key) {
                item.accessibilityCustomActions = [UIAccessibilityCustomAction(name: L("输入", "Insert") + " " + alternate) { [weak self] _ in
                    guard let self, self.alternate(for: key) == alternate else { return false }
                    self.feedback.send(.press); self.onTypingPress?(); self.onAlternate?(alternate); return true
                }]
            }
            return item
        }
        var utilityElements: [UIView] = [emojiButton, languageButton]
        if let languageCellView { utilityElements.append(languageCellView) }
        utilityElements.append(contentsOf: emojiButtons)
        utilityElements.append(backButton)
        var orderedElements: [Any] = letters
        orderedElements.append(contentsOf: utilityElements.filter { !$0.isHidden })
        func readingOrder(_ left: UIView, _ right: UIView) -> Bool {
            left.frame.minY == right.frame.minY
                ? left.frame.minX < right.frame.minX
                : left.frame.minY < right.frame.minY
        }
        if usesCustomLayout {
            let visible = customFunctionViews.values.filter { !$0.isHidden }
            orderedElements.append(contentsOf: visible.sorted(by: readingOrder))
        }
        if usesStandardLayout {
            let visible = standardFunctionViews.values.filter { !$0.isHidden }
            orderedElements.append(contentsOf: visible.sorted(by: readingOrder))
        }
        accessibilityElements = orderedElements
        redraw()
    }
    override func draw(_ rect: CGRect) {
        let selected = gesture.keys ?? []
        let eligible = gesture.availableKeys(in: reachable)
        for (key, box) in boxes {
            let active = (key.first.map(selected.contains) ?? false) || ordinary.values.contains { $0.key == key }
            let dimmed = resolvesChords && gesture.active && !active && !(key.first.map(eligible.contains) ?? false)
            let context = UIGraphicsGetCurrentContext()!
            context.saveGState(); context.setAlpha(dimmed ? 0.30 : 1)
            let cap = KeycapStyle.capRect(in: box, pressed: active, compact: chordMode && !numeric)
            KeycapStyle.draw(in: box, pressed: active, compact: chordMode && !numeric, theme: theme, legacy: legacyKeys)
            let base = labels[key] ?? key.uppercased()
            let value = shifted && !numeric && base.count == 1 ? base.uppercased() : base
            // Apple's font in every skin; chord layouts keep their compact monospaced caps.
            let nativeFont = !chordMode
            let size: CGFloat = standardMode == .nineKey ? 20 : nativeFont ? (numeric ? KeycapStyle.numberFontSize : KeycapStyle.letterFontSize) : 21
            let mark = chineseNumberPages && value.unicodeScalars.contains { $0.value > 0x7F }
            let font = mark ? UIFont.chineseMarks(ofSize: size, weight: nativeFont ? .regular : .medium)
                : nativeFont ? UIFont.systemFont(ofSize: size) : UIFont.monospacedSystemFont(ofSize: size, weight: .medium)
            let fittedFont = font.withSize(min(size, max(1, floor(size * cap.height / font.lineHeight)), max(1, floor(size * (cap.width - 2) / max(1, value.size(withAttributes: [.font: font]).width)))))
            let attr: [NSAttributedString.Key: Any] = [.font: fittedFont, .foregroundColor: active ? theme.palette.accentInk : theme.palette.ink]
            let textSize = value.size(withAttributes: attr)
            // Apple centres a lowercase letter's x-height, and a capital's or
            // digit's cap height, in the key, not the whole line box.
            // Marks share the digits' baseline, so `.` and `,` sit low and `?` and `!` centre on the
            // cap height, as on Apple's number pages.
            let glyphBox: CGFloat? = value.count != 1 || mark ? nil
                : value.first!.isLowercase ? fittedFont.xHeight
                : value.first!.isUppercase || value.first!.isNumber || (numeric && nativeFont && value.first!.isASCII) ? fittedFont.capHeight : nil
            var origin = CGPoint(x: cap.midX - textSize.width / 2,
                                 y: glyphBox.map { cap.midY + $0 / 2 - fittedFont.ascender } ?? cap.midY - textSize.height / 2)
            if numeric && nativeFont && value.count == 1 && value.utf16.count == 1 {
                // Apple centres the drawn mark itself on the key, wherever its glyph normally sits.
                var glyph = CGGlyph(), character = UniChar(value.utf16.first!), ink = CGRect.zero
                if CTFontGetGlyphsForCharacters(fittedFont, &character, &glyph, 1) {
                    CTFontGetBoundingRectsForGlyphs(fittedFont, .horizontal, &glyph, &ink, 1)
                }
                if !ink.isNull && !ink.isEmpty {
                    origin = CGPoint(x: cap.midX - ink.midX, y: cap.midY + ink.midY - fittedFont.ascender)
                }
            }
            value.draw(at: origin, withAttributes: attr)
            if let alternate = alternate(for: key) {
                let hint: [NSAttributedString.Key: Any] = [.font: UIFont.systemFont(ofSize: min(11, cap.height * 0.24)), .foregroundColor: (active ? theme.palette.accentInk : theme.palette.ink).withAlphaComponent(0.55)]
                let hintSize = alternate.size(withAttributes: hint)
                alternate.draw(at: CGPoint(x: cap.maxX - hintSize.width - 4, y: cap.minY + 2), withAttributes: hint)
            }
            context.restoreGState()
        }
    }
    private func key(at point: CGPoint) -> String? { boxes.first { $0.1.contains(point) }?.0 }
    /// Snaps only across the narrow inter-cap gutters, never across blank row ends.
    static let gapTolerance: CGFloat = 6
    private func nearestKey(to point: CGPoint) -> String? {
        guard bounds.contains(point) else { return nil }
        func distance(_ rect: CGRect) -> CGFloat { hypot(max(rect.minX - point.x, 0, point.x - rect.maxX), max(rect.minY - point.y, 0, point.y - rect.maxY)) }
        guard let best = boxes.min(by: { distance($0.1) < distance($1.1) }), distance(best.1) <= Self.gapTolerance else { return nil }
        return best.0
    }
    private func preview() {
        onPreview?(gesture.resolution(in: profile)?.preview ?? "")
        feedback.send(.selection, combination: gesture.resolution(in: profile)?.keys)
        redraw()
    }
    private func alternate(for key: String) -> String? {
        guard longPressSwipeSymbols, !chordMode, !numeric, !emojiMode,
              standardMode != .numeric, standardMode != .symbols else { return nil }
        return OrdinaryKeyGesture.alternate(for: key, nineKey: !usesCustomLayout && standardMode == .nineKey, english: englishInput)
    }
    private func showAlternatePreview() {
        guard let touch = ordinary.values.first(where: { $0.armed }), let value = touch.alternate,
              let frame = boxes.first(where: { $0.0 == touch.initialKey })?.1 else {
            if !alternatePreview.isHidden { alternatePreview.isHidden = true }; return
        }
        alternatePreview.text = value
        alternatePreview.backgroundColor = touch.selected ? theme.palette.accent : theme.palette.key
        alternatePreview.textColor = touch.selected ? theme.palette.accentInk : theme.palette.ink
        let width = max(44, frame.width)
        alternatePreview.frame = CGRect(x: min(max(0, frame.midX - width / 2), bounds.width - width), y: frame.minY - 42, width: width, height: 40)
        alternatePreview.isHidden = false; bringSubviewToFront(alternatePreview)
    }
    private func beginOrdinary(_ id: ObjectIdentifier, key: String, point: CGPoint, at time: TimeInterval, scheduleHold: Bool = true) {
        let alternate = alternate(for: key)
        ordinary[id] = .init(key: key, origin: point, at: time, alternate: alternate, keyWidth: boxes.first { $0.0 == key }?.1.width ?? 32, direct: directSwipeSymbols)
        guard alternate != nil, scheduleHold, !directSwipeSymbols else { return }
        let timer = Timer(timeInterval: OrdinaryKeyGesture.holdDuration, repeats: false) { [weak self] _ in
            guard let self, self.ordinary[id]?.arm(at: ProcessInfo.processInfo.systemUptime) == true else { return }
            self.holdTimers.removeValue(forKey: id)
            self.showAlternatePreview(); self.redraw()
        }
        holdTimers[id] = timer; RunLoop.main.add(timer, forMode: .common)
    }
    private func moveOrdinary(_ id: ObjectIdentifier, point: CGPoint, at time: TimeInterval) {
        guard let previous = ordinary[id] else { return }
        ordinary[id]?.move(to: point, key: key(at: point), at: time)
        guard let current = ordinary[id], previous.key != current.key || previous.armed != current.armed || previous.selected != current.selected else { return }
        showAlternatePreview(); redraw()
    }
    private func endOrdinary(_ id: ObjectIdentifier, point: CGPoint, at time: TimeInterval) {
        holdTimers.removeValue(forKey: id)?.invalidate()
        guard var tracked = ordinary.removeValue(forKey: id) else { return }
        tracked.move(to: point, key: key(at: point), at: time)
        showAlternatePreview()
        if let value = tracked.selectedAlternate { onAlternate?(value) }
        else { onKey?(shifted && !numeric ? tracked.key.uppercased() : tracked.key) }
    }
    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard !emojiMode else { return }
        utilityGeneration = UUID()
        for t in touches {
            // Mapping captions and blank areas never start or poison a chord.
            // Ordinary typing forgives a tap that lands in the gap between caps.
            let point = t.location(in: self)
            guard let key = key(at: point) ?? (resolvesChords ? nil : nearestKey(to: point)) else { continue }
            let oid = ObjectIdentifier(t)
            feedback.send(.press); onTypingPress?()
            if resolvesChords {
                nextID += 1; touchIDs[oid] = nextID; gesture.begin(id: nextID, key: key.first, profile: profile)
            }
            else { beginOrdinary(oid, key: key, point: point, at: t.timestamp) }
        }
        if resolvesChords { preview() }; redraw()
    }
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard resolvesChords else {
            for t in touches {
                moveOrdinary(ObjectIdentifier(t), point: t.location(in: self), at: t.timestamp)
            }
            return
        }
        for t in touches { if let id = touchIDs[ObjectIdentifier(t)] { gesture.move(id: id, key: key(at: t.location(in: self))?.first, profile: profile) } }
        preview()
    }
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        for t in touches {
            let oid = ObjectIdentifier(t), key = key(at: t.location(in: self))
            if let id = touchIDs.removeValue(forKey: oid) {
                let result = gesture.end(id: id, key: key?.first, profile: profile)
                if !gesture.active { if result != nil { feedback.send(.commit) }; feedback.reset(); onChord?(result); onPreview?("") }
            } else { endOrdinary(oid, point: t.location(in: self), at: t.timestamp) }
        }
        redraw()
    }
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        gesture.cancel()
        for t in touches {
            let oid = ObjectIdentifier(t)
            if let id = touchIDs.removeValue(forKey: oid) { _ = gesture.end(id: id, key: nil, profile: profile) }
            ordinary.removeValue(forKey: oid); holdTimers.removeValue(forKey: oid)?.invalidate()
        }
        showAlternatePreview(); onPreview?(""); redraw()
    }
    #if DEBUG
    func developmentOrdinaryGesture(key: String, duration: TimeInterval, dx: CGFloat = 0, dy: CGFloat = -24, beforeRelease: (() -> Void)? = nil) {
        guard let frame = boxes.first(where: { $0.0 == key })?.1 else { return }
        let token = NSObject(), start = CGPoint(x: frame.midX, y: frame.midY)
        let id = ObjectIdentifier(token), end = CGPoint(x: start.x + dx, y: start.y + dy)
        beginOrdinary(id, key: key, point: start, at: 0, scheduleHold: false)
        moveOrdinary(id, point: end, at: duration)
        beforeRelease?()
        endOrdinary(id, point: end, at: duration)
        redraw()
    }
    func developmentAlternate(for key: String) -> String? { alternate(for: key) }
    var developmentKeyFrames: [String: CGRect] {
        var frames = Dictionary(uniqueKeysWithValues: boxes)
        if !emojiButton.isHidden { frames["emoji"] = emojiButton.frame; frames["mode"] = languageButton.frame }
        return frames
    }
    func developmentKey(at point: CGPoint) -> String? { key(at: point) }
    func developmentOrdinaryKey(at point: CGPoint) -> String? { key(at: point) ?? (resolvesChords ? nil : nearestKey(to: point)) }
    func developmentPress(_ start: Character, end: Character, id: Int = 900) {
        gesture.begin(id: id, key: start, profile: profile)
        gesture.move(id: id, key: end, profile: profile); preview()
    }
    func developmentRelease(_ key: Character, id: Int = 900) {
        let result = gesture.end(id: id, key: key, profile: profile)
        if !gesture.active { onChord?(result); onPreview?("") }
    }
    func developmentMove(_ key: Character?, id: Int = 900) {
        gesture.move(id: id, key: key, profile: profile); preview()
    }
    #endif

}
extension UIFont {
    /// The system font draws curly quotes, dashes and ellipses in their Latin shapes, where an
    /// opening quote is hard to tell from a closing one. Chinese marks use the full-width forms.
    static func chineseMarks(ofSize size: CGFloat, weight: UIFont.Weight = .regular) -> UIFont {
        UIFont(name: weight == .medium ? "PingFangSC-Medium" : "PingFangSC-Regular", size: size) ?? .systemFont(ofSize: size, weight: weight)
    }
}
private final class UtilityKeycapButton: KeycapButton {
    var onAccessibilityActivate: (() -> Bool)?
    override func accessibilityActivate() -> Bool { onAccessibilityActivate?() ?? false }
}
private final class KeyAccessibility: UIAccessibilityElement {
    var activate: (() -> Void)?
    override func accessibilityActivate() -> Bool { activate?(); return true }
}
