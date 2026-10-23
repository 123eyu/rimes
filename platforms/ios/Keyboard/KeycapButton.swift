import UIKit
#if KEYBOARD_LAYOUT_TESTS
@testable import RIMES
#endif
import ImageIO
import RimesCore

/// One cap geometry/palette for drawn letters and UIKit-backed controls.
enum KeycapStyle {
    /// Measured against Apple's keyboard (iOS 26.5 simulator, 3x screenshots).
    /// Fitted from the top-edge corner profile: Apple 5.34 pt; ours 3.34 pt at
    /// r = 5 and 4.97 pt at r = 6.75, which puts Apple's continuous radius at ≈ 7.15 pt.
    static let cornerRadius: CGFloat = 7.15
    /// Apple's caps sit on whole device pixels (adjacent keys alternate 100 and
    /// 101 px at 3x); fractional edges smear by a pixel and read as wider keys.
    static func pixelSnapped(_ rect: CGRect) -> CGRect {
        let scale = max(1, UITraitCollection.current.displayScale)
        func snap(_ value: CGFloat) -> CGFloat { (value * scale).rounded() / scale }
        // Horizontal edges snap independently (Apple's 100/101 px alternation);
        // the height snaps as a length so a half-point inset keeps Apple's 43 pt cap.
        let x = snap(rect.minX), y = snap(rect.minY)
        return CGRect(x: x, y: y, width: max(0, snap(rect.maxX) - x), height: max(0, snap(rect.height)))
    }
    /// Letter keys use Apple's size and the system font in every skin.
    static let letterFontSize: CGFloat = 25
    /// Keys are flat: no drop shadow, no raised base plate, and a press shows
    /// by colour only, so the cap never moves.
    static func capRect(in rect: CGRect, pressed: Bool, compact: Bool = false) -> CGRect {
        rect.insetBy(dx: compact ? 0 : 0.5, dy: compact ? 0.75 : 1.5)
    }
    static func draw(in rect: CGRect, pressed: Bool, selected: Bool = false, enabled: Bool = true, compact: Bool = false,
                     theme: StatusSkin = .rhino, functional: Bool = false, accent: Bool = false, legacy: Bool = false) {
        guard let context = UIGraphicsGetCurrentContext() else { return }
        let rect = pixelSnapped(rect)
        let palette = theme.palette
        if theme.keyboardStyle == .system {
            let cap = UIBezierPath(roundedRect: rect.insetBy(dx: 0, dy: 0.5), cornerRadius: cornerRadius)
            context.saveGState()
            if !enabled { context.setAlpha(0.4) }
            if legacy {
                // Apple's legacy keyboard: grey function keys, and the cap lifted 1 pt off a dark base.
                let base = UIBezierPath(roundedRect: rect.insetBy(dx: 0, dy: 0.5).offsetBy(dx: 0, dy: 1), cornerRadius: cornerRadius)
                UIColor { $0.userInterfaceStyle == .dark ? UIColor(white: 0, alpha: 0.6) : UIColor(red: 0.54, green: 0.54, blue: 0.56, alpha: 1) }.setFill(); base.fill()
                let legacyCap = UIBezierPath(roundedRect: rect.insetBy(dx: 0, dy: 0.5).offsetBy(dx: 0, dy: -0.5), cornerRadius: cornerRadius)
                // Only an action Return (selected accent) is tinted; other function keys are grey.
                let tone: UIColor = accent && (selected || pressed) ? palette.accent : pressed ? (functional ? palette.key : palette.functional)
                    : functional ? palette.functional : palette.key
                tone.setFill(); legacyCap.fill(); context.restoreGState(); return
            }
            // Apple's keyboard draws function keys in the same tone as letters;
            // only an action Return (Send, Search, Go…) is tinted.
            let fill: UIColor = pressed ? (accent && selected ? palette.pressedSelected : palette.accent)
                : accent && selected ? palette.accent : palette.key
            fill.setFill(); cap.fill(); context.restoreGState(); return
        }
        context.saveGState()
        if !enabled { context.setAlpha(0.4) }
        let cap = UIBezierPath(roundedRect: capRect(in: rect, pressed: pressed, compact: compact), cornerRadius: compact ? 3 : cornerRadius)
        (pressed && selected ? palette.pressedSelected : pressed || selected ? palette.accent : functional ? palette.functional : palette.key).setFill(); cap.fill()
        UIColor.label.withAlphaComponent(pressed || selected ? 0.15 : 0.10).setStroke()
        cap.lineWidth = 0.7; cap.stroke()
        context.restoreGState()
    }
}

class KeycapButton: UIButton {
    var theme: StatusSkin = .rhino { didSet { if oldValue != theme { updateAppearance() } } }
    var skin: KeyboardSkin { theme.keyboardStyle }
    var legacyKey = false { didSet { if oldValue != legacyKey { setNeedsDisplay() } } }
    var functionalCap = false { didSet { if oldValue != functionalCap { setNeedsDisplay() } } }
    var accentCap = false { didSet { if oldValue != accentCap { updateAppearance() } } }
    var compactCap = false { didSet { if oldValue != compactCap { setNeedsDisplay(); setNeedsLayout() } } }
    var titleHorizontalInset: CGFloat = 6 { didSet { if oldValue != titleHorizontalInset { setNeedsLayout() } } }
    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .clear; isOpaque = false; contentMode = .redraw
        titleLabel?.font = .systemFont(ofSize: 14, weight: .medium)
        titleLabel?.adjustsFontSizeToFitWidth = false
        titleLabel?.lineBreakMode = .byTruncatingTail
        titleLabel?.textAlignment = .center
        setTitleColor(.label, for: .normal)
        setTitleColor(.white, for: .highlighted)
        setTitleColor(.white, for: .selected)
        setTitleColor(.label.withAlphaComponent(0.4), for: .disabled)
        setPreferredSymbolConfiguration(.init(pointSize: 17, weight: .medium), forImageIn: .normal)
        registerForTraitChanges([UITraitUserInterfaceStyle.self]) { (button: KeycapButton, _: UITraitCollection) in button.updateAppearance() }
        updateAppearance()
    }
    required init?(coder: NSCoder) { fatalError() }
    override var isHighlighted: Bool { didSet { if oldValue != isHighlighted { updateAppearance() } } }
    override var isSelected: Bool { didSet { if oldValue != isSelected { updateAppearance() } } }
    override var isEnabled: Bool { didSet { if oldValue != isEnabled { updateAppearance() } } }
    private func updateAppearance() {
        let palette = theme.palette
        let selectedInk: UIColor = skin == .system && !accentCap ? palette.ink : palette.accentInk
        let pressedInk = isSelected && (skin != .system || accentCap) ? palette.pressedSelectedInk : palette.accentInk
        adjustsImageWhenHighlighted = false
        setTitleColor(palette.ink, for: .normal)
        setTitleColor(palette.ink.withAlphaComponent(0.4), for: .disabled)
        setTitleColor(palette.accentInk, for: .highlighted)
        setTitleColor(pressedInk, for: [.selected, .highlighted])
        setTitleColor(selectedInk, for: .selected)
        tintColor = !isEnabled ? palette.ink.withAlphaComponent(0.4) : isHighlighted ? pressedInk : isSelected ? selectedInk : palette.ink
        setNeedsDisplay(); setNeedsLayout()
    }
    override func draw(_ rect: CGRect) {
        KeycapStyle.draw(in: bounds, pressed: isHighlighted, selected: isSelected, enabled: isEnabled, compact: compactCap,
                         theme: theme, functional: functionalCap, accent: accentCap, legacy: legacyKey)
    }
    override func layoutSubviews() {
        super.layoutSubviews()
        let cap = KeycapStyle.capRect(in: bounds, pressed: isHighlighted, compact: compactCap)
        let textHeight = min(cap.height, ceil(titleLabel?.font.lineHeight ?? 0))
        titleLabel?.frame = CGRect(x: titleHorizontalInset, y: cap.midY - textHeight / 2, width: max(0, bounds.width - 2 * titleHorizontalInset), height: textHeight)
        if let imageView, let image = imageView.image {
            let size = CGSize(width: max(0, min(image.size.width, cap.width - 8)), height: max(0, min(image.size.height, cap.height - 6)))
            imageView.frame = CGRect(x: cap.midX - size.width / 2, y: cap.midY - size.height / 2, width: size.width, height: size.height)
        }
    }
    func symbol(_ name: String, label: String) {
        setTitle(nil, for: .normal); setImage(UIImage(systemName: name), for: .normal)
        accessibilityLabel = label
    }
}

/// Time-based state is shared by real touch tracking and deterministic tests.
struct InsertionPress {
    enum Action: Equatable { case next, all }
    static let holdDuration: TimeInterval = 1
    private var beganAt: TimeInterval?
    private var fired = false
    mutating func begin(at time: TimeInterval) { beganAt = time; fired = false }
    mutating func cancel() { beganAt = nil; fired = false }
    mutating func advance(to time: TimeInterval) -> Action? {
        guard let beganAt, !fired, time - beganAt >= Self.holdDuration else { return nil }
        fired = true; return .all
    }
    mutating func end(at time: TimeInterval, inside: Bool) -> Action? {
        defer { cancel() }
        guard let beganAt, !fired, inside else { return nil }
        return time - beganAt >= Self.holdDuration ? .all : .next
    }
}

final class InsertKeycapButton: KeycapButton {
    var onPressBegan: (() -> Void)?
    var onInsert: ((InsertionPress.Action) -> Void)?
    private var press = InsertionPress()
    private var holdTimer: Timer?
    private var deadline: TimeInterval = 0
    override init(frame: CGRect) {
        super.init(frame: frame)
        accessibilityCustomActions = [UIAccessibilityCustomAction(name: L("插入全部", "Insert all"), target: self, selector: #selector(accessibilityInsertAll))]
    }
    required init?(coder: NSCoder) { fatalError() }
    override var isEnabled: Bool { didSet { if !isEnabled { cancelPress() } } }
    func cancelPress() { holdTimer?.invalidate(); holdTimer = nil; press.cancel(); isHighlighted = false }
    override func beginTracking(_ touch: UITouch, with event: UIEvent?) -> Bool {
        guard super.beginTracking(touch, with: event) else { return false }
        onPressBegan?()
        let now = ProcessInfo.processInfo.systemUptime
        press.begin(at: now); deadline = now + InsertionPress.holdDuration
        scheduleHold(); return true
    }
    private func scheduleHold() {
        holdTimer?.invalidate()
        let timer = Timer(timeInterval: max(0.001, deadline - ProcessInfo.processInfo.systemUptime), repeats: false) { [weak self] _ in
            guard let self else { return }
            if let action = self.press.advance(to: ProcessInfo.processInfo.systemUptime) { self.onInsert?(action) }
            else if self.isTracking && self.isEnabled { self.scheduleHold() }
        }
        holdTimer = timer; RunLoop.main.add(timer, forMode: .common)
    }
    override func continueTracking(_ touch: UITouch, with event: UIEvent?) -> Bool {
        guard bounds.contains(touch.location(in: self)) else { cancelPress(); return false }
        return super.continueTracking(touch, with: event)
    }
    override func endTracking(_ touch: UITouch?, with event: UIEvent?) {
        holdTimer?.invalidate(); holdTimer = nil
        let action = press.end(at: ProcessInfo.processInfo.systemUptime, inside: touch.map { bounds.contains($0.location(in: self)) } ?? false)
        super.endTracking(touch, with: event)
        if let action { onInsert?(action) }
    }
    override func cancelTracking(with event: UIEvent?) { cancelPress(); super.cancelTracking(with: event) }
    override func didMoveToWindow() { super.didMoveToWindow(); if window == nil { cancelPress() } }
    override func accessibilityActivate() -> Bool {
        guard isEnabled else { return false }; onPressBegan?(); onInsert?(.next); return true
    }
    @objc private func accessibilityInsertAll() -> Bool {
        guard isEnabled else { return false }; onPressBegan?(); onInsert?(.all); return true
    }
}

/// Horizontal caret stepping shared by the space key and its tests.
struct CursorDrag {
    static let holdDuration: TimeInterval = 0.35
    static let step: CGFloat = 10
    private var anchor: CGFloat = 0
    private(set) var active = false
    mutating func activate(at x: CGFloat) { active = true; anchor = x }
    mutating func cancel() { active = false }
    /// Whole steps since the last call; the remainder carries over.
    mutating func steps(to x: CGFloat) -> Int {
        guard active else { return 0 }
        let count = Int(((x - anchor) / Self.step).rounded(.towardZero))
        anchor += CGFloat(count) * Self.step; return count
    }
}

/// Space: a tap types; holding it and dragging moves by one character per step,
/// and releasing then types nothing. The controller assigns selection or caret
/// movement to each independent Space key.
final class SpaceCursorButton: KeycapButton {
    var onCursorBegan: (() -> Bool)?
    var onCursorMove: ((Int) -> Void)?
    private var drag = CursorDrag()
    private var holdTimer: Timer?
    private var lastX: CGFloat = 0
    private var suppressTap = false
    var isMovingCursor: Bool { drag.active }
    /// False once a hold has turned this press into caret movement.
    func consumeTap() -> Bool { defer { suppressTap = false }; return !suppressTap }
    override func beginTracking(_ touch: UITouch, with event: UIEvent?) -> Bool {
        finish(); suppressTap = false; lastX = touch.location(in: self).x
        let timer = Timer(timeInterval: CursorDrag.holdDuration, repeats: false) { [weak self] _ in self?.beginCursor(requireTracking: true) }
        holdTimer = timer; RunLoop.main.add(timer, forMode: .common)
        return super.beginTracking(touch, with: event)
    }
    func beginCursor(requireTracking: Bool) {
        holdTimer?.invalidate(); holdTimer = nil
        guard isTracking || !requireTracking, !drag.active, onCursorBegan?() == true else { return }
        drag.activate(at: lastX); suppressTap = true; isSelected = true
    }
    func moveCursor(to x: CGFloat) {
        lastX = x
        let count = drag.steps(to: x)
        if count != 0 { onCursorMove?(count) }
    }
    override func continueTracking(_ touch: UITouch, with event: UIEvent?) -> Bool {
        if drag.active { moveCursor(to: touch.location(in: self).x); return true }
        lastX = touch.location(in: self).x
        return super.continueTracking(touch, with: event)
    }
    override func endTracking(_ touch: UITouch?, with event: UIEvent?) { finish(); super.endTracking(touch, with: event) }
    override func cancelTracking(with event: UIEvent?) { finish(); super.cancelTracking(with: event) }
    override func didMoveToWindow() { super.didMoveToWindow(); if window == nil { finish(); suppressTap = false } }
    func finish() {
        holdTimer?.invalidate(); holdTimer = nil
        if drag.active { drag.cancel(); isSelected = false }
    }
}

/// 1U cell left of the output line showing what the output is doing: a breathing
/// coloured light, or a hermit crab whose shell takes the colour and who acts out the
/// state. It draws the same cap as the keys so the column lines up. Hold to change skin.
final class StatusLight: UIView {
    enum State: Equatable {
        case idle, waiting, streaming, ready, failed
        var color: UIColor {
            switch self {
            case .idle: return .systemGray
            case .waiting: return .systemOrange
            case .streaming: return .systemTeal
            case .ready: return .systemGreen
            case .failed: return .systemRed
            }
        }
        /// Seconds per breath; nil is steady.
        var period: CFTimeInterval? {
            switch self { case .idle, .ready: return nil; case .waiting: return 1.4; case .streaming: return 0.7; case .failed: return 0.45 }
        }
        var label: String {
            switch self {
            case .idle: return L("等待输入", "Waiting for input")
            case .waiting: return L("已发送，等待回应", "Sent; waiting for a reply")
            case .streaming: return L("正在输出", "Output streaming")
            case .ready: return L("输出完成，可以发送", "Output ready to send")
            case .failed: return L("输出出错", "Output failed")
            }
        }
    }
    typealias Skin = StatusSkin
    private(set) var state = State.idle
    var skin = Skin.apple { didSet { if skin != oldValue { setNeedsDisplay(); setNeedsLayout(); apply(animated: false) } } }
    /// A tap asks for the next skin in the chosen rotation.
    var onCycleSkin: (() -> Void)?
    private let dot = CALayer()
    private let crab = HermitCrabLayer()
    private let pets: [PetLayer.Species: PetLayer] = [.kitten: PetLayer(.kitten), .puppy: PetLayer(.puppy), .piglet: PetLayer(.piglet)]
    /// Animated Noto pet: a halo in the state colour behind the looping emoji.
    private let notoHalo = CAShapeLayer(), notoView = UIImageView()
    private var notoLoaded: Skin?
    private var notoAnimated: UIImage?
    /// The rhino mascot plays a playlist of his actions for each state.
    private let mascotView = UIImageView()
    private var mascotClips: [String: UIImage] = [:]
    private var mascotWork: DispatchWorkItem?
    private var mascotToken = UUID()
    private enum MascotStep { case play(String, times: Int, reversed: Bool), hold(String, frame: Int, seconds: Double) }
    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .clear; isOpaque = false; contentMode = .redraw
        dot.name = "status.dot"
        dot.shadowOffset = .zero; dot.shadowRadius = 5; dot.shadowOpacity = 0.9
        layer.addSublayer(dot); layer.addSublayer(crab)
        for pet in pets.values { layer.addSublayer(pet) }
        notoHalo.shadowOffset = .zero; notoHalo.shadowRadius = 3; notoHalo.shadowOpacity = 0.8
        layer.addSublayer(notoHalo)
        notoView.contentMode = .scaleAspectFit; notoView.isUserInteractionEnabled = false
        addSubview(notoView)
        mascotView.contentMode = .scaleAspectFit; mascotView.isUserInteractionEnabled = false; mascotView.isHidden = true
        addSubview(mascotView)
        addGestureRecognizer(UITapGestureRecognizer(target: self, action: #selector(tapped)))
        isAccessibilityElement = true; accessibilityTraits = [.staticText, .updatesFrequently]
        accessibilityHint = L("轻点切换宠物与键盘配色", "Tap to change pet and keyboard colors")
        registerForTraitChanges([UITraitUserInterfaceStyle.self]) { (light: StatusLight, _: UITraitCollection) in light.apply(animated: false) }
        apply(animated: false)
    }
    required init?(coder: NSCoder) { fatalError() }
    @objc private func tapped() { onCycleSkin?() }
    override func draw(_ rect: CGRect) { KeycapStyle.draw(in: bounds, pressed: false, theme: skin) }
    override func layoutSubviews() {
        super.layoutSubviews()
        let cap = KeycapStyle.capRect(in: bounds, pressed: false), size: CGFloat = 10
        CATransaction.begin(); CATransaction.setDisableActions(true)
        dot.frame = CGRect(x: cap.midX - size / 2, y: cap.midY - size / 2, width: size, height: size); dot.cornerRadius = size / 2
        crab.place(in: cap.insetBy(dx: 2, dy: 2), scale: traitCollection.displayScale)
        for pet in pets.values { pet.place(in: cap.insetBy(dx: 1.5, dy: 1.5), scale: traitCollection.displayScale) }
        let side = min(cap.width, cap.height) - 3
        let square = CGRect(x: cap.midX - side / 2, y: cap.midY - side / 2, width: side, height: side)
        notoHalo.frame = square; notoHalo.path = UIBezierPath(ovalIn: CGRect(origin: .zero, size: square.size)).cgPath
        notoView.frame = square.insetBy(dx: side * 0.1, dy: side * 0.1)
        mascotView.frame = square
        CATransaction.commit()
    }
    override var isHidden: Bool { didSet { if isHidden != oldValue { apply(animated: false) } } }
    override func didMoveToWindow() { super.didMoveToWindow(); apply(animated: false) }
    func set(_ next: State) { guard next != state else { return }; state = next; apply(animated: true) }
    private func suspendAnimations() {
        mascotWork?.cancel(); mascotWork = nil; mascotToken = UUID()
        dot.removeAllAnimations(); notoHalo.removeAllAnimations(); notoView.layer.removeAllAnimations()
        crab.show(nil, color: .clear, motion: false)
        for pet in pets.values { pet.show(nil, color: .clear, motion: false) }
        notoView.stopAnimating(); notoView.image = nil; notoAnimated = nil; notoLoaded = nil
        mascotView.stopAnimating(); mascotView.animationImages = nil; mascotView.image = nil; mascotClips = [:]
    }
    private func apply(animated: Bool) {
        // UIKit may retain a hidden keyboard. Its status light must not keep
        // scheduling mascot clips or hold decoded frames until process exit.
        guard window != nil, !isHidden else { suspendAnimations(); return }
        let color = state.color.resolvedColor(with: traitCollection)
        let motion = !UIAccessibility.isReduceMotionEnabled
        accessibilityLabel = skin.title + " · " + state.label
        dot.isHidden = !skin.usesDot; crab.isHidden = skin != .crab
        applyNoto(color: color, motion: motion)
        applyMascot(color: color, motion: motion)
        for (species, pet) in pets { pet.isHidden = skin.petSpecies != species; pet.show(skin.petSpecies == species ? state : nil, color: color, motion: motion) }
        dot.removeAnimation(forKey: "breath")
        crab.show(skin == .crab ? state : nil, color: color, motion: motion)
        guard skin.usesDot else { return }
        dot.backgroundColor = color.cgColor; dot.shadowColor = color.cgColor
        dot.opacity = state == .idle ? 0.45 : 1
        guard let period = state.period, motion else { return }
        dot.add(Self.breath(period: period, from: 1.1, to: 0.8), forKey: "breath")
    }
    private func applyNoto(color: UIColor, motion: Bool) {
        notoHalo.removeAllAnimations(); notoView.layer.removeAnimation(forKey: "shake")
        guard skin.isNoto else {
            // Free the frames when another skin is chosen.
            notoHalo.isHidden = true; notoView.isHidden = true; notoView.stopAnimating(); notoView.image = nil
            notoAnimated = nil; notoLoaded = nil; return
        }
        if notoLoaded != skin { notoAnimated = NotoPet.image(skin.rawValue); notoLoaded = skin }
        notoHalo.isHidden = false; notoView.isHidden = false
        notoHalo.fillColor = color.withAlphaComponent(0.22).cgColor; notoHalo.shadowColor = color.cgColor
        notoHalo.opacity = state == .idle ? 0.5 : 1
        // Resting when idle, slow while waiting, quick while streaming, stopped on error.
        let speed: Float = switch state { case .idle, .failed: 0; case .waiting: 0.55; case .streaming: 1.5; case .ready: 1 }
        notoView.alpha = state == .idle ? 0.7 : 1
        if motion && speed > 0 {
            notoView.image = notoAnimated; notoView.layer.speed = speed; notoView.startAnimating()
        } else {
            // The first frame is the emoji's rest pose.
            notoView.stopAnimating(); notoView.layer.speed = 1; notoView.image = notoAnimated?.images?.first ?? notoAnimated
        }
        guard motion else { return }
        if let period = state.period {
            let glow = CABasicAnimation(keyPath: "shadowRadius"); glow.fromValue = 5; glow.toValue = 0.5
            glow.duration = period / 2; glow.autoreverses = true; glow.repeatCount = .infinity
            notoHalo.add(glow, forKey: "glow")
        }
        if state == .failed {
            let shake = CABasicAnimation(keyPath: "transform.rotation.z"); shake.fromValue = -0.14; shake.toValue = 0.14
            shake.duration = 0.1; shake.autoreverses = true; shake.repeatCount = .infinity
            notoView.layer.add(shake, forKey: "shake")
        }
    }
    private func applyMascot(color: UIColor, motion: Bool) {
        mascotWork?.cancel(); mascotWork = nil; mascotToken = UUID()
        guard skin == .rhino else {
            mascotView.isHidden = true; mascotView.stopAnimating(); mascotView.animationImages = nil; mascotView.image = nil
            mascotClips = [:]; return
        }
        mascotView.isHidden = false
        // He shares the Noto pets' halo in the state colour.
        notoHalo.isHidden = false
        notoHalo.fillColor = color.withAlphaComponent(0.22).cgColor; notoHalo.shadowColor = color.cgColor
        notoHalo.opacity = state == .idle ? 0.5 : 1
        if motion, let period = state.period {
            let glow = CABasicAnimation(keyPath: "shadowRadius"); glow.fromValue = 5; glow.toValue = 0.5
            glow.duration = period / 2; glow.autoreverses = true; glow.repeatCount = .infinity
            notoHalo.add(glow, forKey: "glow")
        }
        guard motion else { mascotView.stopAnimating(); mascotView.image = mascotClip("stand")?.images?.first; return }
        let steps: [MascotStep]
        switch state {
        case .idle:      // stand, stroll, sit down for a rest, get up again
            steps = [.play("stand", times: 2, reversed: false), .play("walk", times: 2, reversed: false), .play("sit", times: 1, reversed: true),
                     .hold("sit", frame: 0, seconds: 3), .play("sit", times: 1, reversed: false), .play("stand", times: 1, reversed: false)]
        case .waiting:   steps = [.play("wait", times: 1, reversed: false)]
        case .streaming: steps = [.play("run", times: 1, reversed: false)]
        case .ready:     steps = [.play("cheer", times: 2, reversed: false), .play("jump", times: 1, reversed: false), .play("stand", times: 3, reversed: false)]
        case .failed:    steps = [.play("sigh", times: 1, reversed: false)]
        }
        runMascot(steps, index: 0, token: mascotToken)
    }
    private func mascotClip(_ name: String) -> UIImage? {
        if let clip = mascotClips[name] { return clip }
        let clip = NotoPet.image("rhino-" + name); mascotClips[name] = clip; return clip
    }
    private func runMascot(_ steps: [MascotStep], index: Int, token: UUID) {
        guard token == mascotToken, !steps.isEmpty, window != nil, !isHidden else { return }
        let duration: Double
        switch steps[index % steps.count] {
        case let .play(name, times, reversed):
            guard let clip = mascotClip(name), let frames = clip.images, !frames.isEmpty else { return }
            let ordered = reversed ? Array(frames.reversed()) : frames
            mascotView.stopAnimating()
            mascotView.animationImages = ordered; mascotView.animationDuration = clip.duration; mascotView.animationRepeatCount = times
            mascotView.image = ordered.last // what stays on screen when this step ends
            mascotView.startAnimating()
            duration = clip.duration * Double(times)
        case let .hold(name, frame, seconds):
            mascotView.stopAnimating(); mascotView.image = mascotClip(name)?.images?[safe: frame]
            duration = seconds
        }
        let work = DispatchWorkItem { [weak self] in self?.runMascot(steps, index: index + 1, token: token) }
        mascotWork = work
        DispatchQueue.main.asyncAfter(deadline: .now() + duration, execute: work)
    }
    /// Fading glow used by both skins.
    static func breath(period: CFTimeInterval, from: CGFloat, to: CGFloat) -> CAAnimation {
        let fade = CABasicAnimation(keyPath: "opacity"); fade.fromValue = 1; fade.toValue = 0.3
        let glow = CABasicAnimation(keyPath: "shadowRadius"); glow.fromValue = 6; glow.toValue = 1
        let scale = CABasicAnimation(keyPath: "transform.scale"); scale.fromValue = from; scale.toValue = to
        let group = CAAnimationGroup(); group.animations = [fade, glow, scale]; group.duration = period / 2
        group.autoreverses = true; group.repeatCount = .infinity
        group.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
        return group
    }
}

/// A hermit crab drawn in a 24-point box: a tall, pointed spiral shell (ridged, in the
/// state colour) with a dark opening, and a coral crab climbing out of it — a big
/// pincer and a small one, jointed walking legs, eyes on stalks and long antennae.
final class HermitCrabLayer: CALayer {
    private let shell = CAShapeLayer(), ridges = CAShapeLayer(), mouth = CAShapeLayer()
    private let crab = CALayer(), body = CAShapeLayer(), claw = CAShapeLayer(), smallClaw = CAShapeLayer(), legs = CAShapeLayer()
    private let eyes = CALayer(), stalks = CAShapeLayer(), pupils = CAShapeLayer()
    private static let coral = UIColor(red: 0.95, green: 0.42, blue: 0.30, alpha: 1)
    private static let shade = UIColor(red: 0.62, green: 0.22, blue: 0.16, alpha: 1)
    override init() {
        super.init()
        // Pivots are in the 24-point drawing; each part rotates about its joint.
        func part<T: CALayer>(_ layer: T, pivot: CGPoint, in parent: CALayer) -> T {
            layer.bounds = CGRect(x: 0, y: 0, width: 24, height: 24)
            layer.anchorPoint = CGPoint(x: pivot.x / 24, y: pivot.y / 24); layer.position = pivot
            parent.addSublayer(layer); return layer
        }
        _ = part(shell, pivot: CGPoint(x: 16, y: 12), in: self)
        _ = part(ridges, pivot: CGPoint(x: 16, y: 12), in: shell)
        _ = part(mouth, pivot: CGPoint(x: 16, y: 12), in: shell)
        _ = part(crab, pivot: CGPoint(x: 12, y: 16), in: self)
        _ = part(legs, pivot: CGPoint(x: 9, y: 17.5), in: crab)
        _ = part(smallClaw, pivot: CGPoint(x: 8, y: 14.5), in: crab)
        _ = part(body, pivot: CGPoint(x: 9, y: 15.5), in: crab)
        _ = part(eyes, pivot: CGPoint(x: 8, y: 13), in: crab)
        _ = part(stalks, pivot: CGPoint(x: 8, y: 13), in: eyes)
        _ = part(pupils, pivot: CGPoint(x: 8, y: 13), in: eyes)
        _ = part(claw, pivot: CGPoint(x: 7, y: 17), in: crab)

        // Shell: a turban shell whose whorls step down from the spire (up right) to the
        // body whorl, with the opening down and to the left.
        let cone = UIBezierPath()
        cone.move(to: CGPoint(x: 10, y: 17.8))
        cone.addCurve(to: CGPoint(x: 13, y: 8.5), controlPoint1: CGPoint(x: 8.8, y: 14), controlPoint2: CGPoint(x: 10.2, y: 10.4))
        cone.addCurve(to: CGPoint(x: 19.8, y: 1.6), controlPoint1: CGPoint(x: 15, y: 6), controlPoint2: CGPoint(x: 17.5, y: 3))
        cone.addQuadCurve(to: CGPoint(x: 20.4, y: 5.4), controlPoint: CGPoint(x: 21.8, y: 3.2))    // apex whorl
        cone.addQuadCurve(to: CGPoint(x: 21.2, y: 10.2), controlPoint: CGPoint(x: 23.2, y: 7.2))  // middle whorl
        cone.addCurve(to: CGPoint(x: 15.2, y: 19.8), controlPoint1: CGPoint(x: 24.4, y: 12.8), controlPoint2: CGPoint(x: 21.5, y: 19.6)) // body whorl
        cone.close()
        shell.path = cone.cgPath
        shell.shadowOffset = .zero; shell.shadowOpacity = 0.85; shell.shadowRadius = 3
        // Sutures run from each step across the shell, and a little curl marks the apex.
        let lines = UIBezierPath()
        lines.move(to: CGPoint(x: 21.2, y: 10.2)); lines.addQuadCurve(to: CGPoint(x: 11.2, y: 12.2), controlPoint: CGPoint(x: 16.5, y: 13.2))
        lines.move(to: CGPoint(x: 20.4, y: 5.4)); lines.addQuadCurve(to: CGPoint(x: 13.4, y: 8.2), controlPoint: CGPoint(x: 17, y: 8.2))
        lines.move(to: CGPoint(x: 19.3, y: 3.6)); lines.addQuadCurve(to: CGPoint(x: 20.2, y: 2.5), controlPoint: CGPoint(x: 19.4, y: 2.4))
        ridges.path = lines.cgPath; ridges.fillColor = nil; ridges.lineWidth = 1; ridges.lineCap = .round
        ridges.strokeColor = UIColor.white.withAlphaComponent(0.7).cgColor
        let clip = CAShapeLayer(); clip.path = cone.cgPath; ridges.mask = clip
        mouth.path = UIBezierPath(ovalIn: CGRect(x: 9.2, y: 13.2, width: 6.5, height: 6)).cgPath
        mouth.fillColor = UIColor.black.withAlphaComponent(0.35).cgColor

        body.path = UIBezierPath(ovalIn: CGRect(x: 5.5, y: 13.2, width: 6.5, height: 4.8)).cgPath
        // Big pincer: an upper and a lower jaw with a gap, on a short arm.
        let pincer = UIBezierPath()
        pincer.move(to: CGPoint(x: 7.5, y: 16.8)); pincer.addLine(to: CGPoint(x: 4.8, y: 17.6))
        pincer.move(to: CGPoint(x: 5.2, y: 18.2))
        pincer.addCurve(to: CGPoint(x: 0.6, y: 15.2), controlPoint1: CGPoint(x: 2.8, y: 18.6), controlPoint2: CGPoint(x: 0.6, y: 17.2))
        pincer.addCurve(to: CGPoint(x: 3.2, y: 16.4), controlPoint1: CGPoint(x: 1.6, y: 15.9), controlPoint2: CGPoint(x: 2.4, y: 16.3))
        pincer.addCurve(to: CGPoint(x: 1.2, y: 19.6), controlPoint1: CGPoint(x: 2.6, y: 17.4), controlPoint2: CGPoint(x: 1.8, y: 18.6))
        pincer.addCurve(to: CGPoint(x: 5.2, y: 18.2), controlPoint1: CGPoint(x: 2.6, y: 20), controlPoint2: CGPoint(x: 4.4, y: 19.4))
        claw.path = pincer.cgPath; claw.lineWidth = 1.3; claw.lineJoin = .round; claw.lineCap = .round
        let small = UIBezierPath()
        small.move(to: CGPoint(x: 7, y: 14.5)); small.addLine(to: CGPoint(x: 4.5, y: 13.5))
        small.append(UIBezierPath(ovalIn: CGRect(x: 2.4, y: 12.2, width: 2.8, height: 2.2)))
        smallClaw.path = small.cgPath; smallClaw.lineWidth = 1.1; smallClaw.lineCap = .round
        // Three bent walking legs: thigh down, shin out.
        let feet = UIBezierPath()
        for (x, reach) in [(7.2, 2.4), (9.2, 2.0), (11.2, 1.4)] as [(CGFloat, CGFloat)] {
            feet.move(to: CGPoint(x: x, y: 17.4)); feet.addLine(to: CGPoint(x: x - 0.8, y: 20.2)); feet.addLine(to: CGPoint(x: x - 0.8 - reach, y: 22.3))
        }
        legs.path = feet.cgPath; legs.fillColor = nil; legs.lineWidth = 1.1; legs.lineCap = .round; legs.lineJoin = .round
        // Short eye stalks, and two long antennae sweeping forward.
        let sticks = UIBezierPath()
        sticks.move(to: CGPoint(x: 7.2, y: 13.8)); sticks.addLine(to: CGPoint(x: 6.4, y: 10.4))
        sticks.move(to: CGPoint(x: 9.2, y: 13.6)); sticks.addLine(to: CGPoint(x: 9.4, y: 10.2))
        sticks.move(to: CGPoint(x: 6.8, y: 12.8)); sticks.addQuadCurve(to: CGPoint(x: 0.8, y: 8.6), controlPoint: CGPoint(x: 3.2, y: 9.8))
        sticks.move(to: CGPoint(x: 8.4, y: 12.4)); sticks.addQuadCurve(to: CGPoint(x: 3.4, y: 6.2), controlPoint: CGPoint(x: 5.2, y: 7.6))
        stalks.path = sticks.cgPath; stalks.fillColor = nil; stalks.lineWidth = 0.8; stalks.lineCap = .round
        let dots = UIBezierPath(ovalIn: CGRect(x: 5.1, y: 8.9, width: 2.6, height: 2.6))
        dots.append(UIBezierPath(ovalIn: CGRect(x: 8.1, y: 8.7, width: 2.6, height: 2.6)))
        pupils.path = dots.cgPath; pupils.fillColor = UIColor.black.cgColor
        for part in [body, claw, smallClaw] as [CAShapeLayer] { part.fillColor = Self.coral.cgColor; part.strokeColor = Self.coral.cgColor }
        for part in [legs, stalks] as [CAShapeLayer] { part.strokeColor = Self.shade.cgColor }
    }
    override init(layer: Any) { super.init(layer: layer) }
    required init?(coder: NSCoder) { fatalError() }
    func place(in rect: CGRect, scale: CGFloat) {
        let size = min(rect.width, rect.height)
        bounds = CGRect(x: 0, y: 0, width: 24, height: 24)
        position = CGPoint(x: rect.midX, y: rect.midY)
        transform = CATransform3DMakeScale(size / 24, size / 24, 1)
        for part in [shell, ridges, mouth, body, claw, smallClaw, legs, stalks, pupils] { part.contentsScale = scale * max(1, size / 24) }
    }
    /// Poses and animates the crab for a state; nil stops everything (skin hidden).
    func show(_ state: StatusLight.State?, color: UIColor, motion: Bool) {
        for part in [self, shell, crab, body, claw, smallClaw, legs, eyes, pupils] as [CALayer] { part.removeAllAnimations() }
        guard let state else { return }
        shell.fillColor = color.cgColor; shell.shadowColor = color.cgColor
        shell.opacity = state == .idle ? 0.55 : 1
        // Tucked in when idle or hurt, out and about otherwise.
        let tucked: CGFloat = state == .failed ? 0.55 : state == .idle ? 0.8 : 1
        crab.transform = CATransform3DConcat(CATransform3DMakeScale(tucked, tucked, 1), CATransform3DMakeTranslation((1 - tucked) * 9, 0, 0))
        guard motion else { return }
        func swing(_ key: String, from: CGFloat, to: CGFloat, period: CFTimeInterval, on layer: CALayer) {
            let a = CABasicAnimation(keyPath: key); a.fromValue = from; a.toValue = to; a.duration = period / 2
            a.autoreverses = true; a.repeatCount = .infinity; a.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
            layer.add(a, forKey: key)
        }
        if let period = state.period {
            let glow = CABasicAnimation(keyPath: "shadowRadius"); glow.fromValue = 5; glow.toValue = 0.5
            glow.duration = period / 2; glow.autoreverses = true; glow.repeatCount = .infinity
            shell.add(glow, forKey: "glow")
        }
        switch state {
        case .idle:
            // A slow blink from inside the shell.
            let blink = CAKeyframeAnimation(keyPath: "transform.scale.y")
            blink.values = [1, 1, 0.1, 1]; blink.keyTimes = [0, 0.9, 0.95, 1]; blink.duration = 3.5; blink.repeatCount = .infinity
            pupils.add(blink, forKey: "blink")
        case .waiting:
            swing("transform.rotation.z", from: -0.35, to: 0.35, period: 1.4, on: eyes) // looking around
        case .streaming:
            swing("transform.rotation.z", from: -0.3, to: 0.3, period: 0.25, on: legs) // scuttling
            swing("transform.translation.y", from: -0.7, to: 0.7, period: 0.5, on: body)
            swing("transform.translation.x", from: -0.6, to: 0.6, period: 0.5, on: self)
        case .ready:
            swing("transform.rotation.z", from: 0.2, to: -0.55, period: 0.9, on: claw) // waving the big pincer
            swing("transform.rotation.z", from: -0.15, to: 0.2, period: 0.9, on: smallClaw)
        case .failed:
            swing("transform.rotation.z", from: -0.12, to: 0.12, period: 0.18, on: shell) // shaking
        }
    }
}

/// A small drawn pet (kitten, puppy or piglet) for the status cell: a glowing halo and
/// collar in the state colour, and a pet that sleeps, looks around, bounces, wags or
/// shakes its head with the state. Drawn in a 24-point box like the hermit crab.
final class PetLayer: CALayer {
    enum Species { case kitten, puppy, piglet }
    private let halo = CAShapeLayer(), tail = CAShapeLayer(), body = CAShapeLayer()
    private let head = CALayer(), face = CAShapeLayer(), leftEar = CAShapeLayer(), rightEar = CAShapeLayer()
    private let eyes = CAShapeLayer(), sleepy = CAShapeLayer(), snout = CAShapeLayer(), marks = CAShapeLayer(), tongue = CAShapeLayer()
    private let collar = CAShapeLayer(), tag = CAShapeLayer()
    private let species: Species
    init(_ species: Species) {
        self.species = species
        super.init()
        func part<T: CALayer>(_ layer: T, pivot: CGPoint, in parent: CALayer) -> T {
            layer.bounds = CGRect(x: 0, y: 0, width: 24, height: 24)
            layer.anchorPoint = CGPoint(x: pivot.x / 24, y: pivot.y / 24); layer.position = pivot
            parent.addSublayer(layer); return layer
        }
        _ = part(halo, pivot: CGPoint(x: 12, y: 12), in: self)
        _ = part(tail, pivot: CGPoint(x: 17, y: 19), in: self)
        _ = part(body, pivot: CGPoint(x: 12, y: 22), in: self)
        _ = part(collar, pivot: CGPoint(x: 12, y: 16), in: self)
        _ = part(tag, pivot: CGPoint(x: 12, y: 16), in: self)
        _ = part(head, pivot: CGPoint(x: 12, y: 15), in: self)
        _ = part(leftEar, pivot: CGPoint(x: 8, y: 7), in: head)
        _ = part(rightEar, pivot: CGPoint(x: 16, y: 7), in: head)
        _ = part(face, pivot: CGPoint(x: 12, y: 11), in: head)
        for layer in [snout, marks, tongue, eyes, sleepy] { _ = part(layer, pivot: CGPoint(x: 12, y: 11), in: head) } // nose and nostrils sit on the snout

        let (fur, dark, pink): (UIColor, UIColor, UIColor) = switch species {
        case .kitten: (UIColor(red: 0.97, green: 0.67, blue: 0.33, alpha: 1), UIColor(red: 0.80, green: 0.45, blue: 0.18, alpha: 1), UIColor(red: 0.98, green: 0.55, blue: 0.62, alpha: 1))
        case .puppy: (UIColor(red: 0.86, green: 0.66, blue: 0.45, alpha: 1), UIColor(red: 0.55, green: 0.36, blue: 0.22, alpha: 1), UIColor(red: 0.96, green: 0.45, blue: 0.52, alpha: 1))
        case .piglet: (UIColor(red: 0.99, green: 0.72, blue: 0.78, alpha: 1), UIColor(red: 0.90, green: 0.50, blue: 0.60, alpha: 1), UIColor(red: 0.93, green: 0.45, blue: 0.56, alpha: 1))
        }
        halo.path = UIBezierPath(ovalIn: CGRect(x: 1.5, y: 1.5, width: 21, height: 21)).cgPath
        halo.shadowOffset = .zero; halo.shadowRadius = 3; halo.shadowOpacity = 0.8
        body.path = UIBezierPath(ovalIn: CGRect(x: 6.5, y: 15.5, width: 11, height: 7.5)).cgPath
        face.path = UIBezierPath(ovalIn: CGRect(x: 5, y: 4.8, width: 14, height: 12.4)).cgPath
        let band = UIBezierPath(); band.move(to: CGPoint(x: 8, y: 16.6)); band.addQuadCurve(to: CGPoint(x: 16, y: 16.6), controlPoint: CGPoint(x: 12, y: 18.6))
        collar.path = band.cgPath; collar.fillColor = nil; collar.lineWidth = 1.6; collar.lineCap = .round
        tag.path = UIBezierPath(ovalIn: CGRect(x: 11, y: 17, width: 2, height: 2)).cgPath
        // Open eyes, and closed "sleeping" arcs shown instead when idle.
        let open = UIBezierPath(ovalIn: CGRect(x: 8.2, y: 9.6, width: 2, height: 2.4))
        open.append(UIBezierPath(ovalIn: CGRect(x: 13.8, y: 9.6, width: 2, height: 2.4)))
        eyes.path = open.cgPath; eyes.fillColor = UIColor(white: 0.12, alpha: 1).cgColor
        let shut = UIBezierPath()
        shut.move(to: CGPoint(x: 8, y: 10.8)); shut.addQuadCurve(to: CGPoint(x: 10.4, y: 10.8), controlPoint: CGPoint(x: 9.2, y: 12))
        shut.move(to: CGPoint(x: 13.6, y: 10.8)); shut.addQuadCurve(to: CGPoint(x: 16, y: 10.8), controlPoint: CGPoint(x: 14.8, y: 12))
        sleepy.path = shut.cgPath; sleepy.fillColor = nil; sleepy.lineWidth = 0.9; sleepy.lineCap = .round
        sleepy.strokeColor = UIColor(white: 0.12, alpha: 1).cgColor
        let lick = UIBezierPath(ovalIn: CGRect(x: 11.1, y: 14.4, width: 1.8, height: 2.2)); tongue.path = lick.cgPath
        tongue.fillColor = pink.cgColor; tongue.isHidden = true

        let ears = (UIBezierPath(), UIBezierPath()), tailPath = UIBezierPath(), snoutPath = UIBezierPath(), markPath = UIBezierPath()
        switch species {
        case .kitten:
            // Pointed ears, a pink nose, whiskers and a long tail curling up.
            ears.0.move(to: CGPoint(x: 5.6, y: 8.5)); ears.0.addLine(to: CGPoint(x: 6.4, y: 2.8)); ears.0.addLine(to: CGPoint(x: 10.4, y: 5.6)); ears.0.close()
            ears.1.move(to: CGPoint(x: 18.4, y: 8.5)); ears.1.addLine(to: CGPoint(x: 17.6, y: 2.8)); ears.1.addLine(to: CGPoint(x: 13.6, y: 5.6)); ears.1.close()
            snoutPath.move(to: CGPoint(x: 11.2, y: 12.8)); snoutPath.addLine(to: CGPoint(x: 12.8, y: 12.8)); snoutPath.addLine(to: CGPoint(x: 12, y: 13.8)); snoutPath.close()
            for (a, b) in [(CGPoint(x: 9.5, y: 13.4), CGPoint(x: 5.4, y: 12.6)), (CGPoint(x: 9.5, y: 14.2), CGPoint(x: 5.6, y: 14.8)),
                           (CGPoint(x: 14.5, y: 13.4), CGPoint(x: 18.6, y: 12.6)), (CGPoint(x: 14.5, y: 14.2), CGPoint(x: 18.4, y: 14.8))] { markPath.move(to: a); markPath.addLine(to: b) }
            tailPath.move(to: CGPoint(x: 16.5, y: 20)); tailPath.addCurve(to: CGPoint(x: 21.5, y: 12.5), controlPoint1: CGPoint(x: 21.5, y: 21), controlPoint2: CGPoint(x: 23, y: 16))
            tail.lineWidth = 1.8
        case .puppy:
            // Floppy ears, a light muzzle with a black nose, and a short wagging tail.
            ears.0.append(UIBezierPath(ovalIn: CGRect(x: 3.4, y: 5.4, width: 4.2, height: 8.2)))
            ears.1.append(UIBezierPath(ovalIn: CGRect(x: 16.4, y: 5.4, width: 4.2, height: 8.2)))
            snoutPath.append(UIBezierPath(ovalIn: CGRect(x: 9.2, y: 11.8, width: 5.6, height: 4.2)))
            markPath.append(UIBezierPath(ovalIn: CGRect(x: 10.9, y: 12.2, width: 2.2, height: 1.6)))
            tailPath.move(to: CGPoint(x: 16.5, y: 19.5)); tailPath.addQuadCurve(to: CGPoint(x: 20.8, y: 15.2), controlPoint: CGPoint(x: 20, y: 19))
            tail.lineWidth = 2
        case .piglet:
            // Folded triangle ears, a big snout with two nostrils, and a curly tail.
            ears.0.move(to: CGPoint(x: 6, y: 7.2)); ears.0.addLine(to: CGPoint(x: 5.2, y: 3.4)); ears.0.addLine(to: CGPoint(x: 9.8, y: 5.4)); ears.0.close()
            ears.1.move(to: CGPoint(x: 18, y: 7.2)); ears.1.addLine(to: CGPoint(x: 18.8, y: 3.4)); ears.1.addLine(to: CGPoint(x: 14.2, y: 5.4)); ears.1.close()
            snoutPath.append(UIBezierPath(ovalIn: CGRect(x: 9.3, y: 11.9, width: 5.4, height: 3.8)))
            markPath.append(UIBezierPath(ovalIn: CGRect(x: 10.5, y: 13.1, width: 1.1, height: 1.4)))
            markPath.append(UIBezierPath(ovalIn: CGRect(x: 12.4, y: 13.1, width: 1.1, height: 1.4)))
            tailPath.move(to: CGPoint(x: 17.4, y: 19)); tailPath.addCurve(to: CGPoint(x: 20.2, y: 17.6), controlPoint1: CGPoint(x: 18.6, y: 17), controlPoint2: CGPoint(x: 21.6, y: 16.2))
            tailPath.addCurve(to: CGPoint(x: 19.4, y: 19.6), controlPoint1: CGPoint(x: 19.2, y: 19), controlPoint2: CGPoint(x: 18.4, y: 18.8))
            tail.lineWidth = 1.2
        }
        leftEar.path = ears.0.cgPath; rightEar.path = ears.1.cgPath; snout.path = snoutPath.cgPath; marks.path = markPath.cgPath; tail.path = tailPath.cgPath
        tail.fillColor = nil; tail.lineCap = .round; tail.strokeColor = (species == .kitten ? fur : dark).cgColor
        for layer in [body, face] { layer.fillColor = fur.cgColor }
        for layer in [leftEar, rightEar] { layer.fillColor = (species == .kitten ? fur : dark).cgColor; layer.strokeColor = dark.cgColor; layer.lineWidth = 0.5; layer.lineJoin = .round }
        switch species {
        case .kitten:
            snout.fillColor = pink.cgColor
            marks.fillColor = nil; marks.strokeColor = dark.withAlphaComponent(0.7).cgColor; marks.lineWidth = 0.5; marks.lineCap = .round
        case .puppy:
            snout.fillColor = UIColor(red: 0.98, green: 0.90, blue: 0.80, alpha: 1).cgColor
            marks.fillColor = UIColor(white: 0.12, alpha: 1).cgColor
        case .piglet:
            snout.fillColor = pink.cgColor; snout.strokeColor = dark.cgColor; snout.lineWidth = 0.5
            marks.fillColor = dark.cgColor
        }
    }
    override init(layer: Any) { species = (layer as? PetLayer)?.species ?? .kitten; super.init(layer: layer) }
    required init?(coder: NSCoder) { fatalError() }
    func place(in rect: CGRect, scale: CGFloat) {
        let size = min(rect.width, rect.height)
        bounds = CGRect(x: 0, y: 0, width: 24, height: 24)
        position = CGPoint(x: rect.midX, y: rect.midY)
        transform = CATransform3DMakeScale(size / 24, size / 24, 1)
        for part in [halo, tail, body, face, leftEar, rightEar, eyes, sleepy, snout, marks, tongue, collar, tag] { part.contentsScale = scale * max(1, size / 24) }
    }
    func show(_ state: StatusLight.State?, color: UIColor, motion: Bool) {
        for part in [self, halo, tail, head, leftEar, rightEar, eyes, collar, tag] as [CALayer] { part.removeAllAnimations() }
        guard let state else { return }
        halo.fillColor = color.withAlphaComponent(0.22).cgColor; halo.shadowColor = color.cgColor
        halo.opacity = state == .idle ? 0.5 : 1
        collar.strokeColor = color.cgColor; tag.fillColor = color.cgColor
        let asleep = state == .idle
        eyes.isHidden = asleep; sleepy.isHidden = !asleep
        tongue.isHidden = !(species == .puppy && state == .ready)
        // Ears droop when something went wrong.
        let droop: CGFloat = state == .failed ? 0.35 : 0
        leftEar.transform = CATransform3DMakeRotation(-droop, 0, 0, 1); rightEar.transform = CATransform3DMakeRotation(droop, 0, 0, 1)
        guard motion else { return }
        func swing(_ key: String, from: CGFloat, to: CGFloat, period: CFTimeInterval, on layer: CALayer) {
            let a = CABasicAnimation(keyPath: key); a.fromValue = from; a.toValue = to; a.duration = period / 2
            a.autoreverses = true; a.repeatCount = .infinity; a.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
            layer.add(a, forKey: key)
        }
        if let period = state.period {
            swing("shadowRadius", from: 5, to: 0.5, period: period, on: halo)
            swing("opacity", from: 1, to: 0.4, period: period, on: tag)
        }
        switch state {
        case .idle:
            swing("transform.scale.y", from: 1, to: 1.035, period: 3, on: head) // slow sleeping breaths
        case .waiting:
            swing("transform.rotation.z", from: -0.16, to: 0.16, period: 1.4, on: head) // head tilt, looking around
            swing("transform.rotation.z", from: 0, to: -0.25, period: 0.7, on: leftEar)
        case .streaming:
            swing("transform.translation.y", from: 0, to: -1.6, period: 0.42, on: self) // bouncing along
            swing("transform.rotation.z", from: -0.45, to: 0.45, period: 0.3, on: tail)
        case .ready:
            swing("transform.rotation.z", from: -0.35, to: 0.35, period: 0.8, on: tail) // happy wag
            let blink = CAKeyframeAnimation(keyPath: "transform.scale.y")
            blink.values = [1, 1, 0.1, 1]; blink.keyTimes = [0, 0.92, 0.96, 1]; blink.duration = 3; blink.repeatCount = .infinity
            eyes.add(blink, forKey: "blink")
        case .failed:
            swing("transform.rotation.z", from: -0.12, to: 0.12, period: 0.2, on: head) // shaking its head
        }
    }
}

/// Loads a bundled animated Noto Emoji pet (a small animated PNG) into a looping image.
enum NotoPet {
    static func image(_ name: String) -> UIImage? {
        guard let url = Bundle.main.url(forResource: name, withExtension: "png", subdirectory: "Pets")
                ?? Bundle.main.url(forResource: name, withExtension: "png") else { return nil }
        return image(at: url)
    }
    static func image(at url: URL) -> UIImage? {
        guard let source = CGImageSourceCreateWithURL(url as CFURL, nil) else { return nil }
        var frames: [UIImage] = [], duration: Double = 0
        for index in 0..<CGImageSourceGetCount(source) {
            guard let frame = CGImageSourceCreateImageAtIndex(source, index, nil) else { continue }
            let png = (CGImageSourceCopyPropertiesAtIndex(source, index, nil) as? [CFString: Any])?[kCGImagePropertyPNGDictionary] as? [CFString: Any]
            duration += (png?[kCGImagePropertyAPNGUnclampedDelayTime] as? Double) ?? (png?[kCGImagePropertyAPNGDelayTime] as? Double) ?? 1.0 / 20
            frames.append(UIImage(cgImage: frame))
        }
        guard !frames.isEmpty else { return nil }
        return frames.count == 1 ? frames[0] : UIImage.animatedImage(with: frames, duration: max(0.3, duration))
    }
}

extension StatusSkin {
    var petSpecies: PetLayer.Species? { switch self { case .kitten: return .kitten; case .puppy: return .puppy; case .piglet: return .piglet; default: return nil } }
}

private extension Array {
    subscript(safe index: Int) -> Element? { indices.contains(index) ? self[index] : nil }
}
