import XCTest
import UIKit
import RimesCore
@testable import RIMES

@MainActor final class KeyboardLatencyProbeTests: XCTestCase {
    func testRepeatedPresentationsAndContinuousTyping() async throws {
        var samples = [Double]()
        var survivors: [() -> Bool] = []
        for presentation in 0..<12 {
            let controller = KeyboardViewController()
            controller.layoutNeedsInputModeSwitchKey = false
            controller.loadViewIfNeeded(); controller.developmentResetPreferences()
            controller.developmentChoose(.pinyin)
            let window = UIWindow(frame: CGRect(x: 0, y: 0, width: 393, height: 852))
            let parent = UIViewController(); window.rootViewController = parent; window.makeKeyAndVisible()
            parent.addChild(controller); parent.view.addSubview(controller.view); controller.didMove(toParent: parent)
            controller.view.frame = CGRect(x: 0, y: 400, width: 393, height: 300)
            controller.beginAppearanceTransition(true, animated: false); controller.endAppearanceTransition(); parent.view.layoutIfNeeded()
            for _ in 0..<25 {
                for key in "nihao" {
                    let start = ProcessInfo.processInfo.systemUptime
                    controller.developmentType(String(key)); window.layoutIfNeeded()
                    samples.append((ProcessInfo.processInfo.systemUptime - start) * 1000)
                    await Task.yield()
                }
                controller.developmentSpaceKey.sendActions(for: .touchUpInside)
                await controller.developmentWaitForDelivery()
            }
            XCTAssertTrue(controller.layoutProxy.native.text.contains("你好"))
            controller.beginAppearanceTransition(false, animated: false); controller.endAppearanceTransition()
            controller.willMove(toParent: nil); controller.view.removeFromSuperview(); controller.removeFromParent()
            window.isHidden = true; window.rootViewController = nil
            survivors.append { [weak controller] in controller != nil }
            print("LATENCY_PRESENTATION \(presentation) livePrevious=\(survivors.filter { $0() }.count)")
        }
        try await Task.sleep(for: .milliseconds(100))
        let ordered = samples.sorted()
        print("LATENCY_PROBE samples=\(ordered.count) p50=\(ordered[ordered.count/2]) p95=\(ordered[ordered.count*95/100]) max=\(ordered.last ?? 0) survivors=\(survivors.filter { $0() }.count)")
        XCTAssertEqual(survivors.filter { $0() }.count, 0)
    }
}

@MainActor final class KeyboardResourceTests: XCTestCase {
    private func animations(in layer: CALayer) -> Int {
        (layer.animationKeys()?.count ?? 0) + (layer.sublayers ?? []).reduce(0) { $0 + animations(in: $1) }
    }
    func testStatusLightStopsWhenHiddenOrDetached() {
        let window = UIWindow(frame: CGRect(x: 0, y: 0, width: 393, height: 852))
        let parent = UIViewController(); window.rootViewController = parent; window.makeKeyAndVisible()
        defer { window.isHidden = true }
        let light = StatusLight(frame: CGRect(x: 0, y: 0, width: 32, height: 36))
        parent.view.addSubview(light); light.skin = .kitten; light.set(.streaming)
        XCTAssertGreaterThan(animations(in: light.layer), 0)
        light.isHidden = true
        XCTAssertEqual(animations(in: light.layer), 0, "Hidden status animations must stop immediately.")
        light.set(.waiting)
        XCTAssertEqual(animations(in: light.layer), 0, "State changes must not restart a hidden light.")
        light.isHidden = false
        XCTAssertGreaterThan(animations(in: light.layer), 0)
        light.removeFromSuperview()
        XCTAssertEqual(animations(in: light.layer), 0, "Detachment must stop even when UIKit retains the view.")
        parent.view.addSubview(light)
        XCTAssertGreaterThan(animations(in: light.layer), 0)
    }
    func testClosingBufferAndLosingFocusStopStatusAnimations() throws {
        let controller = KeyboardViewController(); controller.layoutNeedsInputModeSwitchKey = false
        controller.loadViewIfNeeded(); controller.developmentResetPreferences()
        let window = UIWindow(frame: CGRect(x: 0, y: 0, width: 393, height: 852))
        window.rootViewController = controller; window.makeKeyAndVisible()
        defer { window.isHidden = true }
        controller.developmentBuffer("合成测试"); controller.developmentSkin(.kitten)
        let light = try XCTUnwrap(controller.layoutViews.buffer.subviews.first { $0 is StatusLight } as? StatusLight)
        XCTAssertGreaterThan(animations(in: light.layer), 0)
        controller.developmentBuffer(nil)
        XCTAssertTrue(light.isHidden)
        XCTAssertEqual(animations(in: light.layer), 0)
        let frame = controller.layoutViews.source.frame
        controller.developmentType("nihao"); window.layoutIfNeeded()
        XCTAssertEqual(controller.layoutViews.source.frame, frame, "Ordinary typing must leave hidden Buffer geometry alone.")
        controller.developmentBuffer("合成测试")
        XCTAssertGreaterThan(animations(in: light.layer), 0)
        controller.developmentHostResigned()
        XCTAssertTrue(light.isHidden)
        XCTAssertEqual(animations(in: light.layer), 0)
        controller.developmentResume()
        XCTAssertFalse(light.isHidden)
        XCTAssertGreaterThan(animations(in: light.layer), 0)
    }
    func testZeroAndAccessoryOnlyLinesHaveEmptyMasksAndRecover() throws {
        let line = SingleLineTextView()
        line.text = "合成测试"; line.caretLocation = 2
        let canvas = try XCTUnwrap(line.subviews.first { $0.layer.mask is CAShapeLayer })
        let mask = try XCTUnwrap(canvas.layer.mask as? CAShapeLayer)
        for size in [CGSize.zero, CGSize(width: 4, height: 36), CGSize(width: 100, height: 4), CGSize(width: 44, height: 36)] {
            line.trailingAccessoryWidth = size.width == 44 ? 44 : 0
            line.frame = CGRect(origin: .zero, size: size)
            line.setNeedsLayout(); line.layoutIfNeeded()
            XCTAssertEqual(mask.path?.isEmpty, true)
        }
        line.trailingAccessoryWidth = 44
        line.frame = CGRect(x: 0, y: 0, width: 220, height: 36)
        line.setNeedsLayout(); line.layoutIfNeeded()
        XCTAssertEqual(mask.path?.isEmpty, false)
        XCTAssertEqual(line.text, "合成测试")
        XCTAssertEqual(line.caretLocation, 2)
        XCTAssertTrue(try XCTUnwrap(mask.path).boundingBox.width.isFinite)
    }
}
