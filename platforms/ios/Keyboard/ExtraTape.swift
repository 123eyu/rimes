import UIKit
#if KEYBOARD_LAYOUT_TESTS
@testable import RIMES
#endif

/// One handwritten stroke as touched: points with their timestamps (seconds).
struct InkTrace: Equatable {
    var points: [CGPoint]
    var times: [TimeInterval]
}

/// One recognized unit of the voice or handwriting tape: a handwritten character or a spoken
/// word, with its own candidates. A pending cell is still being recognized. A handwritten cell
/// keeps its ink, so going back to it shows (and lets you rewrite) what was written.
struct TapeCell: Equatable {
    let id: UUID
    var candidates: [String]
    var choice = 0
    var pending: Bool
    /// Cells of one live voice utterance share a group and are replaced together.
    var group: UUID?
    var ink: [InkTrace] = []
    init(id: UUID = UUID(), candidates: [String] = [], pending: Bool = false, group: UUID? = nil, ink: [InkTrace] = []) {
        self.id = id; self.candidates = candidates; self.pending = pending; self.group = group; self.ink = ink
    }
    var text: String { !pending && candidates.indices.contains(choice) ? candidates[choice] : "" }
}

/// The composition of the voice and handwriting schemes ("走带"): cells in order, and a read
/// head on one of them. Moving the head along the tape picks which cell's candidates show; it
/// follows the newest cell until the user moves it back.
struct InputTape: Equatable {
    private(set) var cells: [TapeCell] = []
    private(set) var head = -1
    private(set) var followsTail = true

    var isEmpty: Bool { cells.isEmpty }
    var hasPending: Bool { cells.contains { $0.pending } }
    var text: String { cells.map(\.text).joined() }
    var headCell: TapeCell? { cells.indices.contains(head) ? cells[head] : nil }

    mutating func append(_ cell: TapeCell) {
        cells.append(cell)
        if followsTail || head < 0 { head = cells.count - 1; followsTail = true }
    }
    /// Results for a pending cell; a cell nothing was read from is dropped.
    mutating func fill(_ id: UUID, with candidates: [String]) {
        guard let index = cells.firstIndex(where: { $0.id == id }) else { return }
        if candidates.isEmpty { remove(at: index); return }
        cells[index].candidates = candidates; cells[index].choice = 0; cells[index].pending = false
    }
    /// A rewritten cell: new ink, recognized again.
    mutating func reopen(_ id: UUID, ink: [InkTrace]) {
        guard let index = cells.firstIndex(where: { $0.id == id }) else { return }
        cells[index].ink = ink; cells[index].pending = true; cells[index].choice = 0
    }
    /// Text of the cells before `id`, the context a recognizer reads it in.
    func text(before id: UUID) -> String {
        guard let index = cells.firstIndex(where: { $0.id == id }) else { return text }
        return cells[..<index].map(\.text).joined()
    }
    /// Replaces a live utterance's cells in place; a choice made on a word that survives is kept.
    mutating func replaceGroup(_ group: UUID, with replacement: [TapeCell]) {
        let start = cells.firstIndex { $0.group == group } ?? cells.count
        let old = cells.filter { $0.group == group }
        cells.removeAll { $0.group == group }
        let kept = replacement.map { cell -> TapeCell in
            var cell = cell
            if let previous = old.first(where: { $0.candidates.first == cell.candidates.first }), previous.choice < cell.candidates.count { cell.choice = previous.choice }
            return cell
        }
        cells.insert(contentsOf: kept, at: min(start, cells.count))
        if followsTail || head >= cells.count { head = cells.count - 1; followsTail = true }
    }
    mutating func setHead(_ index: Int) {
        guard !cells.isEmpty else { head = -1; followsTail = true; return }
        head = min(max(0, index), cells.count - 1); followsTail = head == cells.count - 1
    }
    mutating func choose(_ index: Int) {
        guard cells.indices.contains(head), cells[head].candidates.indices.contains(index) else { return }
        cells[head].choice = index
    }
    /// Steps the head cell's choice; true when it changed.
    @discardableResult mutating func cycle(_ delta: Int) -> Bool {
        guard let cell = headCell, !cell.pending, cell.candidates.count > 1 else { return false }
        let next = min(max(0, cell.choice + delta), cell.candidates.count - 1)
        guard next != cell.choice else { return false }
        cells[head].choice = next; return true
    }
    mutating func removeHead() { if cells.indices.contains(head) { remove(at: head) } }
    mutating func removeGroup(_ group: UUID) {
        cells.removeAll { $0.group == group }
        if head >= cells.count || followsTail { head = cells.count - 1; followsTail = true }
    }
    mutating func clear() { cells = []; head = -1; followsTail = true }
    private mutating func remove(at index: Int) {
        cells.remove(at: index)
        if head > index || head >= cells.count { head -= 1 }
        if cells.isEmpty { head = -1; followsTail = true } else { head = max(0, head) }
        if head == cells.count - 1 { followsTail = true }
    }
}

/// The tape drawn as a vertical ribbon that moves past a fixed read head in its middle.
/// Scrolling moves the head along the tape; a horizontal swipe changes the head cell's candidate.
///
/// With `pages` (handwriting), every cell is a page of the pad and one more row after the last
/// cell is the page being written. While that page has ink, one further row follows it:
/// scrolling onto it — the left hand holding the ribbon while the right hand writes — pushes the
/// page onto the tape at once, to be recognized in the background. Scrolling back reopens
/// earlier pages.
final class TapeRibbon: UIView, UIScrollViewDelegate, UIGestureRecognizerDelegate {
    /// The row under the read head changed (a cell index; with pages, `cells.count` is the writing page).
    var onHead: ((Int) -> Void)?
    var onCycle: ((Int) -> Void)?
    /// Scrolled past the writing page while it has ink.
    var onAdvance: (() -> Void)?
    /// A finger is resting on the ribbon (true) or has left it.
    var onHold: ((Bool) -> Void)?
    var pages = false { didSet { if pages != oldValue { relayoutRows() } } }
    /// The writing page has ink.
    var draft = false { didSet { if draft != oldValue { relayoutRows() } } }
    private(set) var row = -1
    private(set) var isHeld = false
    private var followsEnd = true
    private let scroll = UIScrollView()
    private let headMark = UIView()
    private let empty = UILabel()
    private var labels: [UILabel] = []
    private let pageLabel = UILabel(), nextLabel = UILabel()
    private var cells: [TapeCell] = []
    private var advanced = false
    private let rowHeight: CGFloat = 34
    private var headInset: CGFloat { max(0, (bounds.height - rowHeight) / 2) }
    private var writingRow: Int { cells.count }
    private var lastRow: Int { pages ? cells.count + (draft ? 1 : 0) : cells.count - 1 }
    private var firstRow: Int { pages || !cells.isEmpty ? 0 : -1 }

    var accent: UIColor = .systemBlue { didSet { headMark.layer.borderColor = accent.cgColor; headMark.backgroundColor = accent.withAlphaComponent(0.12); stylePages() } }

    override init(frame: CGRect) {
        super.init(frame: frame)
        clipsToBounds = true
        layer.cornerRadius = 10
        backgroundColor = UIColor.secondarySystemBackground.withAlphaComponent(0.5)
        headMark.layer.cornerRadius = 7; headMark.layer.borderWidth = 1.5
        headMark.isUserInteractionEnabled = false
        accent = .systemBlue
        addSubview(headMark)
        scroll.showsVerticalScrollIndicator = false
        scroll.alwaysBounceVertical = true
        scroll.decelerationRate = .fast
        scroll.delegate = self
        if #available(iOS 26, *) {
            for edge in [scroll.topEdgeEffect, scroll.bottomEdgeEffect, scroll.leftEdgeEffect, scroll.rightEdgeEffect] { edge.isHidden = true }
        }
        addSubview(scroll)
        for label in [pageLabel, nextLabel] { label.textAlignment = .center; label.isHidden = true; scroll.addSubview(label) }
        stylePages()
        empty.text = L("走带", "Tape"); empty.font = .systemFont(ofSize: 12); empty.textColor = .tertiaryLabel; empty.textAlignment = .center
        empty.isUserInteractionEnabled = false
        addSubview(empty)
        for direction in [UISwipeGestureRecognizer.Direction.left, .right] {
            let swipe = UISwipeGestureRecognizer(target: self, action: #selector(swiped(_:)))
            swipe.direction = direction; swipe.delegate = self
            addGestureRecognizer(swipe)
        }
        // A resting finger, recognized alongside scrolling: the left hand holding the tape.
        let hold = UILongPressGestureRecognizer(target: self, action: #selector(held(_:)))
        hold.minimumPressDuration = 0; hold.cancelsTouchesInView = false; hold.delaysTouchesBegan = false
        hold.allowableMovement = .greatestFiniteMagnitude; hold.delegate = self
        addGestureRecognizer(hold)
        isMultipleTouchEnabled = false
        isAccessibilityElement = true
        accessibilityTraits = .adjustable
        accessibilityIdentifier = "keyboard.tape.ribbon"
    }
    required init?(coder: NSCoder) { fatalError() }

    func gestureRecognizer(_ gestureRecognizer: UIGestureRecognizer, shouldRecognizeSimultaneouslyWith other: UIGestureRecognizer) -> Bool { true }
    @objc private func held(_ gesture: UILongPressGestureRecognizer) {
        switch gesture.state {
        case .began: isHeld = true; onHold?(true)
        case .ended, .cancelled, .failed: isHeld = false; onHold?(false)
        default: break
        }
    }
    @objc private func swiped(_ gesture: UISwipeGestureRecognizer) { onCycle?(gesture.direction == .left ? 1 : -1) }
    override func accessibilityIncrement() { move(to: row + 1) }
    override func accessibilityDecrement() { move(to: row - 1) }
    private func move(to target: Int) {
        if pages && draft && target > writingRow { onAdvance?(); return }
        let clamped = min(max(firstRow, target), lastRow)
        guard clamped != row else { return }
        row = clamped; followsEnd = pages ? row == writingRow : row == cells.count - 1
        onHead?(row)
    }

    /// Shows the tape. Without pages the head row is the tape's head cell; with pages it stays
    /// on the writing page while following the end, else on the page the user turned to.
    func update(_ tape: InputTape) {
        let changed = tape.cells != cells
        cells = tape.cells
        if changed {
            while labels.count > cells.count { labels.removeLast().removeFromSuperview() }
            while labels.count < cells.count {
                let label = UILabel(); label.textAlignment = .center
                label.adjustsFontSizeToFitWidth = true; label.minimumScaleFactor = 0.45
                scroll.addSubview(label); labels.append(label)
            }
            for (index, cell) in cells.enumerated() { style(labels[index], cell) }
            relayoutRows()
        }
        accessibilityLabel = tape.headCell.map { $0.pending ? L("识别中", "Recognizing") : $0.text } ?? L("走带为空", "Tape empty")
        let target: Int
        if pages { target = followsEnd ? writingRow : min(max(0, row), writingRow) }
        else { target = tape.head; followsEnd = tape.followsTail }
        let moved = target != row
        row = target
        if (moved || changed) && !scroll.isDragging && !scroll.isDecelerating { center(on: row, animated: true) }
    }

    private func style(_ label: UILabel, _ cell: TapeCell) {
        if cell.pending { label.text = "…"; label.textColor = .secondaryLabel; label.font = .systemFont(ofSize: 18) }
        else { label.text = cell.text; label.textColor = cell.choice == 0 ? .label : accent; label.font = .systemFont(ofSize: 20, weight: cell.choice == 0 ? .regular : .semibold) }
    }
    private func stylePages() {
        pageLabel.text = draft ? "✎" : "·"
        pageLabel.font = .systemFont(ofSize: draft ? 16 : 20, weight: .semibold)
        pageLabel.textColor = draft ? accent : .tertiaryLabel
        nextLabel.text = "＋"; nextLabel.font = .systemFont(ofSize: 14); nextLabel.textColor = .quaternaryLabel
    }
    private func relayoutRows() {
        pageLabel.isHidden = !pages
        nextLabel.isHidden = !(pages && draft)
        empty.isHidden = pages || !cells.isEmpty
        stylePages()
        setNeedsLayout(); layoutIfNeeded()
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        scroll.frame = bounds
        empty.frame = bounds
        let inset = headInset
        scroll.contentInset = UIEdgeInsets(top: inset + rowHeight, left: 0, bottom: inset, right: 0)
        scroll.contentSize = CGSize(width: bounds.width, height: rowHeight * CGFloat(max(0, lastRow + 1)))
        for (index, label) in labels.enumerated() { label.frame = rowFrame(index) }
        pageLabel.frame = rowFrame(writingRow); nextLabel.frame = rowFrame(writingRow + 1)
        headMark.frame = CGRect(x: 3, y: inset, width: bounds.width - 6, height: rowHeight)
    }
    private func rowFrame(_ index: Int) -> CGRect { CGRect(x: 4, y: rowHeight * CGFloat(index), width: bounds.width - 8, height: rowHeight) }

    private func offset(for index: Int) -> CGFloat { CGFloat(index) * rowHeight - headInset }
    private func rawIndex(at offset: CGFloat) -> Int { Int(((offset + headInset) / rowHeight).rounded()) }
    private func center(on index: Int, animated: Bool) {
        let target = min(max(firstRow, index), max(firstRow, lastRow))
        scroll.setContentOffset(CGPoint(x: 0, y: offset(for: target)), animated: animated)
    }

    func scrollViewWillBeginDragging(_ scrollView: UIScrollView) { advanced = false }
    func scrollViewDidScroll(_ scrollView: UIScrollView) {
        guard scrollView.isDragging || scrollView.isDecelerating else { return }
        let index = min(max(firstRow, rawIndex(at: scrollView.contentOffset.y)), max(firstRow, lastRow))
        // Reaching the row after an inked page pushes it right away, while still scrolling.
        if pages && draft && index == writingRow + 1 && !advanced { advanced = true; onAdvance?(); return }
        if index != row && (index >= 0 || !pages) {
            row = index; followsEnd = pages ? row == writingRow : row == cells.count - 1
            if pages || !cells.isEmpty { onHead?(row) }
        }
        if index <= writingRow { advanced = false }
    }
    func scrollViewWillEndDragging(_ scrollView: UIScrollView, withVelocity velocity: CGPoint, targetContentOffset: UnsafeMutablePointer<CGPoint>) {
        var target = min(max(firstRow, rawIndex(at: targetContentOffset.pointee.y)), max(firstRow, lastRow))
        if pages && draft && target == writingRow + 1 && !advanced { advanced = true; DispatchQueue.main.async { [weak self] in self?.onAdvance?() } }
        if pages && target > writingRow && !draft { target = writingRow }
        targetContentOffset.pointee.y = offset(for: target)
    }
}
