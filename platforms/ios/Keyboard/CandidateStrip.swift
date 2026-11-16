import UIKit
#if KEYBOARD_LAYOUT_TESTS
@testable import RIMES
#endif
import RimesCore

struct CandidateLayout {
    let frames: [CGRect]
    let contentSize: CGSize
    let height: CGFloat
    static func rowHeight(landscape: Bool) -> CGFloat { ceil(UIFont.systemFont(ofSize: landscape ? 18 : 20).lineHeight) + 6 }
    static func commentFont(landscape: Bool) -> UIFont { .systemFont(ofSize: landscape ? 12 : 13) }
    /// Neighbouring candidates touch: the space between words is padding inside each
    /// hit area, so a tap between two words always lands on one of them.
    static let gap: CGFloat = 0
    static let padding: CGFloat = 16
    /// Width of the expand/collapse control. Collapsed it sits beside the words; expanded it
    /// overlays the end of the first row only, so the other rows keep the full width.
    static let controlWidth: CGFloat = 52
    static func measure(_ texts: [String], comments: [String] = [], width: CGFloat, expanded: Bool, landscape: Bool, firstRowReserve: CGFloat = 0) -> CandidateLayout {
        let font = UIFont.systemFont(ofSize: landscape ? 18 : 20)
        let rowHeight = rowHeight(landscape: landscape)
        let available = max(1, width)
        var row = 0, x: CGFloat = 0, frames: [CGRect] = []
        for (index, text) in texts.enumerated() {
            let comment = comments.indices.contains(index) ? comments[index] : ""
            let annotationWidth = comment.isEmpty ? 0 : 4 + (comment as NSString).size(withAttributes: [.font: commentFont(landscape: landscape)]).width
            let natural = max(36, ceil((text as NSString).size(withAttributes: [.font: font]).width + annotationWidth) + padding)
            if expanded && x > 0 && x + natural > available - (row == 0 ? firstRowReserve : 0) {
                row += 1; x = 0
            }
            let rowWidth = available - (row == 0 ? firstRowReserve : 0)
            let w = expanded ? min(max(1, rowWidth), natural) : natural
            frames.append(CGRect(x: x, y: CGFloat(row) * (rowHeight + gap), width: w, height: rowHeight)); x += w + gap
        }
        let contentHeight = texts.isEmpty ? 0 : CGFloat(row) * (rowHeight + gap) + rowHeight
        let contentWidth = expanded ? available : max(0, x - gap)
        return CandidateLayout(frames: frames, contentSize: CGSize(width: contentWidth, height: contentHeight), height: min(contentHeight, expanded ? (landscape ? 2 : 3) * rowHeight + (landscape ? 1 : 2) * gap : rowHeight))
    }
}

/// The expand control is a full key square: its touch area reaches above the row into the keyboard's top margin
/// and down into the gap above the keys, but only over the control, never over the words.
protocol ExpandReach: UIView {
    var expandReachWidth: CGFloat { get }
}
enum ExpandReachMetrics { static let above: CGFloat = 7, below: CGFloat = 4 }
extension ExpandReach {
    func pointInsideOrInExpandReach(_ point: CGPoint, default base: Bool) -> Bool {
        if base { return true }
        return point.x >= bounds.width - expandReachWidth && point.y >= -ExpandReachMetrics.above && point.y <= bounds.height + ExpandReachMetrics.below
    }
}
/// Holds the candidate strip; see `ExpandReach`.
final class CandidatePanelView: UIView, ExpandReach {
    var expandReachWidth: CGFloat = 0
    override func point(inside point: CGPoint, with event: UIEvent?) -> Bool {
        pointInsideOrInExpandReach(point, default: super.point(inside: point, with: event))
    }
}

private final class CandidateScrollView: UIScrollView {
    override func touchesShouldCancel(in view: UIView) -> Bool { true }
}

/// Holds the candidate buttons. Empty space between and after the words belongs to the scroll
/// view, so a drag that starts there scrolls like one that starts on a word.
private final class CandidateContentView: UIView {
    override func hitTest(_ point: CGPoint, with event: UIEvent?) -> UIView? {
        let hit = super.hitTest(point, with: event)
        return hit === self ? nil : hit
    }
}

final class CandidateStrip: UIView, ExpandReach {
    var expandReachWidth: CGFloat { texts.isEmpty ? 0 : CandidateLayout.controlWidth + 12 }
    override func point(inside point: CGPoint, with event: UIEvent?) -> Bool {
        pointInsideOrInExpandReach(point, default: super.point(inside: point, with: event))
    }
    let scroll: UIScrollView = CandidateScrollView()
    let expandButton = CandidateButton()
    private let content = CandidateContentView()
    private(set) var buttons: [CandidateButton] = []
    private var texts: [String] = []
    private var comments: [String] = []
    private var context = ""
    private var layingOutCandidates = false
    var onSelect: ((Int) -> Void)?
    var onExpand: (() -> Void)?
    var onPress: (() -> Void)?
    var expanded = false { didSet { if expanded != oldValue { setNeedsLayout() } } }
    var landscape = false { didSet { if landscape != oldValue { setNeedsLayout() } } }
    /// The nine-key punctuation strip draws its marks in their Chinese forms.
    var showsChineseMarks = false { didSet { if showsChineseMarks != oldValue { setNeedsLayout() } } }
    /// Controls occupy fixed gutters outside the scrolling candidate content.
    var controlInsets = UIEdgeInsets.zero { didSet { if controlInsets != oldValue { setNeedsLayout() } } }
    override init(frame: CGRect) {
        super.init(frame: frame)
        addSubview(scroll); scroll.addSubview(content); addSubview(expandButton)
        scroll.delaysContentTouches = false
        scroll.showsHorizontalScrollIndicator = false; scroll.showsVerticalScrollIndicator = false; scroll.hideEdgeEffects()
        expandButton.accessibilityIdentifier = "keyboard.candidates.expand"
        // A full key wide plus reach to its left: it sits at the screen edge, where touches are hardest to land.
        expandButton.hitSlop = UIEdgeInsets(top: ExpandReachMetrics.above, left: 12, bottom: ExpandReachMetrics.below, right: 0)
        expandButton.setPreferredSymbolConfiguration(.init(pointSize: 19, weight: .semibold), forImageIn: .normal)
        expandButton.addAction(UIAction { [weak self] _ in self?.onPress?() }, for: .touchDown)
        expandButton.addAction(UIAction { [weak self] _ in self?.onExpand?() }, for: .touchUpInside)
    }
    required init?(coder: NSCoder) { fatalError() }
    // MARK: Space-bar control
    private var wheel: CandidateWheel?
    private var wheelHighlights = true
    private var wheelPages: [[CandidateWheel.Item]] = []
    var isWheelActive: Bool { wheel != nil }
    /// The words of the line the wheel is on.
    var wheelRange: ClosedRange<Int>? {
        guard let wheel, wheelPages.indices.contains(wheel.current), let first = wheelPages[wheel.current].first?.index,
              let last = wheelPages[wheel.current].last?.index else { return nil }
        return first...last
    }
    private func pageItems() -> [[CandidateWheel.Item]] {
        guard !texts.isEmpty else { return [] }
        let layout = CandidateLayout.measure(texts, comments: comments, width: max(1, scroll.bounds.width), expanded: true, landscape: landscape)
        var result: [[CandidateWheel.Item]] = [], line: [CandidateWheel.Item] = []
        for (index, frame) in layout.frames.enumerated() {
            if let last = line.last, last.frame.minY != frame.minY { result.append(line); line = [] }
            line.append((index, texts[index], frame))
        }
        if !line.isEmpty { result.append(line) }
        return result
    }
    /// Shows the candidates as a drum on the line holding `index`; the plain row stays underneath, hidden.
    func beginWheel(at index: Int, highlighted: Bool = true) {
        endWheel(); wheelHighlights = highlighted
        wheelPages = pageItems()
        let page = wheelPages.firstIndex { $0.contains { $0.index == index } } ?? 0
        let view = CandidateWheel(frame: scroll.frame)
        view.tintColor = tintColor
        view.configure(pages: wheelPages, current: page, highlight: highlighted ? index : nil,
                       font: .systemFont(ofSize: landscape ? 17 : 19))
        insertSubview(view, aboveSubview: scroll); wheel = view; scroll.alpha = 0
    }
    /// Rolls the drum one line and scrolls the plain row to match; returns the middle word of the new line.
    @discardableResult func turnWheel(_ delta: Int) -> Int? {
        guard let wheel, wheelPages.indices.contains(wheel.current + delta) else { return nil }
        let line = wheelPages[wheel.current + delta]
        let middle = line[line.count / 2].index
        wheel.roll(by: delta, highlight: wheelHighlights ? middle : nil)
        if buttons.indices.contains(line[0].index) {
            let limit = max(0, scroll.contentSize.width - scroll.bounds.width)
            scroll.setContentOffset(CGPoint(x: min(limit, buttons[line[0].index].frame.minX), y: 0), animated: false)
        }
        return middle
    }
    func endWheel() {
        wheel?.removeFromSuperview(); wheel = nil; wheelPages = []; scroll.alpha = 1
    }
    private(set) var highlightedIndex: Int?
    var count: Int { buttons.count }
    var isAtTop: Bool { scroll.contentOffset.y <= 0.5 }
    /// The last candidate that fits entirely in the visible row or rows.
    var lastVisibleIndex: Int? {
        let visible = scroll.bounds
        return buttons.indices.last { index in
            let frame = buttons[index].frame
            return expanded ? frame.maxY <= visible.maxY + 0.5 && frame.maxX <= visible.maxX + 0.5 && frame.minY >= visible.minY - 0.5
                : frame.maxX <= visible.maxX + 0.5 && frame.minX >= visible.minX - 0.5
        }
    }
    var firstVisibleIndex: Int? {
        let visible = scroll.bounds
        return buttons.indices.first { buttons[$0].frame.minX >= visible.minX - 0.5 && buttons[$0].frame.maxX <= visible.maxX + 0.5 }
    }
    /// The word in the middle of those fully visible, where a Space-bar highlight starts.
    var middleVisibleIndex: Int? {
        guard let first = firstVisibleIndex, let last = lastVisibleIndex, last >= first else { return nil }
        return (first + last) / 2
    }
    /// The lines the matrix would wrap the words into. In the single row each is one page.
    private func pages() -> [Range<Int>] {
        guard !texts.isEmpty else { return [] }
        let layout = CandidateLayout.measure(texts, comments: comments, width: max(1, scroll.bounds.width), expanded: true, landscape: landscape)
        var result: [Range<Int>] = [], start = 0
        for index in layout.frames.indices.dropFirst() where layout.frames[index].minY != layout.frames[index - 1].minY {
            result.append(start..<index); start = index
        }
        result.append(start..<layout.frames.count)
        return result
    }
    /// Shows the next (+1) or previous (-1) line of words in the single row; false past either end.
    @discardableResult func turnPage(_ delta: Int) -> Bool {
        guard !expanded, buttons.count == texts.count else { return false }
        let pages = pages()
        let current = pages.lastIndex { buttons[$0.lowerBound].frame.minX <= scroll.contentOffset.x + 1 } ?? 0
        guard pages.indices.contains(current + delta) else { return false }
        let limit = max(0, scroll.contentSize.width - scroll.bounds.width)
        scroll.setContentOffset(CGPoint(x: min(limit, buttons[pages[current + delta].lowerBound].frame.minX), y: 0), animated: false)
        return true
    }
    func setHighlight(_ index: Int?) {
        if let old = highlightedIndex, buttons.indices.contains(old) { buttons[old].isSweepTarget = false }
        highlightedIndex = index.flatMap { buttons.indices.contains($0) ? $0 : nil }
        wheel?.setHighlight(highlightedIndex)
        guard let index = highlightedIndex else { return }
        buttons[index].isSweepTarget = true
        scroll.scrollRectToVisible(buttons[index].frame.insetBy(dx: -12, dy: 0), animated: false)
    }
    /// The candidate one row above or below `index`, nearest in horizontal position; nil past either end.
    func index(inRowOffset delta: Int, from index: Int) -> Int? {
        guard buttons.indices.contains(index) else { return nil }
        let current = buttons[index].frame, rowHeight = CandidateLayout.rowHeight(landscape: landscape)
        let targetY = current.minY + CGFloat(delta) * rowHeight
        let row = buttons.indices.filter { abs(buttons[$0].frame.minY - targetY) < rowHeight / 2 }
        return row.min { abs(buttons[$0].frame.midX - current.midX) < abs(buttons[$1].frame.midX - current.midX) }
    }
    func scrollRows(_ rows: Int) {
        guard expanded else { return }
        let step = CandidateLayout.rowHeight(landscape: landscape) * CGFloat(rows)
        let limit = max(0, scroll.contentSize.height - scroll.bounds.height)
        scroll.setContentOffset(CGPoint(x: 0, y: min(limit, max(0, scroll.contentOffset.y + step))), animated: false)
    }
    func update(_ values: [String], comments: [String] = [], context: String = "") {
        let sameContext = context == self.context
        if !sameContext { self.context = context }
        guard values != texts || comments != self.comments else { return }
        endWheel(); setHighlight(nil)
        let appending = expanded && sameContext && !texts.isEmpty && values.count >= texts.count
            && Array(values.prefix(texts.count)) == texts
            && texts.indices.allSatisfy { (comments.indices.contains($0) ? comments[$0] : "") == (self.comments.indices.contains($0) ? self.comments[$0] : "") }
        texts = values
        self.comments = comments
        // Keep idle buttons across compositions instead of constructing sixty
        // UIKit controls on every key. A button under a finger is retired so its
        // eventual release cannot select a different word at the reused index.
        while buttons.count > values.count { buttons.removeLast().removeFromSuperview() }
        if !appending { scroll.setContentOffset(.zero, animated: false) }
        for (index, text) in values.enumerated() {
            if index == buttons.count { buttons.append(makeButton(at: index)) }
            else if buttons[index].isTracking || buttons[index].isHighlighted {
                buttons[index].removeFromSuperview(); buttons[index] = makeButton(at: index)
            }
            let button = buttons[index]
            if button.currentTitle != text { button.setTitle(text, for: .normal); button.accessibilityLabel = text }
            button.comment = comments.indices.contains(index) ? comments[index] : ""
        }
        setNeedsLayout()
    }
    private func makeButton(at index: Int) -> CandidateButton {
        let button = CandidateButton()
        button.accessibilityIdentifier = "keyboard.candidate.\(index)"
        // No feedback on touch-down: swiping the row to scroll must stay silent. The
        // controller gives one soft tick when a word is actually chosen.
        button.addAction(UIAction { [weak self, weak button] _ in
            guard let self, self.buttons.indices.contains(index),
                  self.buttons[index] === button else { return }
            self.onSelect?(index)
        }, for: .touchUpInside)
        content.addSubview(button)
        return button
    }
    private var reservedInsets: UIEdgeInsets {
        // Expanded rows use the full width; only the first row steps aside for the control.
        .init(top: 0, left: controlInsets.left, bottom: 0, right: controlInsets.right + (texts.isEmpty || expanded ? 0 : CandidateLayout.controlWidth))
    }
    func fittingHeight(width: CGFloat) -> CGFloat {
        // A collapsed strip always has one row; measuring every candidate here
        // repeats text shaping during each host sizing/layout callback.
        guard expanded else { return texts.isEmpty ? 0 : CandidateLayout.rowHeight(landscape: landscape) }
        return CandidateLayout.measure(texts, comments: comments, width: max(1, width - reservedInsets.left - reservedInsets.right), expanded: true, landscape: landscape, firstRowReserve: texts.isEmpty ? 0 : CandidateLayout.controlWidth).height
    }
    override func layoutSubviews() {
        super.layoutSubviews()
        layoutCandidates()
    }
    private func layoutCandidates() {
        guard !layingOutCandidates else { return }
        layingOutCandidates = true
        defer { layingOutCandidates = false }
        let insets = reservedInsets
        let width = max(1, bounds.width - insets.left - insets.right)
        scroll.frame = CGRect(x: insets.left, y: 0, width: width, height: bounds.height)
        // Layout depends only on the candidate list and available width, never on
        // scroll position. Every row has the same width, including under controls.
        let layout = CandidateLayout.measure(texts, comments: comments, width: width,
            expanded: expanded, landscape: landscape, firstRowReserve: expanded && !texts.isEmpty ? CandidateLayout.controlWidth : 0)
        var offset = scroll.contentOffset
        offset.x = expanded ? 0 : min(max(0, offset.x), max(0, layout.contentSize.width - width))
        offset.y = expanded ? min(max(0, offset.y), max(0, layout.contentSize.height - bounds.height)) : 0
        content.frame = CGRect(origin: .zero, size: layout.contentSize)
        scroll.contentSize = layout.contentSize
        let rowHeight = layout.frames.first?.height ?? 32
        expandButton.isHidden = texts.isEmpty
        expandButton.frame = CGRect(x: bounds.width - controlInsets.right - CandidateLayout.controlWidth, y: 0, width: CandidateLayout.controlWidth, height: rowHeight)
        expandButton.symbol(expanded ? "chevron.down" : "chevron.up", label: expanded ? L("收起候选", "Collapse candidates") : L("展开候选", "Expand candidates"))
        for (button, frame) in zip(buttons, layout.frames) {
            button.titleLabel?.font = showsChineseMarks ? .chineseMarks(ofSize: landscape ? 18 : 20) : .systemFont(ofSize: landscape ? 18 : 20)
            button.commentLabel.font = CandidateLayout.commentFont(landscape: landscape)
            button.frame = frame
        }
        if offset != scroll.contentOffset { scroll.contentOffset = offset }
    }
}

/// Candidates read as text, with a transient touch highlight and no keycap base.
/// A drum of candidate lines: the line in focus sits in the middle and the lines before and after show as
/// tilted slivers at the upper and lower edges, so turning a line feels like rolling a watch crown.
final class CandidateWheel: UIView {
    typealias Item = (index: Int, text: String, frame: CGRect)
    private var rows: [UIView] = []
    private let pill = UIView()
    private(set) var current = 0
    private var pages: [[Item]] = []
    private var font = UIFont.systemFont(ofSize: 19)
    /// How much of the neighbouring lines shows above and below.
    static let peek: CGFloat = 6
    override init(frame: CGRect) {
        super.init(frame: frame)
        clipsToBounds = true; isUserInteractionEnabled = false; backgroundColor = .clear
        var perspective = CATransform3DIdentity; perspective.m34 = -1 / 260
        layer.sublayerTransform = perspective
        pill.layer.cornerRadius = 7; pill.layer.cornerCurve = .continuous
        pill.backgroundColor = (tintColor ?? .systemBlue).withAlphaComponent(0.3)
    }
    required init?(coder: NSCoder) { fatalError() }
    override func tintColorDidChange() { super.tintColorDidChange(); pill.backgroundColor = (tintColor ?? .systemBlue).withAlphaComponent(0.3) }
    private var rowHeight: CGFloat { max(16, bounds.height - 2 * Self.peek) }
    /// Rotation about the edge nearest the focused line, tilting the neighbour away like the side of a drum.
    private func tilt(_ angle: CGFloat, pivot: CGFloat) -> CATransform3D {
        var t = CATransform3DIdentity
        t = CATransform3DTranslate(t, 0, pivot, 0); t = CATransform3DRotate(t, angle, 1, 0, 0); t = CATransform3DTranslate(t, 0, -pivot, 0)
        return t
    }
    private func makeRow(page: Int, slot: Int, highlight: Int?) -> UIView {
        let height = rowHeight
        let y = slot < 0 ? Self.peek - height : slot > 0 ? bounds.height - Self.peek : Self.peek
        let row = UIView(frame: CGRect(x: 0, y: y, width: bounds.width, height: height))
        row.isUserInteractionEnabled = false
        guard pages.indices.contains(page) else { return row }
        for item in pages[page] {
            let label = UILabel(frame: CGRect(x: item.frame.minX, y: 0, width: item.frame.width, height: height))
            label.text = item.text; label.font = font; label.textAlignment = .center; label.textColor = .label
            label.lineBreakMode = .byTruncatingTail
            if slot == 0, item.index == highlight {
                pill.frame = CGRect(x: item.frame.minX + 2, y: 1, width: item.frame.width - 4, height: height - 2)
                row.addSubview(pill)
            }
            row.addSubview(label)
        }
        if slot != 0 {
            row.alpha = 0.22
            row.layer.transform = tilt(slot < 0 ? 0.95 : -0.95, pivot: slot < 0 ? height / 2 : -height / 2)
        }
        return row
    }
    private func build(highlight: Int?) {
        rows.forEach { $0.removeFromSuperview() }
        rows = [-1, 0, 1].map { makeRow(page: current + $0, slot: $0, highlight: highlight) }
        rows.forEach(addSubview)
    }
    func configure(pages: [[Item]], current: Int, highlight: Int?, font: UIFont) {
        self.pages = pages; self.current = min(max(0, current), max(0, pages.count - 1)); self.font = font
        build(highlight: highlight)
    }
    func setHighlight(_ index: Int?) {
        guard pages.indices.contains(current) else { return }
        let height = rowHeight
        if let index, let item = pages[current].first(where: { $0.index == index }), rows.indices.contains(1) {
            pill.frame = CGRect(x: item.frame.minX + 2, y: 1, width: item.frame.width - 4, height: height - 2)
            if pill.superview !== rows[1] { rows[1].insertSubview(pill, at: 0) }
            pill.isHidden = false
        } else { pill.isHidden = true }
    }
    /// Rolls to the previous (-1) or next (+1) line.
    func roll(by delta: Int, highlight: Int?) {
        guard pages.indices.contains(current + delta) else { return }
        current += delta
        build(highlight: highlight)
        // New rows start where the old ones were and roll into place.
        let pitch = rowHeight - Self.peek
        for row in rows {
            let final = row.layer.transform, alpha = row.alpha
            row.transform = row.transform.concatenating(CGAffineTransform(translationX: 0, y: CGFloat(delta) * pitch))
            row.alpha = alpha * 0.4
            UIView.animate(withDuration: 0.18, delay: 0, options: [.curveEaseOut, .beginFromCurrentState]) {
                row.layer.transform = final; row.alpha = alpha
            }
        }
    }
}

final class CandidateButton: UIButton {
    let commentLabel = UILabel()
    private let highlight = UIView()
    /// Extra reach beyond the visible button, so a thumb that lands near it still counts.
    var hitSlop = UIEdgeInsets.zero
    override func point(inside point: CGPoint, with event: UIEvent?) -> Bool {
        bounds.inset(by: UIEdgeInsets(top: -hitSlop.top, left: -hitSlop.left, bottom: -hitSlop.bottom, right: -hitSlop.right)).contains(point)
    }
    /// Chosen by the Space-bar sweep, not touched: shows in the accent colour.
    var isSweepTarget = false { didSet { if oldValue != isSweepTarget { updateHighlight() } } }
    var comment = "" {
        didSet {
            guard comment != oldValue else { return }
            commentLabel.text = comment; commentLabel.isHidden = comment.isEmpty
            accessibilityValue = comment.isEmpty ? nil : comment
            setNeedsLayout()
        }
    }
    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .clear
        highlight.isUserInteractionEnabled = false; highlight.layer.cornerRadius = 7; highlight.layer.cornerCurve = .continuous
        highlight.alpha = 0; insertSubview(highlight, at: 0)
        commentLabel.font = CandidateLayout.commentFont(landscape: false)
        commentLabel.lineBreakMode = .byTruncatingTail; commentLabel.isHidden = true
        commentLabel.isUserInteractionEnabled = false; addSubview(commentLabel)
        titleLabel?.textAlignment = .center; titleLabel?.adjustsFontSizeToFitWidth = false
        titleLabel?.lineBreakMode = .byTruncatingTail
        setTitleColor(.label, for: .normal)
        setPreferredSymbolConfiguration(.init(pointSize: 17, weight: .medium), forImageIn: .normal)
        updateHighlight()
    }
    required init?(coder: NSCoder) { fatalError() }
    override var isHighlighted: Bool {
        didSet { if oldValue != isHighlighted { updateHighlight() } }
    }
    override func tintColorDidChange() { super.tintColorDidChange(); updateHighlight() }
    private func updateHighlight() {
        // A touch only greys the word; the blue flood was jarring while scrolling or on a mis-tap.
        highlight.backgroundColor = isSweepTarget ? (tintColor ?? .systemBlue).withAlphaComponent(0.3) : .tertiarySystemFill
        highlight.alpha = isHighlighted || isSweepTarget ? 1 : 0
        setTitleColor(.label, for: .normal); setTitleColor(.label, for: .highlighted)
        imageView?.tintColor = .label
        commentLabel.textColor = .secondaryLabel
    }
    func symbol(_ name: String, label: String) {
        setImage(UIImage(systemName: name), for: .normal); accessibilityLabel = label
    }
    override func layoutSubviews() {
        super.layoutSubviews()
        highlight.frame = bounds.insetBy(dx: 2, dy: 1)
        titleLabel?.transform = .identity
        let textHeight = min(bounds.height, ceil(titleLabel?.font.lineHeight ?? 0))
        titleLabel?.bounds = CGRect(x: 0, y: 0, width: max(0, bounds.width - 12), height: textHeight)
        titleLabel?.center = CGPoint(x: bounds.midX, y: bounds.midY)
        if !comment.isEmpty {
            let available = max(0, bounds.width - 12)
            let titleWidth = min(available, ceil(titleLabel?.sizeThatFits(.init(width: .greatestFiniteMagnitude, height: bounds.height)).width ?? 0))
            let commentWidth = min(max(0, available - titleWidth - 4), ceil(commentLabel.intrinsicContentSize.width))
            let left = (bounds.width - titleWidth - 4 - commentWidth) / 2
            titleLabel?.frame = CGRect(x: left, y: (bounds.height - textHeight) / 2, width: titleWidth, height: textHeight)
            commentLabel.frame = CGRect(x: left + titleWidth + 4, y: 0, width: commentWidth, height: bounds.height)
        }
        if let imageView, let image = imageView.image {
            imageView.frame = CGRect(x: (bounds.width - image.size.width) / 2, y: (bounds.height - image.size.height) / 2, width: image.size.width, height: image.size.height)
        }
    }
}

/// Temporarily replaces the candidate row while a chord is held. Mirrored about
/// the centre: left keys, left mapping, [combined result], right mapping, right keys.
final class ChordHandPreviewView: UIView {
    var theme: StatusSkin = .apple { didSet { if oldValue != theme { update(preview) } } }
    private let leftKeys = UILabel(), leftOutput = UILabel(), combined = UILabel(), rightOutput = UILabel(), rightKeys = UILabel()
    private let pill = UIView()
    private var columns: [UILabel] { [leftKeys, leftOutput, combined, rightOutput, rightKeys] }
    private(set) var preview: ChordHandPreview?
    var landscape = false { didSet { if oldValue != landscape { update(preview) } } }
    override init(frame: CGRect) {
        super.init(frame: frame)
        isUserInteractionEnabled = false; isHidden = true
        pill.layer.cornerRadius = 8; pill.layer.cornerCurve = .continuous; addSubview(pill)
        for (label, id) in zip(columns, ["left.keys", "left.output", "combined", "right.output", "right.keys"]) {
            label.textAlignment = .center; label.adjustsFontSizeToFitWidth = true; label.minimumScaleFactor = 0.5
            label.accessibilityIdentifier = "keyboard.chord.\(id)"; addSubview(label)
        }
    }
    required init?(coder: NSCoder) { fatalError() }
    override func layoutSubviews() {
        super.layoutSubviews()
        let h = bounds.height, mid = bounds.midX
        func natural(_ label: UILabel) -> CGFloat { (label.text?.isEmpty ?? true) ? 0 : ceil(label.intrinsicContentSize.width) }
        // Result pill stays centred; each side packs outward from it.
        let pillWidth = min(bounds.width * 0.42, max(58, natural(combined) + 20))
        pill.frame = CGRect(x: mid - pillWidth / 2, y: 2, width: pillWidth, height: max(0, h - 4))
        combined.frame = pill.frame.insetBy(dx: 6, dy: 0)
        let room = max(0, (bounds.width - pillWidth) / 2 - 4)
        for (output, keys, sign) in [(leftOutput, leftKeys, CGFloat(-1)), (rightOutput, rightKeys, CGFloat(1))] {
            let outputWidth = min(natural(output), room * 0.55), keysWidth = min(natural(keys), max(0, room - outputWidth - 14))
            let outputCentre = mid + sign * (pillWidth / 2 + 8 + outputWidth / 2)
            output.frame = CGRect(x: outputCentre - outputWidth / 2, y: 0, width: outputWidth, height: h)
            let keysCentre = outputCentre + sign * (outputWidth / 2 + 6 + keysWidth / 2)
            keys.frame = CGRect(x: keysCentre - keysWidth / 2, y: 0, width: keysWidth, height: h)
        }
    }
    /// Column texts left to right, for tests and accessibility.
    var texts: [String] { columns.map { $0.text ?? "" } }
    func update(_ value: ChordHandPreview?) {
        preview = value
        let keyFont = UIFont.monospacedSystemFont(ofSize: landscape ? 12 : 13, weight: .medium)
        let outputFont = UIFont.systemFont(ofSize: landscape ? 15 : 17, weight: .semibold)
        func side(_ side: ChordHandPreview.Side?, keys: UILabel, output: UILabel) {
            keys.font = keyFont; keys.textColor = .secondaryLabel; keys.text = side?.keys.uppercased() ?? ""
            output.font = outputFont
            output.text = side == nil ? "—" : side?.output ?? "?"
            output.textColor = side == nil ? .tertiaryLabel : side?.output == nil ? .systemRed : theme.palette.accentText
        }
        side(value?.left, keys: leftKeys, output: leftOutput)
        side(value?.right, keys: rightKeys, output: rightOutput)
        let mapped = value?.combined != nil
        combined.font = mapped ? .systemFont(ofSize: landscape ? 20 : 22, weight: .bold) : .systemFont(ofSize: 13, weight: .semibold)
        combined.textColor = mapped ? theme.palette.accentInk : .secondaryLabel
        combined.text = value == nil ? "" : value?.combined ?? L("无映射", "No mapping")
        pill.backgroundColor = mapped ? theme.palette.accent : .tertiarySystemFill
        pill.isHidden = value == nil
        accessibilityLabel = value == nil ? nil : texts.filter { !$0.isEmpty }.joined(separator: " ")
        setNeedsLayout()
    }
}

/// Fills the empty candidate row: one button per plugin, each two squares wide.
final class PluginShortcutBar: UIView {
    private let scroll = UIScrollView()
    private(set) var buttons: [(plugin: KeyboardPlugin, button: UIButton)] = []
    /// Plugins left off the quick entrance, behind a trailing key of the same size.
    private let overflow = ShortcutCapButton()
    var overflowMenu: UIMenu? { didSet { overflow.menu = overflowMenu; overflow.isHidden = overflowMenu == nil; setNeedsLayout() } }
    var overflowSelected = false { didSet { overflow.isSelected = overflowSelected } }
    var onSelect: ((KeyboardPlugin) -> Void)?
    /// Holding a shortcut opens that plugin's settings.
    var onSettings: ((KeyboardPlugin) -> Void)?
    var onPress: (() -> Void)?
    /// The plugin open in the Buffer, highlighted; nil for Default or Buffer off.
    var selected: KeyboardPlugin? { didSet { if selected != oldValue { refresh() } } }
    /// Shortcuts are keys: they follow the keyboard skin like every other cap.
    var theme: StatusSkin = .rhino { didSet { if oldValue != theme { allCaps.forEach { $0.theme = theme } } } }
    var legacyKey = false { didSet { if oldValue != legacyKey { allCaps.forEach { $0.legacyKey = legacyKey } } } }
    private var allCaps: [ShortcutCapButton] { buttons.compactMap { $0.button as? ShortcutCapButton } + [overflow] }
    override init(frame: CGRect) {
        super.init(frame: frame)
        scroll.showsHorizontalScrollIndicator = false; addSubview(scroll); scroll.hideEdgeEffects()
        overflow.setImage(UIImage(systemName: "ellipsis"), for: .normal)
        overflow.accessibilityLabel = L("更多插件", "More plugins"); overflow.accessibilityIdentifier = "keyboard.shortcut.more"
        overflow.showsMenuAsPrimaryAction = true; overflow.accentCap = true; overflow.isHidden = true
        overflow.addAction(UIAction { [weak self] _ in self?.onPress?() }, for: .touchDown)
        scroll.addSubview(overflow)
        setPlugins(KeyboardPlugin.builtIn)
    }
    /// Rebuilds the bar when the user's entrance list changes; unchanged lists keep their buttons.
    func setPlugins(_ plugins: [KeyboardPlugin]) {
        func signature(_ list: [KeyboardPlugin]) -> [String] { list.map { [$0.rawValue, $0.title, $0.shortTitle, $0.symbol].joined(separator: "|") } }
        guard buttons.isEmpty || signature(plugins) != signature(buttons.map(\.plugin)) else { return }
        buttons.forEach { $0.button.removeFromSuperview() }; buttons = []
        for plugin in plugins {
            let button = ShortcutCapButton()
            button.theme = theme; button.legacyKey = legacyKey; button.accentCap = true
            button.setImage(UIImage(systemName: plugin.symbol), for: .normal)
            button.setTitle(plugin.shortTitle, for: .normal)
            button.accessibilityIdentifier = "keyboard.shortcut.\(plugin.rawValue)"
            button.accessibilityLabel = plugin.title
            button.addAction(UIAction { [weak self] _ in self?.onPress?() }, for: .touchDown)
            button.addAction(UIAction { [weak self] _ in self?.onSelect?(plugin) }, for: .touchUpInside)
            let hold = UILongPressGestureRecognizer(target: self, action: #selector(hold(_:))); hold.minimumPressDuration = 0.45
            button.addGestureRecognizer(hold)
            scroll.addSubview(button); buttons.append((plugin, button))
        }
        refresh(); setNeedsLayout()
    }
    required init?(coder: NSCoder) { fatalError() }
    @objc private func hold(_ recognizer: UILongPressGestureRecognizer) {
        guard recognizer.state == .began, let plugin = buttons.first(where: { $0.button === recognizer.view })?.plugin else { return }
        onSettings?(plugin)
    }
    private func refresh() {
        for (plugin, button) in buttons {
            let on = plugin == selected
            button.isSelected = on
            button.accessibilityTraits = on ? [.button, .selected] : [.button]
        }
    }
    override func layoutSubviews() {
        super.layoutSubviews()
        scroll.frame = bounds
        // Full-height caps on the row's left edge, 4 pt apart like the side keys.
        let height = bounds.height, gap: CGFloat = 4
        var x: CGFloat = 0
        for cap in buttons.compactMap({ $0.button as? ShortcutCapButton }) + (overflow.isHidden ? [] : [overflow]) {
            let width = max(height * 1.6, cap.fittingWidth)
            cap.frame = CGRect(x: x, y: 0, width: width, height: height); x += width + gap
        }
        scroll.contentSize = CGSize(width: max(bounds.width, x - gap), height: bounds.height)
    }
}

/// A shortcut drawn as a key: the plugin's glyph and short name side by side on one cap.
final class ShortcutCapButton: KeycapButton {
    override init(frame: CGRect) {
        super.init(frame: frame)
        titleLabel?.font = .systemFont(ofSize: 13, weight: .medium)
        setPreferredSymbolConfiguration(.init(pointSize: 13, weight: .medium), forImageIn: .normal)
    }
    required init?(coder: NSCoder) { fatalError() }
    private var imageSize: CGSize { currentImage == nil ? .zero : (imageView?.intrinsicContentSize ?? .zero) }
    private var textWidth: CGFloat { ceil(titleLabel?.intrinsicContentSize.width ?? 0) }
    private var spacing: CGFloat { imageSize.width > 0 && textWidth > 0 ? 4 : 0 }
    /// Glyph, title and 10 pt either side.
    var fittingWidth: CGFloat { ceil(imageSize.width + spacing + textWidth) + 20 }
    override func layoutSubviews() {
        super.layoutSubviews()
        let cap = KeycapStyle.capRect(in: bounds, pressed: isHighlighted)
        let image = imageSize, total = image.width + spacing + textWidth
        var x = max(cap.minX + 6, cap.midX - total / 2)
        imageView?.frame = CGRect(x: x, y: cap.midY - image.height / 2, width: image.width, height: image.height)
        x += image.width + spacing
        let textHeight = ceil(titleLabel?.font.lineHeight ?? 0)
        titleLabel?.frame = CGRect(x: x, y: cap.midY - textHeight / 2, width: max(0, min(textWidth, cap.maxX - 6 - x)), height: textHeight)
    }
}

/// One choice in the Buffer settings panel.
struct PanelItem: Equatable {
    enum Role: Equatable { case choice, action, destructive }
    var id: String
    var title: String
    var symbol: String? = nil
    var selected = false
    var role = Role.choice
    var enabled = true
}
struct PanelSection {
    var title: String
    var note: String?
    var items: [PanelItem]
    /// A row the controller owns (and keeps across renders), shown below the note.
    var custom: UIView?
    var customHeight: CGFloat = 0
    /// Changes whenever the custom row's content changes, so the panel knows to relayout.
    var customKey = ""
    var action: (String) -> Void
    init(title: String, note: String? = nil, items: [PanelItem] = [], custom: UIView? = nil, customHeight: CGFloat = 0, customKey: String = "", action: @escaping (String) -> Void = { _ in }) {
        self.title = title; self.note = note; self.items = items; self.custom = custom
        self.customHeight = customHeight; self.customKey = customKey; self.action = action
    }
}

/// Chip drawn by RIMES rather than a system control, so the panel reads as part of the keyboard.
final class PanelChip: UIControl {
    let label = UILabel()
    let icon = UIImageView()
    var item: PanelItem { didSet { apply() } }
    init(_ item: PanelItem) {
        self.item = item
        super.init(frame: .zero)
        layer.cornerRadius = 9; layer.cornerCurve = .continuous
        label.textAlignment = .center; label.isUserInteractionEnabled = false
        icon.contentMode = .scaleAspectFit; icon.isUserInteractionEnabled = false
        addSubview(icon); addSubview(label); isAccessibilityElement = true
        registerForTraitChanges([UITraitUserInterfaceStyle.self]) { (chip: PanelChip, _: UITraitCollection) in chip.apply() }
        apply()
    }
    required init?(coder: NSCoder) { fatalError() }
    override func tintColorDidChange() { super.tintColorDidChange(); apply() }
    static let height: CGFloat = 32
    static let font = UIFont.systemFont(ofSize: 14, weight: .medium)
    func fittingWidth() -> CGFloat { ceil((item.title as NSString).size(withAttributes: [.font: Self.font]).width) + 24 + (item.symbol == nil ? 0 : 22) }
    override var isHighlighted: Bool { didSet { alpha = isHighlighted ? 0.55 : item.enabled ? 1 : 0.35 } }
    private func apply() {
        label.text = item.title; label.font = Self.font
        icon.image = item.symbol.flatMap { UIImage(systemName: $0) }; icon.isHidden = item.symbol == nil
        let (fill, text, border): (UIColor, UIColor, UIColor?) = switch item.role {
        case .choice: item.selected ? (tintColor ?? .systemBlue, KeyboardPalette.contrastingInk(on: tintColor ?? .systemBlue), nil) : (.secondarySystemGroupedBackground, .label, .separator)
        case .action: ((tintColor ?? .systemBlue).withAlphaComponent(0.14), .label, nil)
        case .destructive: (UIColor.systemRed.withAlphaComponent(0.12), .systemRed, nil)
        }
        backgroundColor = fill; label.textColor = text; icon.tintColor = text
        layer.borderWidth = border == nil ? 0 : 0.7
        layer.borderColor = border?.resolvedColor(with: traitCollection).cgColor
        isEnabled = item.enabled; alpha = item.enabled ? 1 : 0.35
        accessibilityLabel = item.title
        accessibilityTraits = item.selected ? [.button, .selected] : item.enabled ? .button : [.button, .notEnabled]
    }
    override func layoutSubviews() {
        super.layoutSubviews()
        icon.frame = CGRect(x: 12, y: (bounds.height - 16) / 2, width: 16, height: 16)
        label.frame = item.symbol == nil ? bounds.insetBy(dx: 8, dy: 0) : CGRect(x: 34, y: 0, width: max(0, bounds.width - 46), height: bounds.height)
    }
}

/// Buffer and plugin settings, drawn over the keys: a header, then titled rows of
/// wrapping chips. Rebuilt only when its content changes, keeping the scroll position.
final class KeyboardPanel: UIView {
    var onClose: (() -> Void)?
    var onPress: (() -> Void)?
    private let titleLabel = UILabel(), closeButton = UIButton(type: .custom), scroll = UIScrollView()
    private var rows: [(title: UILabel, note: UILabel?, custom: UIView?, customHeight: CGFloat, chips: [PanelChip])] = []
    private var sections: [PanelSection] = []
    private var signature = ""
    private(set) var chips: [PanelChip] = []
    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .systemGroupedBackground
        titleLabel.font = .systemFont(ofSize: 16, weight: .semibold); titleLabel.textColor = .label
        addSubview(titleLabel); addSubview(scroll)
        closeButton.setImage(UIImage(systemName: "xmark", withConfiguration: UIImage.SymbolConfiguration(pointSize: 12, weight: .bold)), for: .normal)
        closeButton.tintColor = .secondaryLabel; closeButton.backgroundColor = .tertiarySystemFill
        closeButton.layer.cornerRadius = 14; closeButton.accessibilityLabel = L("关闭设置", "Close settings")
        closeButton.accessibilityIdentifier = "keyboard.panel.close"
        closeButton.addAction(UIAction { [weak self] _ in self?.onPress?() }, for: .touchDown)
        closeButton.addAction(UIAction { [weak self] _ in self?.onClose?() }, for: .touchUpInside)
        addSubview(closeButton)
        scroll.showsVerticalScrollIndicator = false; scroll.alwaysBounceVertical = true; scroll.hideEdgeEffects()
        accessibilityIdentifier = "keyboard.panel"
    }
    required init?(coder: NSCoder) { fatalError() }
    func show(title: String, sections: [PanelSection]) {
        titleLabel.text = title
        self.sections = sections
        let next = sections.map { "\($0.title)|\($0.note ?? "")|\($0.customKey)|" + $0.items.map { "\($0.id)\($0.title)\($0.symbol ?? "")\($0.selected)\($0.enabled)" }.joined(separator: ",") }.joined(separator: ";")
        guard next != signature else { return }
        signature = next
        let offset = scroll.contentOffset
        rows.forEach { row in
            row.title.removeFromSuperview(); row.note?.removeFromSuperview(); row.chips.forEach { $0.removeFromSuperview() }
            if let custom = row.custom, !sections.contains(where: { $0.custom === custom }) { custom.removeFromSuperview() }
        }
        rows = []; chips = []
        for (index, section) in sections.enumerated() {
            let heading = UILabel(); heading.text = section.title
            heading.font = .systemFont(ofSize: 12, weight: .semibold); heading.textColor = .secondaryLabel
            scroll.addSubview(heading)
            var note: UILabel?
            if let text = section.note {
                let label = UILabel(); label.text = text; label.numberOfLines = 0
                label.font = .systemFont(ofSize: 13); label.textColor = .secondaryLabel
                scroll.addSubview(label); note = label
            }
            let row = section.items.map { item -> PanelChip in
                let chip = PanelChip(item)
                chip.accessibilityIdentifier = "keyboard.panel.\(index).\(item.id)"
                chip.addAction(UIAction { [weak self] _ in self?.onPress?() }, for: .touchDown)
                chip.addAction(UIAction { [weak self, weak chip] _ in
                    guard let self, let chip, chip.item.enabled, self.sections.indices.contains(index) else { return }
                    self.sections[index].action(chip.item.id)
                }, for: .touchUpInside)
                scroll.addSubview(chip); return chip
            }
            if let custom = section.custom, custom.superview !== scroll { scroll.addSubview(custom) }
            rows.append((heading, note, section.custom, section.customHeight, row)); chips += row
        }
        setNeedsLayout(); layoutIfNeeded()
        scroll.contentOffset = CGPoint(x: 0, y: min(offset.y, max(0, scroll.contentSize.height - scroll.bounds.height)))
    }
    override func layoutSubviews() {
        super.layoutSubviews()
        let inset: CGFloat = 12, width = bounds.width - 2 * inset
        titleLabel.frame = CGRect(x: inset, y: 6, width: max(0, width - 36), height: 28)
        closeButton.frame = CGRect(x: bounds.width - inset - 28, y: 6, width: 28, height: 28)
        scroll.frame = CGRect(x: 0, y: 38, width: bounds.width, height: max(0, bounds.height - 38))
        var y: CGFloat = 4
        for row in rows {
            row.title.frame = CGRect(x: inset, y: y, width: width, height: 18); y += 22
            if let note = row.note {
                let height = ceil(note.sizeThatFits(CGSize(width: width, height: .greatestFiniteMagnitude)).height)
                note.frame = CGRect(x: inset, y: y, width: width, height: height); y += height + 6
            }
            if let custom = row.custom {
                custom.frame = CGRect(x: inset, y: y, width: width, height: row.customHeight); y += row.customHeight + (row.chips.isEmpty ? 0 : 8)
            }
            var x = inset
            for (i, chip) in row.chips.enumerated() {
                let w = min(width, chip.fittingWidth())
                if i > 0 && x + w > inset + width { x = inset; y += PanelChip.height + 8 }
                chip.frame = CGRect(x: x, y: y, width: w, height: PanelChip.height); x += w + 8
            }
            if !row.chips.isEmpty { y += PanelChip.height }
            y += 14
        }
        scroll.contentSize = CGSize(width: bounds.width, height: y)
    }
}

/// Three-row wheel, like a meter counter: the current item sits in a lit band with its
/// neighbours above and below, tilted back in 3D and fading, so the list reads as a
/// drum turning. Swipe to roll; tap the row above or below (or the middle) to step.
final class DrumPicker: UIView, UIScrollViewDelegate {
    var onChange: ((Int) -> Void)?
    /// Fires as each item rolls past the band, for haptics.
    var onTick: (() -> Void)?
    private let scroll = UIScrollView(), band = UIView(), fade = CAGradientLayer()
    private var labels: [UILabel] = []
    private(set) var items: [String] = []
    private(set) var selectedIndex = 0
    private var tickIndex = 0
    private var row: CGFloat { bounds.height / 3 }
    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .secondarySystemGroupedBackground
        layer.cornerRadius = 10; layer.cornerCurve = .continuous; layer.borderWidth = 0.7
        clipsToBounds = true
        band.isUserInteractionEnabled = false; band.layer.cornerRadius = 7; band.layer.cornerCurve = .continuous
        band.layer.borderWidth = 1
        addSubview(band)
        scroll.showsVerticalScrollIndicator = false; scroll.showsHorizontalScrollIndicator = false
        scroll.decelerationRate = .fast; scroll.delegate = self; scroll.alwaysBounceVertical = true; scroll.hideEdgeEffects()
        addSubview(scroll)
        // Top and bottom rows sink into the background, like the ends of a drum.
        fade.startPoint = CGPoint(x: 0.5, y: 0); fade.endPoint = CGPoint(x: 0.5, y: 1)
        fade.locations = [0, 0.3, 0.7, 1]
        layer.addSublayer(fade)
        scroll.addGestureRecognizer(UITapGestureRecognizer(target: self, action: #selector(tapped(_:))))
        isAccessibilityElement = true; accessibilityTraits = .adjustable
        registerForTraitChanges([UITraitUserInterfaceStyle.self]) { (drum: DrumPicker, _: UITraitCollection) in drum.applyColors() }
        applyColors()
    }
    required init?(coder: NSCoder) { fatalError() }
    override func tintColorDidChange() { super.tintColorDidChange(); applyColors() }
    private func applyColors() {
        layer.borderColor = UIColor.separator.resolvedColor(with: traitCollection).cgColor
        band.backgroundColor = (tintColor ?? .systemBlue).withAlphaComponent(0.12)
        band.layer.borderColor = (tintColor ?? .systemBlue).withAlphaComponent(0.5).resolvedColor(with: traitCollection).cgColor
        let base = UIColor.secondarySystemGroupedBackground.resolvedColor(with: traitCollection)
        fade.colors = [base.withAlphaComponent(0.85).cgColor, base.withAlphaComponent(0).cgColor, base.withAlphaComponent(0).cgColor, base.withAlphaComponent(0.85).cgColor]
    }
    /// Sets the list and the current item without animation, unless the user is rolling it.
    func set(items next: [String], selected: Int) {
        if next != items {
            items = next; labels.forEach { $0.removeFromSuperview() }
            labels = next.map { text in
                let label = UILabel(); label.text = text; label.textAlignment = .center
                label.adjustsFontSizeToFitWidth = true; label.minimumScaleFactor = 0.7
                scroll.addSubview(label); return label
            }
            setNeedsLayout()
        }
        guard !scroll.isTracking, !scroll.isDecelerating else { return }
        selectedIndex = min(max(0, selected), max(0, items.count - 1)); tickIndex = selectedIndex
        layoutIfNeeded(); scroll.contentOffset.y = offset(for: selectedIndex); shape()
        accessibilityValue = items.indices.contains(selectedIndex) ? items[selectedIndex] : nil
    }
    private func offset(for index: Int) -> CGFloat { CGFloat(index) * row - scroll.contentInset.top }
    private var centreIndex: Int { Int(((scroll.contentOffset.y + scroll.contentInset.top) / max(1, row)).rounded()) }
    override func layoutSubviews() {
        super.layoutSubviews()
        let h = row
        scroll.frame = bounds
        // One row of inset above and below lets the first and last items reach the band.
        scroll.contentInset = UIEdgeInsets(top: h, left: 0, bottom: h, right: 0)
        for (i, label) in labels.enumerated() {
            label.transform3D = CATransform3DIdentity
            label.frame = CGRect(x: 8, y: CGFloat(i) * h, width: max(0, bounds.width - 16), height: h)
        }
        scroll.contentSize = CGSize(width: bounds.width, height: CGFloat(labels.count) * h)
        band.frame = CGRect(x: 4, y: h, width: bounds.width - 8, height: h)
        fade.frame = bounds
        if !scroll.isTracking && !scroll.isDecelerating { scroll.contentOffset.y = offset(for: selectedIndex) }
        shape()
    }
    /// Tilts, shrinks and dims each row by its distance from the band: a turning drum.
    private func shape() {
        let h = max(1, row), middle = scroll.contentOffset.y + bounds.height / 2
        for label in labels {
            let distance = (label.center.y - middle) / h // 0 in the band, ±1 one row away
            let d = max(-1.6, min(1.6, distance))
            var t = CATransform3DIdentity; t.m34 = -1 / 260
            t = CATransform3DRotate(t, -d * 0.62, 1, 0, 0)
            t = CATransform3DScale(t, 1 - abs(d) * 0.12, 1 - abs(d) * 0.12, 1)
            label.layer.transform = t
            label.alpha = max(0.15, 1 - abs(d) * 0.5)
            let centred = abs(distance) < 0.5
            label.font = .systemFont(ofSize: centred ? 16 : 14, weight: centred ? .semibold : .regular)
            label.textColor = centred ? .label : .secondaryLabel
        }
    }
    func scrollViewDidScroll(_ scrollView: UIScrollView) {
        shape()
        guard scrollView.isTracking || scrollView.isDecelerating else { return }
        let index = centreIndex
        if index != tickIndex, items.indices.contains(index) { tickIndex = index; onTick?() }
    }
    /// Always come to rest with an item in the band.
    func scrollViewWillEndDragging(_ scrollView: UIScrollView, withVelocity velocity: CGPoint, targetContentOffset target: UnsafeMutablePointer<CGPoint>) {
        let h = max(1, row)
        let index = min(max(0, Int(((target.pointee.y + scroll.contentInset.top) / h).rounded())), max(0, items.count - 1))
        target.pointee.y = offset(for: index)
    }
    func scrollViewDidEndDecelerating(_ scrollView: UIScrollView) { settle() }
    func scrollViewDidEndDragging(_ scrollView: UIScrollView, willDecelerate decelerate: Bool) { if !decelerate { settle() } }
    func scrollViewDidEndScrollingAnimation(_ scrollView: UIScrollView) { settle() }
    private func settle() {
        guard !items.isEmpty else { return }
        let index = min(max(0, centreIndex), items.count - 1)
        guard index != selectedIndex else { return }
        selectedIndex = index; accessibilityValue = items[index]; onChange?(index)
    }
    /// Upper row steps back, lower row steps forward, the band steps forward (wrapping).
    @objc private func tapped(_ tap: UITapGestureRecognizer) {
        guard !items.isEmpty else { return }
        let y = tap.location(in: self).y
        if y < row { roll(to: max(0, selectedIndex - 1)) }
        else if y > 2 * row { roll(to: min(items.count - 1, selectedIndex + 1)) }
        else { roll(to: (selectedIndex + 1) % items.count) }
    }
    func roll(to index: Int) {
        guard items.indices.contains(index), index != selectedIndex else { return }
        onTick?(); scroll.setContentOffset(CGPoint(x: 0, y: offset(for: index)), animated: true)
    }
    override func accessibilityIncrement() { roll(to: min(items.count - 1, selectedIndex + 1)) }
    override func accessibilityDecrement() { roll(to: max(0, selectedIndex - 1)) }
}

/// On/off switch using the active theme accent with a sliding knob.
final class PanelToggle: UIControl {
    private let knob = UIView()
    private(set) var isOn = false
    override init(frame: CGRect) {
        super.init(frame: frame)
        layer.cornerCurve = .continuous
        knob.backgroundColor = .white; knob.isUserInteractionEnabled = false
        knob.layer.shadowColor = UIColor.black.cgColor; knob.layer.shadowOpacity = 0.18; knob.layer.shadowRadius = 2; knob.layer.shadowOffset = CGSize(width: 0, height: 1)
        addSubview(knob)
        addAction(UIAction { [weak self] _ in guard let self else { return }; self.setOn(!self.isOn, animated: true); self.sendActions(for: .valueChanged) }, for: .touchUpInside)
        isAccessibilityElement = true; accessibilityTraits = .button
        apply()
    }
    required init?(coder: NSCoder) { fatalError() }
    override func tintColorDidChange() { super.tintColorDidChange(); apply() }
    override var intrinsicContentSize: CGSize { CGSize(width: 48, height: 28) }
    func setOn(_ on: Bool, animated: Bool) {
        guard on != isOn else { return }
        isOn = on
        if animated { UIView.animate(withDuration: 0.2, delay: 0, options: [.curveEaseOut, .allowUserInteraction]) { self.apply(); self.layoutIfNeeded() } } else { apply() }
    }
    private func apply() {
        backgroundColor = isOn ? tintColor : .tertiarySystemFill
        accessibilityValue = isOn ? L("开", "On") : L("关", "Off")
        setNeedsLayout()
    }
    override func layoutSubviews() {
        super.layoutSubviews()
        layer.cornerRadius = bounds.height / 2
        let size = bounds.height - 4
        knob.frame = CGRect(x: isOn ? bounds.width - size - 2 : 2, y: 2, width: size, height: size); knob.layer.cornerRadius = size / 2
    }
}

/// [from ▲▼]  ⇄  [to ▲▼] on one line.
final class LanguagePairRow: UIView {
    let source = DrumPicker(), target = DrumPicker(), swap = UIButton(type: .custom)
    override init(frame: CGRect) {
        super.init(frame: frame)
        swap.setImage(UIImage(systemName: "arrow.left.arrow.right", withConfiguration: UIImage.SymbolConfiguration(pointSize: 14, weight: .semibold)), for: .normal)
        swap.backgroundColor = (tintColor ?? .systemBlue).withAlphaComponent(0.14)
        swap.layer.cornerRadius = 9; swap.layer.cornerCurve = .continuous
        swap.accessibilityLabel = L("交换方向", "Swap direction"); swap.accessibilityIdentifier = "keyboard.panel.swap"
        source.accessibilityLabel = L("原文语言", "From"); target.accessibilityLabel = L("译文语言", "To")
        source.accessibilityIdentifier = "keyboard.panel.source"; target.accessibilityIdentifier = "keyboard.panel.target"
        for view in [source, swap, target] { addSubview(view) }
    }
    required init?(coder: NSCoder) { fatalError() }
    override func tintColorDidChange() {
        super.tintColorDidChange()
        swap.backgroundColor = (tintColor ?? .systemBlue).withAlphaComponent(0.14)
        swap.imageView?.tintColor = .label
    }
    override func layoutSubviews() {
        super.layoutSubviews()
        let gap: CGFloat = 8, swapWidth: CGFloat = 44, drum = max(0, (bounds.width - swapWidth - 2 * gap) / 2)
        source.frame = CGRect(x: 0, y: 0, width: drum, height: bounds.height)
        // The swap button lines up with the rollers' middle band.
        let band = min(40, bounds.height / 3 + 6)
        swap.frame = CGRect(x: drum + gap, y: (bounds.height - band) / 2, width: swapWidth, height: band)
        target.frame = CGRect(x: bounds.width - drum, y: 0, width: drum, height: bounds.height)
    }
}

/// A title with a RIMES toggle on the right.
final class PanelSwitchRow: UIView {
    let label = UILabel(), toggle = PanelToggle()
    override init(frame: CGRect) {
        super.init(frame: frame)
        label.font = .systemFont(ofSize: 15, weight: .medium); label.textColor = .label
        label.adjustsFontSizeToFitWidth = true; label.minimumScaleFactor = 0.8
        addSubview(label); addSubview(toggle)
    }
    required init?(coder: NSCoder) { fatalError() }
    override func layoutSubviews() {
        super.layoutSubviews()
        let size = toggle.intrinsicContentSize
        toggle.frame = CGRect(x: bounds.width - size.width, y: (bounds.height - size.height) / 2, width: size.width, height: size.height)
        label.frame = CGRect(x: 0, y: 0, width: max(0, toggle.frame.minX - 12), height: bounds.height)
        toggle.accessibilityLabel = label.text
    }
}

/// A passing prompt. It floats over the keys for a few seconds and then fades, so it never takes a row of its
/// own or moves the keyboard. Setting `text` shows it; an empty text hides it at once.
final class PromptLabel: UILabel {
    private var hideWork: DispatchWorkItem?
    private let padding = UIEdgeInsets(top: 6, left: 12, bottom: 6, right: 12)
    override init(frame: CGRect) {
        super.init(frame: frame)
        isUserInteractionEnabled = false; alpha = 0; isHidden = true
        layer.cornerRadius = 12; layer.cornerCurve = .continuous; layer.masksToBounds = true
        backgroundColor = UIColor.secondarySystemBackground.withAlphaComponent(0.96)
        textAlignment = .center; textColor = .label
        registerForTraitChanges([UITraitUserInterfaceStyle.self]) { (label: PromptLabel, _: UITraitCollection) in
            label.backgroundColor = UIColor.secondarySystemBackground.withAlphaComponent(0.96)
        }
    }
    required init?(coder: NSCoder) { fatalError() }
    override var text: String? {
        didSet {
            hideWork?.cancel()
            guard let text, !text.isEmpty else { alpha = 0; isHidden = true; return }
            isHidden = false
            UIView.animate(withDuration: 0.15) { self.alpha = 1 }
            // Long messages stay longer; the full text is also announced to VoiceOver.
            let work = DispatchWorkItem { [weak self] in
                UIView.animate(withDuration: 0.3, animations: { self?.alpha = 0 }, completion: { _ in
                    if self?.alpha == 0 { self?.isHidden = true }
                })
            }
            hideWork = work
            DispatchQueue.main.asyncAfter(deadline: .now() + max(2.4, Double(text.count) * 0.09), execute: work)
            UIAccessibility.post(notification: .announcement, argument: text)
        }
    }
    override func drawText(in rect: CGRect) { super.drawText(in: rect.inset(by: padding)) }
    override func sizeThatFits(_ size: CGSize) -> CGSize {
        let inner = super.sizeThatFits(CGSize(width: max(0, size.width - padding.left - padding.right), height: .greatestFiniteMagnitude))
        return CGSize(width: ceil(inner.width) + padding.left + padding.right, height: ceil(inner.height) + padding.top + padding.bottom)
    }
}

/// The default Buffer's typing figures as small bordered chips: an icon and a number, never a word, so
/// they read as readings in any language.
final class TypingStatsView: UIView {
    struct Item: Equatable {
        var symbol: String
        var value: String
        /// What VoiceOver says for the icon.
        var spoken: String
    }
    private var chips: [UILabel] = []
    private var items: [Item] = []
    var fontSize: CGFloat = 10.5 { didSet { if oldValue != fontSize { render() } } }
    private var chipFont: UIFont { .monospacedDigitSystemFont(ofSize: fontSize, weight: .medium) }
    override init(frame: CGRect) { super.init(frame: frame); isUserInteractionEnabled = false }
    required init?(coder: NSCoder) { fatalError() }
    /// The numbers, for tests and a plain-text read-out.
    var text: String { items.map(\.value).joined(separator: " · ") }
    var symbols: [String] { items.map(\.symbol) }
    func update(_ values: [Item]) {
        guard values != items else { return }
        items = values
        while chips.count < values.count { chips.append(makeChip()) }
        while chips.count > values.count { chips.removeLast().removeFromSuperview() }
        render()
        accessibilityLabel = values.map { "\($0.spoken) \($0.value)" }.joined(separator: "，")
    }
    private func render() {
        let config = UIImage.SymbolConfiguration(pointSize: fontSize, weight: .medium)
        for (chip, item) in zip(chips, items) {
            let text = NSMutableAttributedString()
            if let image = UIImage(systemName: item.symbol, withConfiguration: config)?.withTintColor(.secondaryLabel, renderingMode: .alwaysOriginal) {
                let attachment = NSTextAttachment(image: image)
                // Sit the icon on the digits' baseline.
                attachment.bounds = CGRect(x: 0, y: -1.5, width: image.size.width, height: image.size.height)
                text.append(NSAttributedString(attachment: attachment)); text.append(NSAttributedString(string: " "))
            }
            text.append(NSAttributedString(string: item.value, attributes: [.font: chipFont, .foregroundColor: UIColor.secondaryLabel]))
            chip.attributedText = text
            chip.accessibilityLabel = "\(item.spoken) \(item.value)"
        }
        setNeedsLayout()
    }
    private func makeChip() -> UILabel {
        let chip = BorderedChipLabel()
        chip.font = chipFont; chip.textColor = .secondaryLabel; chip.textAlignment = .center
        chip.layer.cornerRadius = 5; chip.layer.cornerCurve = .continuous
        chip.layer.borderWidth = 0.8; chip.layer.borderColor = UIColor.separator.cgColor
        addSubview(chip); return chip
    }
    override func traitCollectionDidChange(_ previous: UITraitCollection?) {
        super.traitCollectionDidChange(previous); chips.forEach { $0.layer.borderColor = UIColor.separator.cgColor }
    }
    override func layoutSubviews() {
        super.layoutSubviews()
        let gap: CGFloat = 4, height = min(bounds.height, ceil(chipFont.lineHeight) + 8)
        let widths = chips.map { ceil($0.intrinsicContentSize.width) }
        let total = widths.reduce(0, +) + gap * CGFloat(max(0, chips.count - 1))
        // Too many chips for the line: squeeze them before clipping.
        var x = max(0, (bounds.width - total) / 2), scale: CGFloat = total > bounds.width && total > 0 ? bounds.width / total : 1
        for (chip, width) in zip(chips, widths) {
            chip.frame = CGRect(x: x, y: (bounds.height - height) / 2, width: width * scale, height: height)
            x += width * scale + gap * scale
        }
    }
}
private final class BorderedChipLabel: UILabel {
    private let inset: CGFloat = 6
    override var intrinsicContentSize: CGSize { let s = super.intrinsicContentSize; return CGSize(width: s.width + 2 * inset, height: s.height) }
    override func drawText(in rect: CGRect) { super.drawText(in: rect.insetBy(dx: inset, dy: 0)) }
    override func sizeThatFits(_ size: CGSize) -> CGSize { intrinsicContentSize }
}

/// The progress of a hold-to-send-all: a thin line along the lower edge of the Buffer's input box fills from left to
/// right while a send key is held, the way the desktop button fills its icon, then flashes when everything is
/// released. The view takes the box's frame and corner radius and clips the line to them, so it follows the rounded
/// corners with clean edges.
final class HoldProgressBar: UIView {
    static let height: CGFloat = 3
    private let fill = CALayer()
    override init(frame: CGRect) {
        super.init(frame: frame)
        isUserInteractionEnabled = false; alpha = 0
        layer.cornerCurve = .continuous; layer.masksToBounds = true; layer.cornerRadius = 7
        fill.anchorPoint = CGPoint(x: 0, y: 0.5)
        layer.addSublayer(fill); applyColor()
    }
    required init?(coder: NSCoder) { fatalError() }
    override func tintColorDidChange() { super.tintColorDidChange(); applyColor() }
    private func applyColor() {
        let color = (tintColor ?? .systemBlue)
        fill.backgroundColor = color.cgColor
    }
    override func layoutSubviews() {
        super.layoutSubviews()
        CATransaction.begin(); CATransaction.setDisableActions(true)
        // The line sits on the box's bottom edge; the view's rounded clip shapes its ends at the corners.
        fill.bounds = CGRect(x: 0, y: 0, width: bounds.width, height: Self.height); fill.position = CGPoint(x: 0, y: bounds.height - Self.height / 2)
        CATransaction.commit()
    }
    /// Whether the line is showing, for tests.
    private(set) var isShowing = false
    func begin(duration: TimeInterval) {
        layer.removeAllAnimations(); fill.removeAllAnimations()
        alpha = 1; isShowing = true
        let grow = CABasicAnimation(keyPath: "transform.scale.x")
        grow.fromValue = 0.001; grow.toValue = 1; grow.duration = duration; grow.timingFunction = CAMediaTimingFunction(name: .linear)
        grow.fillMode = .both; grow.isRemovedOnCompletion = false
        fill.add(grow, forKey: "hold")
    }
    /// A tap or a slide-off: the line drops away.
    func cancel() {
        guard isShowing else { return }
        isShowing = false
        UIView.animate(withDuration: 0.18, animations: { self.alpha = 0 }, completion: { _ in
            if !self.isShowing { self.fill.removeAllAnimations() }
        })
    }
    /// The hold finished: the full line flashes once and fades.
    func complete() {
        fill.removeAllAnimations()
        alpha = 1; isShowing = true
        UIView.animate(withDuration: 0.4, delay: 0.05, options: [.curveEaseOut], animations: { self.alpha = 0 }, completion: { _ in
            if self.alpha == 0 { self.isShowing = false }
        })
    }
}
