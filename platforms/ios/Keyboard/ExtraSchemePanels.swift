import UIKit
#if KEYBOARD_LAYOUT_TESTS
@testable import RIMES
#endif

/// Input schemes that replace the keys instead of feeding Rime. The Rime scheme stays loaded
/// underneath, so switching back is instant.
enum ExtraScheme: String, CaseIterable {
    case voice, handwriting
    var title: String {
        switch self {
        case .voice: return L("语音 · Voice（实验）", "Voice (experimental)")
        case .handwriting: return L("手写 · Handwriting（实验）", "Handwriting (experimental)")
        }
    }
    var symbol: String { self == .voice ? "mic" : "hand.draw" }
    /// Set by the app; only the app writes the group defaults.
    var enabledKey: String { self == .voice ? "voiceDictation.enabled" : "handwriting.enabled" }
    var isEnabled: Bool { UserDefaults(suiteName: ConfigurationStore.groupID)?.bool(forKey: enabledKey) ?? false }
}

/// Key area of an extra scheme: the tape ribbon on the left, the scheme's own input area on
/// the right, and [keyboard] [globe] [  space  ] [delete] [return] below. Candidates for the
/// cell under the read head show in the keyboard's usual candidate row above.
class ExtraSchemePanel: UIView {
    var onExit: (() -> Void)?
    var onSpace: (() -> Void)?
    var onDelete: (() -> Void)?
    var onReturn: (() -> Void)?
    var onPress: (() -> Void)?
    /// The ribbon's read head moved to this row.
    var onPage: ((Int) -> Void)?

    let ribbon = TapeRibbon()
    let globe = KeycapButton()
    private let keyboard = KeycapButton(), space = KeycapButton(), delete = KeycapButton(), enter = KeycapButton()
    /// Subclasses add their own keys here so they follow the theme.
    var themedKeys: [KeycapButton] { [keyboard, globe, space, delete, enter] }

    var theme: StatusSkin = .rhino { didSet { themedKeys.forEach { $0.theme = theme }; ribbon.accent = theme.palette.accent } }
    var legacyKey = false { didSet { themedKeys.forEach { $0.legacyKey = legacyKey } } }
    var showsGlobe = true { didSet { if oldValue != showsGlobe { globe.isHidden = !showsGlobe; setNeedsLayout() } } }
    var returnTitle = "" { didSet { if oldValue != returnTitle { enter.setTitle(returnTitle, for: .normal); enter.accessibilityLabel = returnTitle } } }

    override init(frame: CGRect) {
        super.init(frame: frame)
        addSubview(ribbon)
        ribbon.onHead = { [weak self] row in self?.turned(to: row) }
        keyboard.symbol("keyboard", label: L("返回键盘", "Back to keys"))
        hook(keyboard) { [weak self] in self?.onExit?() }
        globe.symbol("globe", label: L("下一个键盘", "Next keyboard"))
        space.setTitle(L("空格", "space"), for: .normal); space.accessibilityLabel = L("空格", "Space")
        hook(space) { [weak self] in self?.onSpace?() }
        delete.symbol("delete.left", label: L("删除", "Delete"))
        hook(delete) { [weak self] in self?.onDelete?() }
        hook(enter) { [weak self] in self?.onReturn?() }
        for key in themedKeys { key.titleLabel?.font = .systemFont(ofSize: 16, weight: .medium); addSubview(key) }
    }
    required init?(coder: NSCoder) { fatalError() }

    func hook(_ button: UIButton, _ action: @escaping () -> Void) {
        button.addAction(UIAction { [weak self] _ in self?.onPress?() }, for: .touchDown)
        button.addAction(UIAction { _ in action() }, for: .touchUpInside)
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        let inset: CGFloat = 5, gap: CGFloat = 6, rowHeight = min(42, max(34, bounds.height * 0.13))
        let row = CGRect(x: inset, y: bounds.height - rowHeight - 4, width: bounds.width - 2 * inset, height: rowHeight)
        let side = min(56, (row.width - gap * 4) / 6.5), returnWidth = side * 1.6
        var x = row.minX
        keyboard.frame = CGRect(x: x, y: row.minY, width: side, height: row.height); x += side + gap
        if showsGlobe { globe.frame = CGRect(x: x, y: row.minY, width: side, height: row.height); x += side + gap }
        let spaceWidth = max(0, row.maxX - x - side - returnWidth - 2 * gap)
        space.frame = CGRect(x: x, y: row.minY, width: spaceWidth, height: row.height); x += spaceWidth + gap
        delete.frame = CGRect(x: x, y: row.minY, width: side, height: row.height); x += side + gap
        enter.frame = CGRect(x: x, y: row.minY, width: returnWidth, height: row.height)
        let content = CGRect(x: inset, y: 4, width: row.width, height: max(0, row.minY - 10))
        let ribbonWidth = self.ribbonWidth(for: content.width)
        ribbon.frame = CGRect(x: content.minX, y: content.minY, width: ribbonWidth, height: content.height)
        layoutArea(in: CGRect(x: content.minX + ribbonWidth + gap, y: content.minY, width: max(0, content.width - ribbonWidth - gap), height: content.height))
    }
    func layoutArea(in rect: CGRect) {}
    /// The read head reached `row`; handwriting turns the pad's page first.
    func turned(to row: Int) { onPage?(row) }
    func sync(_ tape: InputTape) { ribbon.update(tape) }
    /// Voice words need room; handwriting cells are single characters and leave the pad wide.
    func ribbonWidth(for width: CGFloat) -> CGFloat { min(84, max(56, width * 0.2)) }
}

/// Voice: one round microphone in the middle of the area, nothing else. Rings around it follow
/// the voice while listening and breathe while the microphone starts. Recognized words go onto
/// the tape; problems show in the keyboard's usual prompt, not here.
final class VoiceInputPanel: ExtraSchemePanel {
    enum Phase: Equatable { case idle, starting, listening }
    var onMic: (() -> Void)?
    private let orb = VoiceOrb()

    var phase: Phase = .idle { didSet { if oldValue != phase { orb.phase = phase } } }
    /// Input level, 0…1, while listening.
    var level: Float = 0 { didSet { orb.level = CGFloat(level) } }
    override var theme: StatusSkin { didSet { orb.palette = theme.palette } }

    override init(frame: CGRect) {
        super.init(frame: frame)
        orb.accessibilityIdentifier = "keyboard.voice.mic"
        orb.addAction(UIAction { [weak self] _ in self?.onPress?() }, for: .touchDown)
        orb.addAction(UIAction { [weak self] _ in self?.onMic?() }, for: .touchUpInside)
        addSubview(orb)
    }
    required init?(coder: NSCoder) { fatalError() }

    override func layoutArea(in rect: CGRect) { orb.frame = rect }
}

/// The microphone control: a filled circle with the mic glyph, inside two level rings.
private final class VoiceOrb: UIControl {
    var phase: VoiceInputPanel.Phase = .idle { didSet { if oldValue != phase { update() } } }
    var level: CGFloat = 0 { didSet { follow(level) } }
    var palette: KeyboardPalette? { didSet { update() } }
    private let disc = CAShapeLayer(), inner = CAShapeLayer(), outer = CAShapeLayer()
    private let glyph = UIImageView()
    private var smoothed: CGFloat = 0

    override init(frame: CGRect) {
        super.init(frame: frame)
        for ring in [outer, inner] { ring.fillColor = nil; ring.lineWidth = 2; layer.addSublayer(ring) }
        layer.addSublayer(disc)
        glyph.contentMode = .scaleAspectFit; glyph.isUserInteractionEnabled = false
        addSubview(glyph)
        isAccessibilityElement = true; accessibilityTraits = .button
        registerForTraitChanges([UITraitUserInterfaceStyle.self]) { (orb: VoiceOrb, _: UITraitCollection) in orb.update() }
        update()
    }
    required init?(coder: NSCoder) { fatalError() }

    private var radius: CGFloat { min(44, max(26, min(bounds.width, bounds.height) * 0.3)) }

    override func layoutSubviews() {
        super.layoutSubviews()
        let center = CGPoint(x: bounds.midX, y: bounds.midY), r = radius
        for (shape, scale) in [(disc, CGFloat(1)), (inner, 1.32), (outer, 1.68)] {
            shape.bounds = CGRect(x: 0, y: 0, width: r * 2 * scale, height: r * 2 * scale)
            shape.position = center
            shape.path = UIBezierPath(ovalIn: shape.bounds).cgPath
        }
        let side = r * 0.9
        glyph.frame = CGRect(x: center.x - side / 2, y: center.y - side / 2, width: side, height: side)
        glyph.preferredSymbolConfiguration = .init(pointSize: r * 0.72, weight: .semibold)
    }

    override var isHighlighted: Bool { didSet { UIView.animate(withDuration: 0.12) { self.transform = self.isHighlighted ? CGAffineTransform(scaleX: 0.94, y: 0.94) : .identity } } }

    private func update() {
        let accent = (palette?.accent ?? .systemBlue).resolvedColor(with: traitCollection)
        let key = (palette?.key ?? .secondarySystemBackground).resolvedColor(with: traitCollection)
        let on = phase != .idle
        CATransaction.begin(); CATransaction.setAnimationDuration(0.2)
        disc.fillColor = (on ? accent : key).cgColor
        disc.shadowColor = UIColor.black.cgColor; disc.shadowOpacity = on ? 0 : 0.18; disc.shadowRadius = 2; disc.shadowOffset = CGSize(width: 0, height: 1)
        inner.strokeColor = accent.withAlphaComponent(0.45).cgColor
        outer.strokeColor = accent.withAlphaComponent(0.22).cgColor
        CATransaction.commit()
        glyph.image = UIImage(systemName: on ? "waveform" : "mic.fill")
        glyph.tintColor = on ? (palette?.accentInk ?? .white) : accent
        accessibilityLabel = on ? L("停止说话", "Stop speaking") : L("开始说话", "Start speaking")
        for ring in [inner, outer] { ring.removeAllAnimations(); ring.opacity = on ? 1 : 0 }
        if phase == .starting {
            // Breathing while the microphone starts.
            for (ring, delay) in [(inner, 0.0), (outer, 0.25)] {
                let pulse = CABasicAnimation(keyPath: "transform.scale")
                pulse.fromValue = 0.86; pulse.toValue = 1.06; pulse.duration = 0.7; pulse.autoreverses = true
                pulse.repeatCount = .infinity; pulse.beginTime = CACurrentMediaTime() + delay
                pulse.timingFunction = CAMediaTimingFunction(name: .easeInEaseOut)
                ring.add(pulse, forKey: "breathe")
            }
        }
        if phase != .listening { smoothed = 0; follow(0) }
    }

    /// Quick to rise, slow to fall, so the rings read as a voice and not as noise.
    private func follow(_ value: CGFloat) {
        guard phase == .listening else { return }
        smoothed = value > smoothed ? 0.6 * value + 0.4 * smoothed : 0.85 * smoothed + 0.15 * value
        CATransaction.begin(); CATransaction.setAnimationDuration(0.09)
        inner.setAffineTransform(CGAffineTransform(scaleX: 0.9 + smoothed * 0.25, y: 0.9 + smoothed * 0.25))
        outer.setAffineTransform(CGAffineTransform(scaleX: 0.86 + smoothed * 0.4, y: 0.86 + smoothed * 0.4))
        outer.opacity = Float(0.35 + smoothed * 0.65)
        CATransaction.commit()
    }
}

/// Handwriting: a writing pad that shows one page of the tape. The last page is where new
/// characters are written; a short pause, or scrolling the ribbon one row past it, pushes the
/// page onto the tape to be recognized in the background and opens a fresh one. While a finger
/// holds the ribbon, the pause never pushes: the left hand decides. Scrolling back opens the
/// earlier pages with their ink; writing on one rewrites that character when the page is left.
final class HandwritingInputPanel: ExtraSchemePanel {
    /// A new character from the writing page, with the pad size it was written in.
    var onCharacter: ((_ ink: [InkTrace], _ size: CGSize) -> Void)?
    /// An earlier character written over.
    var onRewrite: ((_ id: UUID, _ ink: [InkTrace], _ size: CGSize) -> Void)?
    private let pad = HandwritingPad()
    private var pauseTimer: Timer?
    private var tape = InputTape()
    /// The page on the pad: an index into the tape, or `tape.cells.count` for the writing page.
    private var page = 0
    /// The earlier character on the pad and the ink it was opened with.
    private var opened: (id: UUID, ink: [InkTrace])?
    static let pause: TimeInterval = 0.5

    override init(frame: CGRect) {
        super.init(frame: frame)
        ribbon.pages = true
        pad.accessibilityIdentifier = "keyboard.handwriting.pad"
        pad.onStrokeBegan = { [weak self] in
            guard let self else { return }
            self.pauseTimer?.invalidate()
            if self.opened == nil { self.ribbon.draft = true }
        }
        pad.onStrokeEnded = { [weak self] in self?.schedulePause() }
        ribbon.onAdvance = { [weak self] in self?.pushWritingPage() }
        ribbon.onHold = { [weak self] holding in
            guard let self else { return }
            if holding { self.pauseTimer?.invalidate() } else if self.pad.hasInk { self.schedulePause() }
        }
        addSubview(pad)
    }
    required init?(coder: NSCoder) { fatalError() }

    var hasInk: Bool { pad.hasInk }

    override func sync(_ tape: InputTape) {
        self.tape = tape
        // An opened character that left the tape (deleted, sent) closes its page.
        if let opened, !tape.cells.contains(where: { $0.id == opened.id }) { self.opened = nil; pad.clear(); page = tape.cells.count }
        if opened == nil { page = tape.cells.count }
        ribbon.update(tape)
    }

    override func turned(to row: Int) {
        let writing = tape.cells.count
        if opened == nil && row != writing && pad.hasInk { pushWritingPage() }
        closeOpenedPage()
        let writingNow = tape.cells.count
        let target = min(max(0, row), writingNow)
        let forward = target >= page
        page = target
        if target < writingNow {
            let cell = tape.cells[target]
            opened = (cell.id, cell.ink)
            pad.turn(to: cell.ink, forward: forward)
        } else {
            pad.turn(to: [], forward: forward)
        }
        onPage?(row)
    }

    /// Undoes the last stroke; false when the pad is empty.
    func undoStroke() -> Bool {
        guard pad.hasInk else { return false }
        pad.removeLastStroke()
        pauseTimer?.invalidate()
        if pad.hasInk { schedulePause() } else if opened == nil { ribbon.draft = false }
        return true
    }
    /// Pushes or rewrites what is on the pad now (before Space or Return act on the tape).
    func flush() { pauseTimer?.invalidate(); if opened != nil { closeOpenedPage(); pad.turn(to: [], forward: true); page = tape.cells.count } else { pushWritingPage() } }
    func reset() { pauseTimer?.invalidate(); opened = nil; pad.clear(); ribbon.draft = false }

    private func schedulePause() {
        pauseTimer?.invalidate()
        guard !ribbon.isHeld else { return }
        pauseTimer = Timer.scheduledTimer(withTimeInterval: Self.pause, repeats: false) { [weak self] _ in
            MainActor.assumeIsolated {
                guard let self else { return }
                if self.opened != nil { self.closeOpenedPage(keepOpen: true) } else { self.pushWritingPage() }
            }
        }
    }
    private func pushWritingPage() {
        pauseTimer?.invalidate()
        guard opened == nil, pad.hasInk else { return }
        let ink = pad.traces, size = pad.bounds.size
        pad.scrollAway()
        ribbon.draft = false
        onCharacter?(ink, size)
    }
    /// Sends a rewritten earlier character for recognition if its ink changed.
    private func closeOpenedPage(keepOpen: Bool = false) {
        guard let opened else { return }
        let ink = pad.traces
        if ink != opened.ink { onRewrite?(opened.id, ink, pad.bounds.size) }
        self.opened = keepOpen ? (opened.id, ink) : nil
    }

    override func ribbonWidth(for width: CGFloat) -> CGFloat { 54 }
    override func layoutArea(in rect: CGRect) { pad.frame = rect }
}

/// One stroke: the raw touch points (what recognition reads) and the smoothed brush samples
/// (what is drawn). Each new point extends a quadratic Bézier through the midpoints of the last
/// three, so the line has no corners; its width follows the writing speed (quick is thin,
/// slow is full) through a low-pass filter, or the pressure of an Apple Pencil.
struct InkStroke {
    private(set) var raw: [CGPoint] = []
    private(set) var times: [TimeInterval] = []
    /// What recognition reads: every touch point with its time.
    var trace: InkTrace { InkTrace(points: raw, times: times) }
    private(set) var samples: [(point: CGPoint, width: CGFloat)] = []
    private var window: [(point: CGPoint, width: CGFloat)] = []
    private var lastTime: TimeInterval = 0
    private var velocity: CGFloat = 0
    let minWidth: CGFloat, maxWidth: CGFloat

    init(at point: CGPoint, time: TimeInterval, force: CGFloat?, minWidth: CGFloat, maxWidth: CGFloat) {
        self.minWidth = minWidth; self.maxWidth = maxWidth
        raw = [point]; times = [time]; lastTime = time
        let width = force.map { minWidth + (maxWidth - minWidth) * min(1, $0) } ?? maxWidth * 0.8
        window = [(point, width)]; samples = [(point, width)]
    }

    /// Adds a touch point; true when the drawn line changed.
    @discardableResult mutating func add(_ point: CGPoint, time: TimeInterval, force: CGFloat?) -> Bool {
        raw.append(point); times.append(time)
        guard let last = window.last, hypot(point.x - last.point.x, point.y - last.point.y) >= 1.2 else { return false }
        let distance = hypot(point.x - last.point.x, point.y - last.point.y)
        let speed = distance / CGFloat(max(time - lastTime, 1.0 / 240))
        lastTime = time
        velocity = 0.6 * speed + 0.4 * velocity
        let target = force.map { minWidth + (maxWidth - minWidth) * min(1, $0) }
            ?? min(maxWidth, max(minWidth, maxWidth / (1 + velocity / 700)))
        let width = 0.55 * last.width + 0.45 * target
        window.append((point, width))
        if window.count > 3 { window.removeFirst() }
        if window.count == 3 { appendCurve(window[0], window[1], window[2]) }
        return true
    }

    /// Redraws a recorded stroke the way it was written.
    init(replaying trace: InkTrace, minWidth: CGFloat, maxWidth: CGFloat) {
        let first = trace.points.first ?? .zero, start = trace.times.first ?? 0
        self.init(at: first, time: start, force: nil, minWidth: minWidth, maxWidth: maxWidth)
        for (point, time) in zip(trace.points.dropFirst(), trace.times.dropFirst()) { add(point, time: time, force: nil) }
        finish()
    }

    /// Ends the line at the last point instead of the last midpoint.
    mutating func finish() {
        guard window.count >= 2, let end = window.last, let start = samples.last, start.point != end.point else { return }
        appendLine(from: start, to: end)
    }

    private mutating func appendCurve(_ p0: (point: CGPoint, width: CGFloat), _ p1: (point: CGPoint, width: CGFloat), _ p2: (point: CGPoint, width: CGFloat)) {
        let m0 = mid(p0.point, p1.point), m1 = mid(p1.point, p2.point)
        let w0 = (p0.width + p1.width) / 2, w1 = (p1.width + p2.width) / 2
        let length = hypot(m0.x - p1.point.x, m0.y - p1.point.y) + hypot(p1.point.x - m1.x, p1.point.y - m1.y)
        let steps = max(2, Int(length / 1.5))
        if samples.count == 1, let first = samples.first { appendLine(from: first, to: (m0, w0)) }
        for step in 1...steps {
            let t = CGFloat(step) / CGFloat(steps), u = 1 - t
            let point = CGPoint(x: u * u * m0.x + 2 * u * t * p1.point.x + t * t * m1.x,
                                y: u * u * m0.y + 2 * u * t * p1.point.y + t * t * m1.y)
            samples.append((point, w0 + (w1 - w0) * t))
        }
    }
    private mutating func appendLine(from a: (point: CGPoint, width: CGFloat), to b: (point: CGPoint, width: CGFloat)) {
        let steps = max(1, Int(hypot(b.point.x - a.point.x, b.point.y - a.point.y) / 1.5))
        for step in 1...steps {
            let t = CGFloat(step) / CGFloat(steps)
            samples.append((CGPoint(x: a.point.x + (b.point.x - a.point.x) * t, y: a.point.y + (b.point.y - a.point.y) * t), a.width + (b.width - a.width) * t))
        }
    }
    private func mid(_ a: CGPoint, _ b: CGPoint) -> CGPoint { CGPoint(x: (a.x + b.x) / 2, y: (a.y + b.y) / 2) }

    /// Draws the samples as short round-capped segments of varying width.
    func draw(in context: CGContext) {
        guard let first = samples.first else { return }
        if samples.count == 1 {
            context.fillEllipse(in: CGRect(x: first.point.x - first.width / 2, y: first.point.y - first.width / 2, width: first.width, height: first.width))
            return
        }
        for index in 1..<samples.count {
            let a = samples[index - 1], b = samples[index]
            context.setLineWidth((a.width + b.width) / 2)
            context.move(to: a.point); context.addLine(to: b.point); context.strokePath()
        }
    }
}

/// Collects strokes and draws them with the brush.
private final class HandwritingPad: UIView {
    var onStrokeBegan: (() -> Void)?
    var onStrokeEnded: (() -> Void)?
    private var ink: [InkStroke] = []
    var traces: [InkTrace] { ink.map(\.trace) }
    var hasInk: Bool { !ink.isEmpty }

    override init(frame: CGRect) {
        super.init(frame: frame)
        layer.cornerRadius = 10; clipsToBounds = true
        layer.borderWidth = 1; layer.borderColor = UIColor.separator.cgColor
        backgroundColor = UIColor.secondarySystemBackground.withAlphaComponent(0.5)
        contentMode = .redraw
        isMultipleTouchEnabled = false
        isAccessibilityElement = true
        accessibilityLabel = L("手写区", "Writing pad")
        accessibilityTraits = .allowsDirectInteraction
        registerForTraitChanges([UITraitUserInterfaceStyle.self]) { (pad: HandwritingPad, _: UITraitCollection) in
            pad.layer.borderColor = UIColor.separator.resolvedColor(with: pad.traitCollection).cgColor; pad.setNeedsDisplay()
        }
    }
    required init?(coder: NSCoder) { fatalError() }

    /// Widths scale with the pad, so a character reads the same on any phone.
    private var widths: (min: CGFloat, max: CGFloat) { let base = min(bounds.width, bounds.height); return (max(2.5, base * 0.012), max(7, base * 0.042)) }

    func clear() { ink = []; setNeedsDisplay() }

    /// Turns to another page of the tape: the current ink slides out the way the tape moves
    /// (up going forward, down going back) and the page's own ink is drawn in its place.
    func turn(to traces: [InkTrace], forward: Bool) {
        let leaving = ink
        let widths = self.widths
        ink = traces.map { InkStroke(replaying: $0, minWidth: widths.min, maxWidth: widths.max) }
        setNeedsDisplay()
        slide(leaving, by: forward ? -1 : 1)
        if !traces.isEmpty {
            transform = CGAffineTransform(translationX: 0, y: (forward ? 1 : -1) * bounds.height * 0.25); alpha = 0.4
            UIView.animate(withDuration: 0.22, delay: 0, options: [.curveEaseOut, .allowUserInteraction]) { self.transform = .identity; self.alpha = 1 }
        }
    }
    func removeLastStroke() { if !ink.isEmpty { ink.removeLast(); setNeedsDisplay() } }

    /// The finished character slides up out of the pad, the way the tape moves on, while the
    /// pad underneath is already empty.
    func scrollAway() {
        let strokes = ink
        ink = []; setNeedsDisplay()
        slide(strokes, by: -1)
    }
    private func slide(_ strokes: [InkStroke], by direction: CGFloat) {
        guard !strokes.isEmpty, bounds.width > 0, bounds.height > 0 else { return }
        let color = UIColor.label.resolvedColor(with: traitCollection)
        let image = UIGraphicsImageRenderer(bounds: bounds).image { context in
            let cg = context.cgContext
            cg.setStrokeColor(color.cgColor); cg.setFillColor(color.cgColor); cg.setLineCap(.round); cg.setLineJoin(.round)
            for stroke in strokes { stroke.draw(in: cg) }
        }
        let ghost = UIImageView(image: image)
        ghost.frame = bounds; ghost.isUserInteractionEnabled = false
        addSubview(ghost)
        UIView.animate(withDuration: 0.26, delay: 0, options: [.curveEaseIn, .allowUserInteraction]) {
            ghost.transform = CGAffineTransform(translationX: 0, y: direction * self.bounds.height * 0.9).scaledBy(x: 0.6, y: 0.6)
            ghost.alpha = 0
        } completion: { _ in ghost.removeFromSuperview() }
    }

    override func draw(_ rect: CGRect) {
        guard let context = UIGraphicsGetCurrentContext() else { return }
        let color = UIColor.label.resolvedColor(with: traitCollection)
        context.setStrokeColor(color.cgColor); context.setFillColor(color.cgColor)
        context.setLineCap(.round); context.setLineJoin(.round)
        for stroke in ink { stroke.draw(in: context) }
    }

    private func force(_ touch: UITouch) -> CGFloat? {
        guard touch.type == .pencil, touch.maximumPossibleForce > 0 else { return nil }
        return touch.force / touch.maximumPossibleForce * 1.6
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = touches.first else { return }
        onStrokeBegan?()
        let widths = self.widths
        ink.append(InkStroke(at: touch.location(in: self), time: touch.timestamp, force: force(touch), minWidth: widths.min, maxWidth: widths.max))
        setNeedsDisplay()
    }
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let touch = touches.first, !ink.isEmpty else { return }
        var changed = false
        for sample in event?.coalescedTouches(for: touch) ?? [touch] {
            changed = ink[ink.count - 1].add(sample.location(in: self), time: sample.timestamp, force: force(sample)) || changed
        }
        if changed { setNeedsDisplay() }
    }
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard !ink.isEmpty else { return }
        ink[ink.count - 1].finish(); setNeedsDisplay()
        onStrokeEnded?()
    }
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) { touchesEnded(touches, with: event) }
}
