import XCTest
import UIKit
import RimesCore

@MainActor final class KeyboardSizingTests: XCTestCase {
    func testFirstAppearanceComputesHeightBeforeInputViewHasBounds() throws {
        let keyboard = KeyboardViewController()
        keyboard.layoutNeedsInputModeSwitchKey = false
        keyboard.loadViewIfNeeded()
        keyboard.developmentResetPreferences(); keyboard.developmentChoose(.chord)
        keyboard.developmentSetLayout(.orthogonal)
        let input = try XCTUnwrap(keyboard.inputView)
        let host = UIView(frame: CGRect(x: 0, y: 0, width: 393, height: 900))
        host.addSubview(input)
        for width: CGFloat in [393, 852, 320] {
            host.bounds.size.width = width
            input.bounds = .zero
            // The first sizing query precedes the input view's first layout.
            keyboard.viewWillAppear(false)
            keyboard.updateViewConstraints()
            let expected = KeyboardGeometry.height(layout: .orthogonal, chord: true, numeric: false,
                emoji: false, landscape: width > 600, width: width - 10) + 10 + 32 + (width > 600 ? 34 : 40) + 8 - 3
            for proposedHeight: CGFloat in [0, 600, 900] {
                let fitted = input.systemLayoutSizeFitting(CGSize(width: width, height: proposedHeight),
                    withHorizontalFittingPriority: .required, verticalFittingPriority: .fittingSizeLevel)
                XCTAssertEqual(fitted.width, width, accuracy: 0.5)
                XCTAssertEqual(fitted.height, expected, accuracy: 0.5)
            }
        }
    }

    func testOrdinaryCustomLayoutComputesHeightBeforeFirstLayoutWithBuffer() throws {
        let keyboard = KeyboardViewController()
        keyboard.layoutNeedsInputModeSwitchKey = false
        keyboard.loadViewIfNeeded(); keyboard.developmentResetPreferences()
        let input = try XCTUnwrap(keyboard.inputView)
        let host = UIView(frame: CGRect(x: 0, y: 0, width: 393, height: 900))
        host.addSubview(input)
        for layout in CustomKeyboardLayout.templates {
            keyboard.developmentSetCustomLayout(layout)
            for width: CGFloat in [320, 393, 852] {
                host.bounds.size.width = width
                for buffering in [false, true] {
                    keyboard.developmentBuffer(buffering ? "尺寸检查" : nil)
                    input.bounds = .zero
                    keyboard.updateViewConstraints()
                    let keyHeight = layout.geometry(width: Double(width - 10), landscape: width > 600).height
                    let expected = keyHeight + 32 + 10 + 4 + (buffering ? (width > 600 ? 64 : 80) : 0)
                    let fitted = input.systemLayoutSizeFitting(CGSize(width: width, height: 900),
                        withHorizontalFittingPriority: .required, verticalFittingPriority: .fittingSizeLevel)
                    XCTAssertEqual(fitted.width, width, accuracy: 0.5)
                    XCTAssertEqual(fitted.height, expected, accuracy: 0.5)
                }
            }
        }
    }

    func testUIKitPresentsAndResizesKeyboardAtContentHeight() async throws {
        let scene = try XCTUnwrap(UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }.first)
        let previousKeyWindow = scene.windows.first { $0.isKeyWindow }
        let window = UIWindow(windowScene: scene)
        let parent = UIViewController()
        window.rootViewController = parent; window.makeKeyAndVisible()
        let field = SizingTestTextField(frame: CGRect(x: 20, y: 100, width: 300, height: 44))
        parent.view.addSubview(field)
        field.keyboard.loadViewIfNeeded()
        field.keyboard.view.frame = CGRect(x: 0, y: 0, width: scene.coordinateSpace.bounds.width, height: 600)
        defer {
            field.resignFirstResponder(); window.isHidden = true
            previousKeyWindow?.makeKeyAndVisible()
        }
        // Capture the sizes announced for the animation, not only its settled
        // endpoint: a tall first frame followed by a short one visibly flashes.
        var announcedHeights = [CGFloat]()
        let observation = NotificationCenter.default.addObserver(forName: UIResponder.keyboardWillChangeFrameNotification,
                                                                object: nil, queue: .main) { notification in
            if let frame = notification.userInfo?[UIResponder.keyboardFrameEndUserInfoKey] as? CGRect,
               frame.minY < scene.coordinateSpace.bounds.maxY, frame.height > 0 {
                announcedHeights.append(frame.height)
            }
        }
        defer { NotificationCenter.default.removeObserver(observation) }
        // UIKit owns the container and all its constraints in this test.
        XCTAssertTrue(field.becomeFirstResponder())
        try await Task.sleep(nanoseconds: 500_000_000)
        let keyboard = field.keyboard
        XCTAssertNotNil(keyboard.view.window)
        XCTAssertGreaterThan(keyboard.view.bounds.width, 300)
        XCTAssertGreaterThan(keyboard.view.bounds.height, 140)
        XCTAssertLessThan(keyboard.view.bounds.height, 300)
        let candidates = keyboard.layoutViews.candidates
        XCTAssertEqual(candidates.convert(candidates.bounds, to: keyboard.view).minY, 5, accuracy: 0.5)
        let finalHeight = try XCTUnwrap(announcedHeights.last)
        XCTAssertTrue(announcedHeights.allSatisfy { $0 <= finalHeight + 0.5 }, "Initial keyboard animation heights: \(announcedHeights)")

        // Check visible content through UIKit's responder/container lifecycle.
        // A local inputViewController is not the remote keyboard extension: UIKit
        // height notifications can lag even for a bare UIInputView (see the
        // validation/build48/UIKitHeightProbe.swift diagnostic). Remote-host
        // height and first-frame flicker require the actual extension recording.
        let idleHeight = keyboard.view.bounds.height
        keyboard.developmentBuffer("展开检查")
        try await Task.sleep(nanoseconds: 300_000_000)
        XCTAssertEqual(keyboard.view.bounds.height, idleHeight + 80, accuracy: 0.5)
        keyboard.developmentContent(preedit: "ni", candidates: Array(repeating: "候选词", count: 60))
        keyboard.layoutViews.candidates.onExpand?()
        try await Task.sleep(nanoseconds: 300_000_000)
        XCTAssertGreaterThan(keyboard.view.bounds.height, idleHeight + 80)
        keyboard.layoutViews.candidates.onExpand?()
        try await Task.sleep(nanoseconds: 300_000_000)
        XCTAssertEqual(keyboard.view.bounds.height, idleHeight + 80, accuracy: 0.5)
        keyboard.developmentBuffer(nil); keyboard.developmentContent()
        try await Task.sleep(nanoseconds: 300_000_000)
        XCTAssertEqual(keyboard.view.bounds.height, idleHeight, accuracy: 0.5)
    }

    func testFirstMountDiscardsProvisionalHostHeight() throws {
        for width: CGFloat in [320, 393, 852] {
            let window = UIWindow(frame: CGRect(x: 0, y: 0, width: width, height: 900))
            let parent = UIViewController()
            window.rootViewController = parent; window.makeKeyAndVisible()
            defer { window.isHidden = true }

            let keyboard = KeyboardViewController()
            keyboard.layoutNeedsInputModeSwitchKey = false
            keyboard.loadViewIfNeeded()
            keyboard.developmentResetPreferences()
            keyboard.developmentChoose(.chord)
            // Do not disable autoresizing-mask constraints in this fixture: the
            // keyboard must reject the provisional height itself on first attachment.
            keyboard.view.frame = CGRect(x: 0, y: 300, width: width, height: 600)
            parent.addChild(keyboard); parent.view.addSubview(keyboard.view)
            keyboard.didMove(toParent: parent)
            parent.view.layoutIfNeeded(); keyboard.view.layoutIfNeeded()

            let views = keyboard.layoutViews
            let top = views.candidates.convert(views.candidates.bounds, to: keyboard.view).minY
            XCTAssertEqual(keyboard.view.bounds.width, width, accuracy: 0.5)
            XCTAssertEqual(top, 5, accuracy: 0.5, "Cold mount at width \(width)")
            XCTAssertLessThan(keyboard.view.bounds.height, 300)
            XCTAssertEqual(views.bottom.frame.maxY, keyboard.view.bounds.height - 5, accuracy: 0.5)
            XCTAssertTrue(try XCTUnwrap(keyboard.inputView).allowsSelfSizing)
        }
    }

    func testContentStaysOnHostBottomEdgeWhileContainerHeightLags() throws {
        let window = UIWindow(frame: CGRect(x: 0, y: 0, width: 393, height: 900))
        let parent = UIViewController()
        window.rootViewController = parent; window.makeKeyAndVisible()
        defer { window.isHidden = true }
        // Like the remote host, this container imposes only its own size.
        let container = UIView(frame: CGRect(x: 0, y: 0, width: 393, height: 874))
        parent.view.addSubview(container)
        let keyboard = KeyboardViewController()
        keyboard.layoutNeedsInputModeSwitchKey = false
        keyboard.loadViewIfNeeded()
        keyboard.developmentResetPreferences(); keyboard.developmentChoose(.chord)
        // This test checks exact heights; the one-point answer to the provisional
        // height has its own test.
        keyboard.developmentSkipProvisionalHeightAnswer()
        parent.addChild(keyboard); container.addSubview(keyboard.view)
        keyboard.didMove(toParent: parent)
        container.layoutIfNeeded()
        let idleHeight = keyboard.view.bounds.height

        func check(container height: CGFloat, content: CGFloat, file: StaticString = #filePath, line: UInt = #line) {
            container.frame.size.height = height
            container.setNeedsLayout(); container.layoutIfNeeded()
            XCTAssertFalse(keyboard.view.hasAmbiguousLayout, file: file, line: line)
            XCTAssertEqual(keyboard.view.frame.minX, 0, accuracy: 0.5, file: file, line: line)
            XCTAssertEqual(keyboard.view.frame.height, content, accuracy: 0.5, file: file, line: line)
            XCTAssertEqual(keyboard.view.frame.maxY, height, accuracy: 0.5, file: file, line: line)
        }
        // Cold start: full screen, then a provisional height, then the requested one.
        for height in [874, 444, idleHeight] { check(container: height, content: idleHeight) }
        // Buffer grows and shrinks the content before the container follows.
        keyboard.developmentBuffer("底边检查")
        check(container: idleHeight, content: idleHeight + 80)
        check(container: idleHeight + 80, content: idleHeight + 80)
        keyboard.developmentBuffer(nil)
        check(container: idleHeight + 80, content: idleHeight)
        check(container: idleHeight, content: idleHeight)
    }

    @MainActor private func gapFixture(answersProvisionalHeight: Bool = false) throws -> (keyboard: KeyboardViewController, container: UIView, idleHeight: CGFloat, window: UIWindow) {
        let window = UIWindow(frame: CGRect(x: 0, y: 0, width: 393, height: 900))
        let parent = UIViewController()
        window.rootViewController = parent; window.makeKeyAndVisible()
        // Like the remote host, this container imposes only its own size.
        let container = UIView(frame: CGRect(x: 0, y: 0, width: 393, height: 874))
        parent.view.addSubview(container)
        let keyboard = KeyboardViewController()
        keyboard.layoutNeedsInputModeSwitchKey = false
        keyboard.loadViewIfNeeded()
        keyboard.developmentResetPreferences(); keyboard.developmentChoose(.chord)
        if !answersProvisionalHeight { keyboard.developmentSkipProvisionalHeightAnswer() }
        parent.addChild(keyboard); container.addSubview(keyboard.view)
        keyboard.didMove(toParent: parent)
        container.layoutIfNeeded()
        return (keyboard, container, keyboard.view.bounds.height, window)
    }

    func testPlaceholderTracksGapButStaysInvisibleForShortLivedGaps() throws {
        let (keyboard, container, idleHeight, window) = try gapFixture()
        defer { window.isHidden = true }
        func settle(container height: CGFloat) {
            container.frame.size.height = height
            container.setNeedsLayout(); container.layoutIfNeeded()
        }
        for height in [874, 444] as [CGFloat] {
            settle(container: height)
            let gap = keyboard.developmentHeightGap
            XCTAssertEqual(gap.height, height - idleHeight, accuracy: 0.5, "container \(height)")
            XCTAssertEqual(gap.wordmark, "Lingxi IME")
            // A short-lived provisional frame must never show the branded fill.
            XCTAssertFalse(gap.wordmarkVisible)
            XCTAssertEqual(keyboard.view.frame.maxY, height, accuracy: 0.5)
            XCTAssertEqual(keyboard.view.frame.height, idleHeight, accuracy: 0.5)
        }
        settle(container: idleHeight)
        XCTAssertEqual(keyboard.developmentHeightGap.height, 0, accuracy: 0.5)
        // A container shorter than the content keeps the keys on the bottom edge.
        keyboard.developmentBuffer("占位检查")
        settle(container: idleHeight)
        XCTAssertEqual(keyboard.developmentHeightGap.height, 0, accuracy: 0.5)
        XCTAssertEqual(keyboard.view.frame.maxY, idleHeight, accuracy: 0.5)
        XCTAssertEqual(keyboard.view.frame.height, idleHeight + 80, accuracy: 0.5)
        // Buffer closing while the container lags leaves an 80-point gap, then none.
        settle(container: idleHeight + 80)
        keyboard.developmentBuffer(nil)
        settle(container: idleHeight + 80)
        XCTAssertEqual(keyboard.developmentHeightGap.height, 80, accuracy: 0.5)
        settle(container: idleHeight)
        XCTAssertEqual(keyboard.developmentHeightGap.height, 0, accuracy: 0.5)
    }

    func testPersistentGapIsCoveredAndRequestedAgainThenRecovers() async throws {
        let (keyboard, container, idleHeight, window) = try gapFixture()
        defer { window.isHidden = true }
        container.frame.size.height = 444
        container.setNeedsLayout(); container.layoutIfNeeded()
        XCTAssertEqual(keyboard.developmentHeightGap.recoveryAttempts, 0)
        // The host never follows: the gap is covered and the height is re-asked,
        // a bounded number of times.
        try await Task.sleep(nanoseconds: 600_000_000)
        var gap = keyboard.developmentHeightGap
        XCTAssertGreaterThanOrEqual(gap.recoveryAttempts, 1)
        XCTAssertLessThanOrEqual(gap.recoveryAttempts, 4)
        XCTAssertEqual(gap.height, 444 - idleHeight, accuracy: 0.5)
        XCTAssertTrue(gap.wordmarkVisible)
        // The host catches up: the placeholder withdraws and the budget resets.
        container.frame.size.height = idleHeight
        container.setNeedsLayout(); container.layoutIfNeeded()
        gap = keyboard.developmentHeightGap
        XCTAssertEqual(gap.height, 0, accuracy: 0.5)
        XCTAssertEqual(gap.recoveryAttempts, 0)
        XCTAssertFalse(gap.wordmarkVisible)
        XCTAssertFalse(keyboard.view.hasAmbiguousLayout)
    }

    func testColdStartAnswersProvisionalHostHeightWithOneNudge() async throws {
        let (keyboard, container, idleHeight, window) = try gapFixture(answersProvisionalHeight: true)
        defer { window.isHidden = true }
        func settle(container height: CGFloat) {
            container.frame.size.height = height
            container.setNeedsLayout(); container.layoutIfNeeded()
        }
        let screen = try XCTUnwrap(window.windowScene?.screen.bounds.height ?? UIScreen.main.bounds.height)
        // Before presentation, and while the container is still the full screen,
        // the request is the plain content height.
        settle(container: screen)
        XCTAssertEqual(keyboard.view.frame.height, idleHeight, accuracy: 0.01)
        keyboard.viewWillAppear(false)
        settle(container: screen)
        XCTAssertEqual(keyboard.view.frame.height, idleHeight, accuracy: 0.01)
        // The provisional height arrives: the request changes at once, by one point.
        settle(container: 444)
        container.setNeedsLayout(); container.layoutIfNeeded()
        XCTAssertEqual(keyboard.view.frame.height, idleHeight + 1, accuracy: 0.01)
        XCTAssertEqual(keyboard.view.frame.maxY, 444, accuracy: 0.5)
        settle(container: idleHeight + 1)
        // The point is given back, and later gaps are ordinary resizes.
        try await Task.sleep(nanoseconds: 400_000_000)
        settle(container: idleHeight + 1)
        XCTAssertEqual(keyboard.view.frame.height, idleHeight, accuracy: 0.01)
        settle(container: idleHeight)
        settle(container: idleHeight + 80)
        container.setNeedsLayout(); container.layoutIfNeeded()
        XCTAssertEqual(keyboard.view.frame.height, idleHeight, accuracy: 0.01)
    }

    func testRequestedHeightDoesNotChangeWhenPresentationStarts() throws {
        // viewWillAppear is the first moment the controller is "onscreen"; the
        // height requested before it must already include the bottom row.
        for width: CGFloat in [393, 852] {
            var heights = [Bool: CGFloat]()
            for needsSwitchKey in [true, false] {
                let keyboard = KeyboardViewController()
                keyboard.layoutNeedsInputModeSwitchKey = needsSwitchKey
                keyboard.loadViewIfNeeded()
                keyboard.developmentResetPreferences(); keyboard.developmentChoose(.pinyin)
                let input = try XCTUnwrap(keyboard.inputView)
                let host = UIView(frame: CGRect(x: 0, y: 0, width: width, height: 900))
                host.addSubview(input)
                func fitted() -> CGFloat {
                    keyboard.updateViewConstraints()
                    return input.systemLayoutSizeFitting(CGSize(width: width, height: 900),
                        withHorizontalFittingPriority: .required, verticalFittingPriority: .fittingSizeLevel).height
                }
                let before = fitted()
                keyboard.viewWillAppear(false)
                let after = fitted()
                XCTAssertEqual(before, after, accuracy: 0.5, "switch key \(needsSwitchKey), width \(width)")
                heights[needsSwitchKey] = before
            }
            // The bottom row exists exactly when the system asks for a switch key.
            let bottom: CGFloat = width > 600 ? 34 : 40
            XCTAssertEqual(try XCTUnwrap(heights[true]) - (try XCTUnwrap(heights[false])), bottom + 4, accuracy: 0.5)
        }
    }

    func testHeightFollowsContentAndRotationAfterSystemSizing() throws {
        let window = UIWindow(frame: CGRect(x: 0, y: 0, width: 900, height: 900))
        let parent = UIViewController()
        window.rootViewController = parent; window.makeKeyAndVisible()
        defer { window.isHidden = true }
        let keyboard = KeyboardViewController()
        keyboard.layoutNeedsInputModeSwitchKey = false
        parent.addChild(keyboard); parent.view.addSubview(keyboard.view)
        // Exercise layout changes under explicit host constraints; the other tests
        // cover UIKit's frame-managed presentation and self-sizing negotiation.
        keyboard.view.translatesAutoresizingMaskIntoConstraints = false
        let width = keyboard.view.widthAnchor.constraint(equalToConstant: 393)
        NSLayoutConstraint.activate([
            width,
            keyboard.view.leadingAnchor.constraint(equalTo: parent.view.leadingAnchor),
            keyboard.view.bottomAnchor.constraint(equalTo: parent.view.bottomAnchor)
        ])
        keyboard.didMove(toParent: parent)
        keyboard.developmentResetPreferences(); keyboard.developmentChoose(.chord)
        keyboard.developmentContent(); keyboard.developmentBuffer(nil)
        parent.view.layoutIfNeeded()
        let idleHeight = keyboard.view.bounds.height

        func checkContentFits(file: StaticString = #filePath, line: UInt = #line) throws {
            parent.view.layoutIfNeeded(); keyboard.view.layoutIfNeeded()
            let views = keyboard.layoutViews
            let topView = views.buffer.isHidden ? try XCTUnwrap(views.candidates.superview) : views.buffer
            XCTAssertEqual(topView.frame.minY, 5, accuracy: 0.5, file: file, line: line)
            XCTAssertEqual(views.bottom.frame.maxY, keyboard.view.bounds.height - 5, accuracy: 0.5, file: file, line: line)
            let input = try XCTUnwrap(keyboard.inputView)
            for proposed in [UIView.layoutFittingCompressedSize, CGSize(width: width.constant, height: 600)] {
                let fitted = input.systemLayoutSizeFitting(proposed)
                XCTAssertEqual(fitted.height, keyboard.view.bounds.height, accuracy: 0.5, file: file, line: line)
            }
        }
        try checkContentFits()

        // A host frame change at the same width must not become the content height.
        keyboard.view.frame.size.height = 600
        XCTAssertEqual(keyboard.view.bounds.height, 600)
        parent.view.setNeedsLayout()
        try checkContentFits()
        XCTAssertEqual(keyboard.view.bounds.height, idleHeight, accuracy: 0.5)

        keyboard.developmentBuffer("测试高度。")
        try checkContentFits()
        XCTAssertEqual(keyboard.view.bounds.height, idleHeight + 80, accuracy: 0.5)
        keyboard.developmentContent(preedit: "ni", candidates: Array(repeating: "这是一个很长的候选词", count: 8))
        keyboard.layoutViews.candidates.onExpand?()
        try checkContentFits()
        XCTAssertGreaterThan(keyboard.view.bounds.height, idleHeight + 80)

        // Width changes must use the landscape row heights in the first layout pass.
        width.constant = 852
        try checkContentFits()
        XCTAssertEqual(keyboard.layoutViews.buffer.bounds.height, 60)
        XCTAssertEqual(keyboard.layoutViews.bottom.bounds.height, 34)
        width.constant = 393
        try checkContentFits()
        XCTAssertEqual(keyboard.layoutViews.buffer.bounds.height, 76)

        keyboard.layoutViews.candidates.onExpand?()
        keyboard.developmentBuffer(nil); keyboard.developmentContent()
        try checkContentFits()
        XCTAssertEqual(keyboard.view.bounds.height, idleHeight, accuracy: 0.5)
        keyboard.beginAppearanceTransition(false, animated: false); keyboard.endAppearanceTransition()
        keyboard.view.frame.size.height = 600
        keyboard.beginAppearanceTransition(true, animated: false); keyboard.endAppearanceTransition()
        parent.view.setNeedsLayout()
        try checkContentFits()
        XCTAssertLessThan(keyboard.view.bounds.height, 300)
    }
}

@MainActor private final class SizingTestTextField: UITextField {
    let keyboard = KeyboardViewController()
    override var inputViewController: UIInputViewController? { keyboard }
}
