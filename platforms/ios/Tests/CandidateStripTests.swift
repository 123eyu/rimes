import XCTest
import UIKit
@testable import RIMES

@MainActor final class CandidateStripTests: XCTestCase {
    private func strip(width: CGFloat = 320, expanded: Bool = false, controls: Bool = false) -> CandidateStrip {
        let strip = CandidateStrip()
        strip.expanded = expanded; strip.landscape = width > 600
        strip.controlInsets = .init(top: 0, left: controls ? 36 : 0, bottom: 0, right: controls ? 36 : 0)
        strip.update(Array(repeating: "候选", count: 60))
        strip.frame = CGRect(x: 0, y: 0, width: width, height: max(32, strip.fittingHeight(width: width)))
        strip.layoutIfNeeded()
        return strip
    }
    private func frame(_ button: CandidateButton, in strip: CandidateStrip) -> CGRect {
        button.convert(button.bounds, to: strip)
    }
    private func center(_ button: CandidateButton, in strip: CandidateStrip) -> CGPoint {
        let rect = frame(button, in: strip)
        return CGPoint(x: rect.midX, y: rect.midY)
    }
    private func visibleIndex(at point: CGPoint, in strip: CandidateStrip) -> Int? {
        strip.buttons.indices.first { frame(strip.buttons[$0], in: strip).contains(point) }
    }

    func testCodeHintsAreSmallerMeasuredAndNeverChangeTheSelectedText() throws {
        let strip = CandidateStrip()
        strip.update(["你", "你好"], comments: ["(wq wqi wqiy)", "(wqvb)"])
        strip.frame = CGRect(x: 0, y: 0, width: 320, height: 32); strip.layoutIfNeeded()
        let plain = CandidateLayout.measure(["你", "你好"], width: 320, expanded: false, landscape: false)
        let button = try XCTUnwrap(strip.buttons.first)
        button.layoutIfNeeded()
        XCTAssertEqual(button.currentTitle, "你")
        XCTAssertEqual(button.accessibilityValue, "(wq wqi wqiy)")
        XCTAssertLessThan(button.commentLabel.font.pointSize, try XCTUnwrap(button.titleLabel?.font.pointSize))
        XCTAssertGreaterThan(button.bounds.width, plain.frames[0].width)
        XCTAssertLessThanOrEqual(button.commentLabel.frame.maxX, button.bounds.width)
        var selected: [Int] = []; strip.onSelect = { selected.append($0) }
        button.sendActions(for: .touchUpInside); XCTAssertEqual(selected, [0])
        strip.update(["你", "你好"], comments: ["(wq)", "(wqvb)"])
        XCTAssertEqual(strip.buttons[0].comment, "(wq)")
        strip.update(["你", "你好"])
        XCTAssertTrue(strip.buttons[0].commentLabel.isHidden)
        XCTAssertNil(strip.buttons[0].accessibilityValue)
    }

    func testMatrixFramesStayFixedThroughoutScrollingAndRelayout() {
        for width: CGFloat in [320, 393, 852] {
            for controls in [false, true] {
                let strip = strip(width: width, expanded: true, controls: controls)
                strip.update((0..<60).map { $0 % 7 == 0 ? "这是一个比较长的候选词" : "词\($0)" })
                strip.layoutIfNeeded()
                let original = strip.buttons.map(\.frame)
                let contentSize = strip.scroll.contentSize
                XCTAssertEqual(strip.scroll.frame.minX, controls ? 36 : 0)
                // Expanded rows use the full width; only the first row steps aside for the control.
                XCTAssertEqual(strip.scroll.frame.maxX, width - (controls ? 36 : 0))
                let limit = max(0, contentSize.height - strip.scroll.bounds.height)
                for proposed: CGFloat in [1, 15, 29, 30, 33, 34, 49, 68, 102, 201, 10_000, 68, 0] {
                    let offset = min(proposed, limit)
                    strip.scroll.contentOffset.y = offset
                    strip.setNeedsLayout(); strip.layoutIfNeeded()
                    XCTAssertEqual(strip.scroll.contentOffset.y, offset, accuracy: 0.01)
                    XCTAssertEqual(strip.scroll.contentSize, contentSize)
                    XCTAssertEqual(strip.buttons.map(\.frame), original)
                    for button in strip.buttons {
                        let rect = frame(button, in: strip)
                        XCTAssertGreaterThanOrEqual(rect.minX, strip.scroll.frame.minX)
                        XCTAssertLessThanOrEqual(rect.maxX, strip.scroll.frame.maxX)
                        // The control overlays only the first row; scrolled rows pass beneath it.
                        if offset == 0 { XCTAssertFalse(rect.intersects(strip.expandButton.frame)) }
                    }
                }
            }
        }
    }

    func testAppendingCandidatesPreservesExistingFramesAndScrollPosition() {
        let strip = strip(expanded: true, controls: true)
        let values = (0..<60).map { "候选\($0)" }
        strip.update(Array(values.prefix(30)), context: "shi")
        strip.layoutIfNeeded()
        strip.scroll.contentOffset.y = 34
        let frames = strip.buttons.map(\.frame)
        strip.update(values, context: "shi")
        strip.layoutIfNeeded()
        XCTAssertEqual(Array(strip.buttons.prefix(30)).map(\.frame), frames)
        XCTAssertEqual(strip.scroll.contentOffset.y, 34)
        strip.update(["新候选"], context: "xin")
        strip.layoutIfNeeded()
        XCTAssertEqual(strip.scroll.contentOffset, .zero)
    }

    func testStableMatrixKeepsEveryWordReachableWhileScrollingForwardAndBackward() {
        let strip = strip(width: 320, expanded: true, controls: true)
        strip.update((0..<60).map { $0 % 7 == 0 ? "这是一个比较长的候选词" : "词\($0)" })
        strip.layoutIfNeeded()
        var seen = Set<Int>()
        let limit = strip.scroll.contentSize.height
        for offset in stride(from: CGFloat(0), through: limit, by: 8) {
            strip.scroll.contentOffset.y = offset
            for index in strip.buttons.indices {
                let rect = frame(strip.buttons[index], in: strip)
                if strip.bounds.contains(rect) { seen.insert(index) }
            }
        }
        XCTAssertEqual(seen, Set(strip.buttons.indices))
        strip.scroll.contentOffset = .zero
        XCTAssertEqual(frame(strip.buttons[0], in: strip).minY, 0)
    }

    func testTapAndAccessibilityActivationSelectTheTappedCandidateOnly() {
        let strip = strip()
        var selected: [Int] = []; strip.onSelect = { selected.append($0) }
        strip.buttons[1].sendActions(for: .touchUpInside)
        strip.buttons[2].sendActions(for: .touchUpInside)
        XCTAssertEqual(selected, [1, 2])
        strip.buttons[2].layoutIfNeeded()
        XCTAssertEqual(strip.buttons[2].titleLabel!.transform, .identity)
        XCTAssertEqual(strip.buttons[2].backgroundColor, .clear)
    }






    func testReuseUpdatesWordsAndAccessibilityButRetiresTouchedAndRemovedCandidates() {
        let strip = strip()
        let reused = strip.buttons[0], touched = strip.buttons[1], removed = strip.buttons[59]
        var selected = [Int](); strip.onSelect = { selected.append($0) }
        touched.isHighlighted = true
        strip.update(["新的首选", "第二词", "长一点的候选词"], context: "next composition")
        strip.layoutIfNeeded()
        XCTAssertTrue(strip.buttons[0] === reused)
        XCTAssertFalse(strip.buttons[1] === touched)
        XCTAssertEqual(reused.currentTitle, "新的首选")
        XCTAssertEqual(reused.accessibilityLabel, "新的首选")
        XCTAssertEqual(strip.buttons.count, 3)
        touched.sendActions(for: .touchUpInside)
        removed.sendActions(for: .touchUpInside)
        XCTAssertTrue(selected.isEmpty)
        reused.sendActions(for: .touchUpInside)
        XCTAssertEqual(selected, [0])
        strip.update([]); reused.sendActions(for: .touchUpInside)
        XCTAssertEqual(selected, [0])
        XCTAssertEqual(strip.fittingHeight(width: 320), 0)
    }


    func testNeighbouringCandidatesTouchSoNoTapFallsInADeadGap() {
        let strip = strip()
        let frames = strip.buttons.map(\.frame)
        for (left, right) in zip(frames, frames.dropFirst()) where left.minY == right.minY {
            XCTAssertEqual(left.maxX, right.minX, accuracy: 0.01)
        }
    }

    func testExpandedRowsBelowTheFirstUseTheFullWidthWithoutAnEmptyColumn() {
        let strip = strip(width: 320, expanded: true)
        strip.update((0..<60).map { "候选\($0)" })
        strip.layoutIfNeeded()
        XCTAssertEqual(strip.scroll.frame.maxX, 320)
        let rows = Dictionary(grouping: strip.buttons.map(\.frame), by: \.minY)
        let first = try! XCTUnwrap(rows[0]), later = rows.filter { $0.key > 0 }.values
        XCTAssertLessThanOrEqual(first.map(\.maxX).max()!, 320 - CandidateLayout.controlWidth)
        XCTAssertTrue(later.contains { $0.map(\.maxX).max()! > 320 - CandidateLayout.controlWidth })
        XCTAssertEqual(strip.expandButton.frame.width, CandidateLayout.controlWidth)
    }
}
