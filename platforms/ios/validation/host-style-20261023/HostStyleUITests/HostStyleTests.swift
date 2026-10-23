import XCTest

/// Captures the keyboard drawn in an Apple host (Notes) and a third-party host (the
/// RIMES app's playground) so key colours, shadow and container can be compared.
/// Output directory: HOST_STYLE_OUT, default /private/tmp/host-style.
final class HostStyleTests: XCTestCase {
    private let out = URL(fileURLWithPath: ProcessInfo.processInfo.environment["HOST_STYLE_OUT"] ?? "/private/tmp/host-style")
    override func setUp() {
        continueAfterFailure = false
        try? FileManager.default.createDirectory(at: out, withIntermediateDirectories: true)
    }
    private func shot(_ name: String) {
        let image = XCUIScreen.main.screenshot()
        try? image.pngRepresentation.write(to: out.appendingPathComponent(name + ".png"))
        let attachment = XCTAttachment(screenshot: image); attachment.name = name; attachment.lifetime = .keepAlways; add(attachment)
    }
    private var tag: String { ProcessInfo.processInfo.environment["HOST_STYLE_TAG"] ?? "system" }
    /// The system keyboard is queryable; a custom keyboard extension is not part of the
    /// host's accessibility tree, so it is driven by position (123 key, then #+=).
    private func capturePages(_ app: XCUIApplication, prefix: String) {
        let prefix = "\(tag)-\(prefix)"
        let keyboard = app.keyboards.firstMatch
        _ = keyboard.waitForExistence(timeout: 6)
        // Cycle with the globe key until the wanted keyboard is showing. The system
        // keyboard exposes a "more" (123) key; a custom extension does not.
        let wantSystem = tag == "system"
        for _ in 0..<4 where keyboard.keys["more"].exists != wantSystem {
            app.coordinate(withNormalizedOffset: CGVector(dx: 0.105, dy: 0.953)).tap(); sleep(2)
        }
        let queryable = keyboard.keys["more"].exists
        sleep(1); shot("\(prefix)-letters")
        func tapAt(_ x: CGFloat, _ y: CGFloat) { app.coordinate(withNormalizedOffset: CGVector(dx: x, dy: y)).tap(); sleep(1) }
        if queryable {
            keyboard.buttons["shift"].tap(); sleep(1); shot("\(prefix)-shift"); keyboard.buttons["shift"].tap()
            keyboard.keys["more"].tap(); sleep(1); shot("\(prefix)-numbers")
            if keyboard.keys["more"].exists { keyboard.keys["more"].tap(); sleep(1); shot("\(prefix)-symbols") }
        } else {
            tapAt(0.063, 0.883); shot("\(prefix)-numbers")
            tapAt(0.07, 0.82); shot("\(prefix)-symbols")
        }
    }
    func testAppleHostReminders() {
        let app = XCUIApplication(bundleIdentifier: "com.apple.reminders")
        app.launch()
        sleep(2); shot("apple-launch")
        for label in ["Continue", "Not Now", "Done", "Close"] where app.buttons[label].waitForExistence(timeout: 2) { app.buttons[label].tap() }
        let new = app.buttons.matching(NSPredicate(format: "label CONTAINS[c] 'New Reminder'")).firstMatch
        if new.waitForExistence(timeout: 5) { new.tap() }
        else if app.cells.firstMatch.waitForExistence(timeout: 3) { app.cells.firstMatch.tap(); _ = app.buttons.matching(NSPredicate(format: "label CONTAINS[c] 'New Reminder'")).firstMatch.waitForExistence(timeout: 3); app.buttons.matching(NSPredicate(format: "label CONTAINS[c] 'New Reminder'")).firstMatch.tap() }
        sleep(1); shot("apple-after-new")
        capturePages(app, prefix: "apple-reminders")
    }
    func testThirdPartyHost() {
        let app = XCUIApplication(bundleIdentifier: "org.scholay.rimes.ios")
        app.launch()
        shot("host-launch")
        let link = app.descendants(matching: .any).matching(NSPredicate(format: "label CONTAINS[c] 'Try typing' OR label CONTAINS '输入体验'")).firstMatch
        XCTAssertTrue(link.waitForExistence(timeout: 5)); link.tap()
        let field = app.textViews["playground.primary"]
        XCTAssertTrue(field.waitForExistence(timeout: 5)); field.tap()
        capturePages(app, prefix: "third-party")
    }
    func testProbeGlobe() {
        let app = XCUIApplication(bundleIdentifier: "org.scholay.rimes.ios")
        app.launch()
        app.descendants(matching: .any).matching(NSPredicate(format: "label CONTAINS '输入体验'")).firstMatch.tap()
        app.textViews["playground.primary"].tap(); sleep(2)
        shot("probe-0")
        app.coordinate(withNormalizedOffset: CGVector(dx: 0.105, dy: 0.953)).tap(); sleep(2); shot("probe-1")
        try? app.keyboards.firstMatch.debugDescription.write(to: out.appendingPathComponent("probe-kb.txt"), atomically: true, encoding: .utf8)
        app.coordinate(withNormalizedOffset: CGVector(dx: 0.105, dy: 0.953)).tap(); sleep(2); shot("probe-2")
    }
    /// Hosts built by `OldHost/build.sh <sdk>`; TEST_RUNNER_HOST_STYLE_SDK names the variant.
    func testOldHost() {
        let app = XCUIApplication(bundleIdentifier: "org.scholay.rimes.oldhost")
        app.launch()
        let field = app.textViews["oldhost.field"]
        XCTAssertTrue(field.waitForExistence(timeout: 5)); field.tap()
        capturePages(app, prefix: "oldhost-" + (ProcessInfo.processInfo.environment["HOST_STYLE_SDK"] ?? "sdk"))
    }
}
