import XCTest
import UIKit
import RimesCore
@testable import RIMES

@MainActor final class OrdinaryKeyGestureTests: XCTestCase {
    func testHoldThenUpSelectsOnceAndReturningToCapCancelsAlternate() {
        var gesture = OrdinaryKeyGesture(key: "a", origin: .init(x: 20, y: 40), at: 1, alternate: "，", keyWidth: 32)
        XCTAssertFalse(gesture.arm(at: 1.2))
        XCTAssertTrue(gesture.arm(at: 1.4))
        XCTAssertNil(gesture.selectedAlternate)
        gesture.move(to: .init(x: 21, y: 18), key: "q", at: 1.5)
        XCTAssertEqual(gesture.selectedAlternate, "，"); XCTAssertEqual(gesture.key, "a")
        gesture.move(to: .init(x: 21, y: 38), key: "a", at: 1.6)
        XCTAssertNil(gesture.selectedAlternate)
        gesture.move(to: .init(x: 80, y: 18), key: "e", at: 1.7)
        XCTAssertNil(gesture.selectedAlternate, "A sideways drag is not an upward selection")
    }

    func testFastSlideCorrectionCannotTurnIntoAnAlternateAfterWaiting() {
        var gesture = OrdinaryKeyGesture(key: "a", origin: .init(x: 20, y: 40), at: 0, alternate: "，", keyWidth: 32)
        gesture.move(to: .init(x: 60, y: 40), key: "s", at: 0.1)
        XCTAssertEqual(gesture.key, "s"); XCTAssertFalse(gesture.arm(at: 1))
        gesture.move(to: .init(x: 20, y: 18), key: "q", at: 1.1)
        XCTAssertEqual(gesture.key, "q"); XCTAssertNil(gesture.selectedAlternate)
        var quick = OrdinaryKeyGesture(key: "q", origin: .init(x: 20, y: 40), at: 0, alternate: "1", keyWidth: 32)
        quick.move(to: .init(x: 20, y: 15), key: nil, at: 0.1)
        XCTAssertFalse(quick.armed); XCTAssertNil(quick.selectedAlternate)
    }

    func testSurfaceKeepsTapsAndCancelsGesturesAcrossModeAndSettingChanges() {
        let surface = KeySurface(frame: .init(x: 0, y: 0, width: 383, height: 206))
        surface.standardMode = .qwerty; surface.layoutIfNeeded()
        var keys: [String] = [], alternates: [String] = []
        surface.onKey = { keys.append($0) }; surface.onAlternate = { alternates.append($0) }
        surface.developmentOrdinaryGesture(key: "q", duration: 0.6)
        XCTAssertEqual(keys, ["q"]); XCTAssertTrue(alternates.isEmpty)
        surface.longPressSwipeSymbols = true; surface.layoutIfNeeded()
        surface.developmentOrdinaryGesture(key: "q", duration: 0.1, dy: 0)
        surface.developmentOrdinaryGesture(key: "q", duration: 0.6, dy: 0)
        surface.developmentOrdinaryGesture(key: "q", duration: 0.6)
        XCTAssertEqual(keys, ["q", "q", "q"]); XCTAssertEqual(alternates, ["1"])
        surface.developmentOrdinaryGesture(key: "q", duration: 0.6) { surface.cancel() }
        surface.developmentOrdinaryGesture(key: "q", duration: 0.6) { surface.longPressSwipeSymbols = false }
        surface.longPressSwipeSymbols = true
        surface.developmentOrdinaryGesture(key: "q", duration: 0.6) { surface.numeric = true }
        XCTAssertEqual(keys, ["q", "q", "q"]); XCTAssertEqual(alternates, ["1"])
        XCTAssertNil(surface.developmentAlternate(for: "q"))
        surface.numeric = false; surface.chordMode = true
        for english in [false, true] {
            surface.englishInput = english
            XCTAssertNil(surface.developmentAlternate(for: "q"), "All chord-layout modes are excluded")
        }
    }

    func testLiteralAlternatesCommitCompositionInHostAndBufferAcrossOrdinaryLayouts() async throws {
        for buffered in [false, true] {
            for custom in [false, true] {
                let (window, keyboard) = host(); defer { window.isHidden = true }
                try await Task.sleep(nanoseconds: 80_000_000)
                if custom { keyboard.developmentSetCustomLayout(CustomKeyboardLayout.templates[0]) }
                keyboard.developmentSwipeSymbols(true)
                if buffered { keyboard.developmentBuffer("") }
                keyboard.developmentType("nihao"); window.layoutIfNeeded()
                keyboard.layoutViews.keys.developmentOrdinaryGesture(key: "r", duration: 0.4)
                await keyboard.developmentWaitForDelivery()
                XCTAssertEqual(buffered ? keyboard.developmentBufferSource.text : keyboard.layoutProxy.native.text, "你好4")
                XCTAssertTrue(keyboard.developmentRaw.isEmpty)
                keyboard.layoutViews.keys.developmentOrdinaryGesture(key: "a", duration: 0.4)
                XCTAssertEqual(buffered ? keyboard.developmentBufferSource.text : keyboard.layoutProxy.native.text, "你好4，")
                if buffered { XCTAssertTrue(keyboard.layoutProxy.native.text.isEmpty) }
            }
        }
        let (window, keyboard) = host(); defer { window.isHidden = true }
        try await Task.sleep(nanoseconds: 80_000_000)
        keyboard.developmentSwipeSymbols(true)
        keyboard.developmentOrdinaryAppearance(layout: .nineKey)
        keyboard.developmentType("64426"); window.layoutIfNeeded()
        keyboard.layoutViews.keys.developmentOrdinaryGesture(key: "2", duration: 0.4)
        await keyboard.developmentWaitForDelivery()
        XCTAssertEqual(keyboard.layoutProxy.native.text, "你好，"); XCTAssertTrue(keyboard.developmentRaw.isEmpty)
        keyboard.developmentChoose(.english); window.layoutIfNeeded()
        keyboard.layoutViews.keys.developmentOrdinaryGesture(key: "d", duration: 0.4)
        XCTAssertEqual(keyboard.layoutProxy.native.text, "你好，?")
        keyboard.layoutViews.keys.developmentOrdinaryGesture(key: "q", duration: 0.4) { keyboard.developmentHostResigned() }
        XCTAssertEqual(keyboard.layoutProxy.native.text, "你好，?", "Retired host must not receive a late alternate")
    }

    func testAppearanceMigrationAndGestureTogglePreserveLayoutAndIndependentRevision() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }
        let revision = UUID()
        let legacy = "{\"layout\":\"nineKey\",\"skin\":\"rimes\",\"revision\":\"\(revision)\"}"
        try Data(legacy.utf8).write(to: root.appendingPathComponent("keyboard-appearance-v1.json"))
        let store = KeyboardAppearanceStore(root: root)
        XCTAssertEqual(store.load().layout, .nineKey); XCTAssertEqual(store.load().skin, .rimes)
        XCTAssertFalse(store.load().longPressSwipeSymbols)
        try store.saveSwipeSymbols(true)
        let enabled = store.load()
        XCTAssertTrue(enabled.longPressSwipeSymbols); XCTAssertNotNil(enabled.longPressSwipeSymbolsRevision)
        XCTAssertEqual(enabled.revision, revision); XCTAssertEqual(enabled.layout, .nineKey)
        try store.save(.init(layout: .qwerty))
        XCTAssertEqual(store.load().longPressSwipeSymbolsRevision, enabled.longPressSwipeSymbolsRevision)
        XCTAssertTrue(store.load().longPressSwipeSymbols)
        XCTAssertNotEqual(store.load().revision, revision)
    }

    func testDirectSwipeUsesOneAlternateAndKeepsNativeTapAndCancellation() {
        let surface = KeySurface(frame: .init(x: 0, y: 0, width: 383, height: 206))
        surface.standardMode = .qwerty; surface.longPressSwipeSymbols = true; surface.directSwipeSymbols = true
        surface.layoutIfNeeded()
        var keys: [String] = [], alternates: [String] = []
        surface.onKey = { keys.append($0) }; surface.onAlternate = { alternates.append($0) }
        surface.developmentOrdinaryGesture(key: "q", duration: 0.05, dy: 0)
        surface.developmentOrdinaryGesture(key: "q", duration: 0.05)
        XCTAssertEqual(keys, ["q"]); XCTAssertEqual(alternates, ["1"])
        surface.developmentOrdinaryGesture(key: "q", duration: 0.05) { surface.cancel() }
        XCTAssertEqual(alternates, ["1"])
        var drag = OrdinaryKeyGesture(key: "a", origin: .init(x: 20, y: 40), at: 0, alternate: "，", keyWidth: 32, direct: true)
        drag.move(to: .init(x: 60, y: 40), key: "s", at: 0.03)
        drag.move(to: .init(x: 20, y: 15), key: "q", at: 0.06)
        XCTAssertNil(drag.selectedAlternate, "Sideways correction cannot arm later")
    }
    func testNewOptionsDoNotOverwriteSeparateLayoutAndGestureEdits() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: root) }
        let store = KeyboardAppearanceStore(root: root)
        try store.save(.init(layout: .nineKey))
        let stale = store.load()
        try store.saveLayoutOptions(portrait: 125, landscape: 110, numericNineGrid: true)
        try store.saveSwipeSymbols(true, direct: true)
        try store.save(stale)
        XCTAssertEqual(store.load().portraitHeight, 125); XCTAssertEqual(store.load().landscapeHeight, 110)
        XCTAssertTrue(store.load().numericNineGrid); XCTAssertTrue(store.load().directSwipeSymbols)
        XCTAssertEqual(store.load().layout, .nineKey)
        try store.saveSwipeSymbols(false)
        XCTAssertFalse(store.load().longPressSwipeSymbols); XCTAssertFalse(store.load().directSwipeSymbols)
    }
    func testNumericKeypadAndHeightFitHostWithoutChangingPinyinLayout() {
        for width in [CGFloat(280), 393, 840] {
            for landscape in [false, true] {
                for percent in [100, 115, 130] {
                    let height = StandardKeyboardGeometry.height(landscape: landscape, percent: percent)
                    let grid = StandardKeyboardGeometry.make(width: width, mode: .numeric, landscape: landscape, numericNineGrid: true, availableHeight: height)
                    XCTAssertEqual(Set(grid.keys.map(\.text)), Set("1234567890.".map(String.init)))
                    let frames = grid.keys.map(\.frame) + Array(grid.controls.values)
                    for (i, frame) in frames.enumerated() {
                        XCTAssertTrue(frame.minX >= 0 && frame.minY >= 0 && frame.maxX <= width + 0.01 && frame.maxY <= height + 0.01)
                        for other in frames.dropFirst(i + 1) { XCTAssertFalse(frame.intersects(other)) }
                    }
                    let plain = StandardKeyboardGeometry.make(width: width, mode: .qwerty, landscape: landscape, numericNineGrid: true)
                    XCTAssertEqual(plain.keys.count, 26)
                }
            }
        }
    }

    private func host() -> (UIWindow, KeyboardViewController) {
        let window = UIWindow(frame: CGRect(x: 0, y: 0, width: 393, height: 900))
        let parent = UIViewController(); window.rootViewController = parent; window.makeKeyAndVisible()
        let keyboard = KeyboardViewController(); keyboard.layoutNeedsInputModeSwitchKey = false
        keyboard.loadViewIfNeeded(); keyboard.developmentResetPreferences()
        parent.addChild(keyboard); parent.view.addSubview(keyboard.view); keyboard.didMove(toParent: parent)
        parent.view.layoutIfNeeded(); keyboard.view.layoutIfNeeded()
        return (window, keyboard)
    }
}
