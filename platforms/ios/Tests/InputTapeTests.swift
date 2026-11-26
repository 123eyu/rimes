import XCTest
@testable import RIMES

final class InputTapeTests: XCTestCase {
    func testPendingCellsFillInOrderAndEmptyResultsDrop() {
        var tape = InputTape()
        let first = TapeCell(pending: true), second = TapeCell(pending: true), third = TapeCell(pending: true)
        tape.append(first); tape.append(second); tape.append(third)
        XCTAssertTrue(tape.hasPending); XCTAssertEqual(tape.text, ""); XCTAssertEqual(tape.head, 2)
        tape.fill(second.id, with: ["好", "女"])
        tape.fill(first.id, with: ["你"])
        XCTAssertEqual(tape.text, "你好")
        tape.fill(third.id, with: [])
        XCTAssertEqual(tape.cells.count, 2); XCTAssertFalse(tape.hasPending)
        XCTAssertEqual(tape.head, 1, "the head stays on the newest remaining cell")
    }

    func testHeadMovesAlongTheTapeAndPicksThatCellsCandidate() {
        var tape = InputTape()
        tape.append(TapeCell(candidates: ["十", "+"])); tape.append(TapeCell(candidates: ["中", "申"]))
        tape.setHead(0)
        XCTAssertTrue(tape.cycle(1)); XCTAssertEqual(tape.text, "+中")
        XCTAssertFalse(tape.cycle(1), "no candidate past the last")
        tape.choose(0); XCTAssertEqual(tape.text, "十中")
        // A cell added while the head is back on the tape does not pull the head away.
        tape.append(TapeCell(candidates: ["大"])); XCTAssertEqual(tape.head, 0)
        tape.setHead(5); XCTAssertEqual(tape.head, 2)
        tape.append(TapeCell(candidates: ["人"])); XCTAssertEqual(tape.head, 3, "back at the tail, the head follows new cells")
    }

    func testLiveUtteranceIsReplacedInPlaceKeepingChoices() {
        var tape = InputTape()
        tape.append(TapeCell(candidates: ["写"]))
        let group = UUID()
        tape.replaceGroup(group, with: [TapeCell(candidates: ["今天", "金天"], group: group)])
        tape.setHead(1); tape.cycle(1)
        XCTAssertEqual(tape.text, "写金天")
        tape.replaceGroup(group, with: [TapeCell(candidates: ["今天", "金天"], group: group), TapeCell(candidates: ["天气"], group: group)])
        XCTAssertEqual(tape.text, "写金天天气", "a word that survives the update keeps its chosen candidate")
        tape.append(TapeCell(candidates: ["。"]))
        tape.removeGroup(group)
        XCTAssertEqual(tape.text, "写。")
    }

    func testRemoveHeadAndClear() {
        var tape = InputTape()
        for text in ["一", "二", "三"] { tape.append(TapeCell(candidates: [text])) }
        tape.setHead(1); tape.removeHead()
        XCTAssertEqual(tape.text, "一三"); XCTAssertEqual(tape.head, 1)
        tape.removeHead(); tape.removeHead()
        XCTAssertTrue(tape.isEmpty); XCTAssertEqual(tape.head, -1)
        tape.removeHead(); XCTAssertTrue(tape.isEmpty)
    }

    func testHandwritingRadicalsFollowWholeCharacters() {
        XCTAssertEqual(HandwritingRecognizer.rerank(["囗", "口", "亻", "田"]), ["口", "田", "囗", "亻"])
        XCTAssertEqual(HandwritingRecognizer.rerank((0..<15).map { String(UnicodeScalar(0x4E00 + $0)!) }).count, 10)
    }

    func testBrushIsSmoothAndThinsWithSpeed() {
        func stroke(seconds: TimeInterval) -> InkStroke {
            var ink = InkStroke(at: .zero, time: 0, force: nil, minWidth: 3, maxWidth: 10)
            for step in 1...40 { ink.add(CGPoint(x: Double(step) * 5, y: sin(Double(step) / 4) * 20), time: seconds * Double(step) / 40, force: nil) }
            ink.finish(); return ink
        }
        let slow = stroke(seconds: 2), fast = stroke(seconds: 0.1)
        func mean(_ ink: InkStroke) -> CGFloat { ink.samples.map(\.width).reduce(0, +) / CGFloat(ink.samples.count) }
        XCTAssertGreaterThan(mean(slow), mean(fast) + 1, "slow writing draws a fuller line")
        XCTAssertTrue(slow.samples.allSatisfy { (3...10).contains($0.width) })
        // No gap between drawn samples, and the line ends where the finger lifted.
        for index in 1..<slow.samples.count {
            let a = slow.samples[index - 1].point, b = slow.samples[index].point
            XCTAssertLessThan(hypot(a.x - b.x, a.y - b.y), 6)
        }
        XCTAssertEqual(slow.samples.last!.point.x, 200, accuracy: 0.01)
        XCTAssertEqual(slow.raw.count, 41, "recognition keeps every touch point")
    }
}
