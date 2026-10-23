import XCTest
import UIKit
import RimesCore
@testable import RIMES

@MainActor final class KeyboardReturnTests: XCTestCase {
    private enum Layout: CaseIterable { case qwerty, nineKey, custom, orthogonal, split }

    func testAIConsentStaysInsideKeyboardAndOnlyExplicitAgreementSends() throws {
        for layout in Layout.allCases {
            let (window, keyboard) = host(layout); defer { window.isHidden = true }
            keyboard.developmentAIFullAccess = true
            keyboard.developmentAIProvider(.init(name: "Test service", baseURL: "https://example.com/v1", model: "test"))
            keyboard.developmentPlugin(.polish, source: "Only this Buffer text")
            var requests: [String] = []
            keyboard.developmentAuthorizedAIRequest = { text, _, _ in requests.append(text) }
            window.layoutIfNeeded()
            let height = keyboard.view.bounds.height
            tap(keyboard.developmentPluginControls.run); window.layoutIfNeeded()
            XCTAssertNil(keyboard.presentedViewController)
            let panel = try XCTUnwrap(keyboard.developmentPanel)
            XCTAssertEqual(keyboard.view.bounds.height, height)
            XCTAssertTrue(requests.isEmpty)
            try XCTUnwrap(panel.chips.first { $0.item.id == "cancel" }).sendActions(for: .touchUpInside)
            XCTAssertNil(keyboard.developmentPanel)
            XCTAssertEqual(keyboard.developmentBufferSource.text, "Only this Buffer text")
            XCTAssertTrue(requests.isEmpty)
            tap(keyboard.developmentPluginControls.run)
            try XCTUnwrap(keyboard.developmentPanel?.chips.first { $0.item.id == "agree" }).sendActions(for: .touchUpInside)
            XCTAssertNil(keyboard.developmentPanel)
            XCTAssertEqual(requests, ["Only this Buffer text"])
        }
    }

    func testAIConsentRejectsChangedTextProviderFieldPluginAndLifecycle() throws {
        for mutation in 0..<6 {
            let (window, keyboard) = host(.qwerty); defer { window.isHidden = true }
            keyboard.developmentAIFullAccess = true
            keyboard.developmentAIProvider(.init(name: "Test service", baseURL: "https://example.com/v1", model: "test"))
            keyboard.developmentPlugin(.polish, source: "Original")
            var sends = 0
            keyboard.developmentAuthorizedAIRequest = { _, _, _ in sends += 1 }
            tap(keyboard.developmentPluginControls.run)
            let staleAgree = try XCTUnwrap(keyboard.developmentPanel?.chips.first { $0.item.id == "agree" })
            switch mutation {
            case 0: keyboard.developmentType("new")
            case 1: keyboard.developmentAIProvider(.init(name: "Other service", baseURL: "https://other.example/v1", model: "test"))
            case 2: keyboard.layoutProxy.documentIdentifier = UUID(); keyboard.textDidChange(nil)
            case 3: keyboard.developmentPlugin(.ask, source: "Original")
            case 4: try keyboard.developmentRevokePlugin(.polish)
            default: keyboard.developmentHostResigned()
            }
            staleAgree.sendActions(for: .touchUpInside)
            XCTAssertEqual(sends, 0, "Stale consent must never authorize changed context: \(mutation)")
            XCTAssertNil(keyboard.presentedViewController)
            XCTAssertNil(keyboard.developmentPanel)
        }
    }

    func testRetiredAIConsentCannotAuthorizeAReplacementPrompt() throws {
        for mutation in 0..<3 {
            let (window, keyboard) = host(.qwerty); defer { window.isHidden = true }
            keyboard.developmentAIFullAccess = true
            keyboard.developmentAIProvider(.init(name: "Test service", baseURL: "https://example.com/v1", model: "test"))
            keyboard.developmentPlugin(.polish, source: "Original")
            var requests: [String] = []
            keyboard.developmentAuthorizedAIRequest = { text, _, _ in requests.append(text) }
            tap(keyboard.developmentPluginControls.run)
            let staleAgree = try XCTUnwrap(keyboard.developmentPanel?.chips.first { $0.item.id == "agree" })
            if mutation == 0 { keyboard.developmentClosePanel() }
            if mutation == 1 { keyboard.developmentPlugin(.polish, source: "Replacement") }
            tap(keyboard.developmentPluginControls.run)
            let newAgree = try XCTUnwrap(keyboard.developmentPanel?.chips.first { $0.item.id == "agree" })
            XCTAssertFalse(staleAgree === newAgree)
            staleAgree.sendActions(for: .touchUpInside)
            XCTAssertTrue(requests.isEmpty)
            newAgree.sendActions(for: .touchUpInside)
            XCTAssertEqual(requests, [mutation == 1 ? "Replacement" : "Original"])
        }
    }

    func testHapticPressIsImmediateAndIndependentOfAudio() {
        let feedback = KeyboardFeedback()
        var time: TimeInterval = 1
        feedback.clock = { time }
        var events: [String] = []
        feedback.onFeedback = { events.append(String(describing: $0)) }
        feedback.playInputClick = { events.append("click") }
        feedback.send(.selection, combination: "AS")
        time = 1.01; feedback.send(.press)
        time = 1.03; feedback.send(.press)
        XCTAssertEqual(events, ["selection", "press", "click", "press", "click"])
        feedback.enabled = false
        time = 1.1; feedback.send(.press)
        XCTAssertEqual(events.last, "click", "Disabling haptics must not disable clicks")
        feedback.soundEnabled = false
        time = 1.2; feedback.send(.press)
        XCTAssertEqual(events.count, 6)
    }

    func testTypingRendersDoNotReadPluginPackages() throws {
        for layout in Layout.allCases {
            var reads = 0
            let (window, keyboard) = host(layout, onBundledRead: { reads += 1 })
            defer { window.isHidden = true }
            for plugin in [nil, KeyboardPlugin.polish] {
                keyboard.developmentPlugin(plugin, source: "")
                keyboard.developmentContent(preedit: "ni", candidates: ["你"])
                reads = 0
                var durations: [Double] = []
                for index in 0..<30 {
                    let start = ProcessInfo.processInfo.systemUptime
                    keyboard.developmentContent(preedit: index.isMultiple(of: 2) ? "ni" : "nih", candidates: ["你"])
                    durations.append((ProcessInfo.processInfo.systemUptime - start) * 1000)
                }
                let sorted = durations.sorted()
                print("IOS_RENDER_BENCH layout=\(layout) plugin=\(plugin?.rawValue ?? "none") reads=\(reads) p95ms=\(sorted[28])")
                XCTAssertEqual(reads, 0, "Typing must not load or hash packages for shortcut appearance: \(layout)")
            }
        }
    }

    func testOrdinaryTypingWorkAcrossBuiltInSchemes() async throws {
        let (window, keyboard) = host(.qwerty); defer { window.isHidden = true }
        keyboard.developmentBuffer(nil)
        for (scheme, text) in [(InputScheme.pinyin, "lupinghenka"), (.ziranma, "lupkhfka"), (.wubi86, "wqivbg")] {
            keyboard.developmentChoose(scheme)
            var durations = [Double]()
            for _ in 0..<10 {
                for character in text {
                    let start = ProcessInfo.processInfo.systemUptime
                    keyboard.developmentType(String(character))
                    window.layoutIfNeeded()
                    durations.append((ProcessInfo.processInfo.systemUptime - start) * 1000)
                }
                keyboard.developmentSpace()
                await keyboard.developmentWaitForDelivery()
            }
            let sorted = durations.sorted()
            print("IOS_TYPING_BENCH scheme=\(scheme.rawValue) samples=\(sorted.count) p50ms=\(sorted[sorted.count / 2]) p95ms=\(sorted[Int(Double(sorted.count - 1) * 0.95)]) maxms=\(sorted.last!)")
            XCTAssertFalse(keyboard.layoutProxy.native.text.isEmpty)
        }
    }

    func testTypingKeepsSettingsMenuAndHiddenBufferStableButRealSettingsChangesRefreshIt() throws {
        let (window, keyboard) = host(.qwerty); defer { window.isHidden = true }
        keyboard.developmentBuffer(nil)
        let menu = try XCTUnwrap(keyboard.layoutViews.settings.menu)
        for character in "nihao" {
            keyboard.developmentType(String(character)); window.layoutIfNeeded()
            XCTAssertTrue(keyboard.layoutViews.settings.menu === menu)
            XCTAssertTrue(keyboard.layoutViews.source.text.isEmpty)
            XCTAssertTrue(keyboard.layoutViews.result.text.isEmpty)
        }
        keyboard.developmentSkin(.rhino)
        let themedMenu = try XCTUnwrap(keyboard.layoutViews.settings.menu)
        XCTAssertFalse(themedMenu === menu)
        XCTAssertEqual((menuEntries(themedMenu).first { $0.title == StatusSkin.rhino.title } as? UIAction)?.state, .on)
        keyboard.developmentChoose(.chord)
        let chordMenu = try XCTUnwrap(keyboard.layoutViews.settings.menu)
        XCTAssertFalse(chordMenu === themedMenu)
        // The layout menu stays (it is how the grid is left); swipe symbols do not apply to chords.
        XCTAssertTrue(menuEntries(chordMenu).contains { $0.title == L("键位布局", "Key layout") })
        XCTAssertFalse(menuEntries(chordMenu).contains { $0.title == L("上滑数字和符号", "Swipe symbols") })
        keyboard.developmentChoose(.pinyin)
        keyboard.developmentOrdinaryAppearance(layout: .nineKey)
        XCTAssertEqual((menuEntries(try XCTUnwrap(keyboard.layoutViews.settings.menu)).first { $0.title == "9 键 · 全拼" } as? UIAction)?.state, .on)
    }

    /// Opt-in: copy a deployed package to the test app's Documents/RIMESTypingBenchmark.
    /// Fixtures and learned data get independent IDs; no installed package is activated.
    func testImportedFlypyTypingWork() async throws {
        let files = FileManager.default
        let fixture = files.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("RIMESTypingBenchmark")
        guard files.fileExists(atPath: fixture.appendingPathComponent("build/double_pinyin_flypy.schema.yaml").path) else {
            throw XCTSkip("Opt-in deployed Flypy package is not installed in the test app")
        }
        let root = files.temporaryDirectory.appendingPathComponent("Flypy-bench-\(UUID())")
        let store = RimeSchemeStore(root: root), id = UUID().uuidString
        let package = RimeSchemePackage(id: id, name: "Flypy benchmark", schemas: [.init(id: "double_pinyin_flypy", name: "Flypy")],
            importedAt: Date(), sourceDigest: String(repeating: "a", count: 64), warnings: [])
        let user = files.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0].appendingPathComponent("RimeImported/\(id)")
        defer { try? files.removeItem(at: root); try? files.removeItem(at: user) }
        try files.createDirectory(at: store.stagingRoot, withIntermediateDirectories: true)
        let staged = store.stagingRoot.appendingPathComponent(id)
        try files.copyItem(at: fixture, to: staged)
        try store.publish(stagedURL: staged, package: package)
        let (window, keyboard) = host(.qwerty); defer { window.isHidden = true }
        keyboard.developmentBuffer(nil)
        let selection = RimeSchemeSelection(packageID: id, schemaID: "double_pinyin_flypy")
        keyboard.developmentChooseImported(selection, store: store)
        XCTAssertEqual(keyboard.developmentImportedSelection, selection)
        var durations = [Double]()
        for _ in 0..<10 {
            for code in ["lupk", "hf", "ka", "nihc", "xtxi", "uijp"] {
                for character in code {
                    let start = ProcessInfo.processInfo.systemUptime
                    keyboard.developmentType(String(character)); window.layoutIfNeeded()
                    durations.append((ProcessInfo.processInfo.systemUptime - start) * 1000)
                }
                if code == "lupk" { XCTAssertTrue(keyboard.layoutViews.candidates.buttons.contains { $0.currentTitle == "录屏" }) }
                keyboard.developmentSpace(); await keyboard.developmentWaitForDelivery()
            }
        }
        let sorted = durations.sorted()
        print("IOS_TYPING_BENCH scheme=flypy samples=\(sorted.count) p50ms=\(sorted[sorted.count / 2]) p95ms=\(sorted[Int(Double(sorted.count - 1) * 0.95)]) maxms=\(sorted.last!)")
        XCTAssertTrue(keyboard.layoutProxy.native.text.contains("录屏"))
        XCTAssertTrue(keyboard.layoutProxy.native.text.contains("你好"))
        XCTAssertNil(store.load().active)
        keyboard.developmentChoose(.pinyin)
    }

    func testDeleteFeedbackPrecedesTextMutation() {
        let (window, keyboard) = host(.qwerty); defer { window.isHidden = true }
        keyboard.developmentChoose(.english)
        keyboard.layoutProxy.native.text = "abc"
        keyboard.layoutProxy.native.selectedRange = NSRange(location: 3, length: 0)
        let feedback = keyboard.layoutViews.keys.feedback
        feedback.enabled = true; feedback.soundEnabled = false; feedback.reset()
        var textAtFeedback: String?
        feedback.onFeedback = { event in
            if event == .press { textAtFeedback = keyboard.layoutProxy.native.text }
        }
        let key = keyboard.developmentDelete
        key.sendActions(for: .touchDown)
        key.sendActions(for: .touchUpInside)
        XCTAssertEqual(textAtFeedback, "abc")
        XCTAssertEqual(keyboard.layoutProxy.native.text, "ab")
    }

    func testEveryLayoutInsertsBufferBlocksBeforePerformingHostReturn() throws {
        for layout in Layout.allCases {
            for theme: StatusSkin in [.apple, .rhino] {
                let (window, keyboard) = host(layout); defer { window.isHidden = true }
                keyboard.developmentSkin(theme)
                keyboard.layoutProxy.returnKeyType = .send
                keyboard.developmentBuffer("")
                let key = try returnKey(keyboard)
                XCTAssertEqual(key.currentTitle, L("发送", "Send"))
                keyboard.developmentBuffer("甲。乙。")
                XCTAssertEqual(key.currentTitle, L("上屏", "Insert"))
                tap(key)
                XCTAssertEqual(keyboard.layoutProxy.insertions, ["甲。"], "\(layout), \(theme)")
                XCTAssertEqual(keyboard.developmentBufferSource.text, "乙。")
                XCTAssertEqual(key.currentTitle, L("上屏", "Insert"))
                tap(key)
                XCTAssertEqual(keyboard.layoutProxy.insertions, ["甲。", "乙。"])
                XCTAssertTrue(keyboard.developmentBufferSource.text.isEmpty)
                XCTAssertEqual(key.currentTitle, L("发送", "Send"))
                tap(key)
                XCTAssertEqual(keyboard.layoutProxy.insertions, ["甲。", "乙。", "\n"])
                keyboard.layoutProxy.returnKeyType = .search; keyboard.developmentBuffer(nil)
                XCTAssertEqual(key.currentTitle, L("搜索", "Search"))
                keyboard.layoutProxy.returnKeyType = .default; keyboard.developmentBuffer(nil)
                XCTAssertEqual(key.currentTitle, L("换行", "return"))
            }
        }
    }

    func testCompositionConfirmationRequiresAnotherTapToInsertTheBuffer() throws {
        for layout in Layout.allCases {
            let (window, keyboard) = host(layout); defer { window.isHidden = true }
            keyboard.layoutProxy.returnKeyType = .send
            keyboard.developmentBuffer("")
            keyboard.developmentType(layout == .nineKey ? "64" : "ni")
            let key = try returnKey(keyboard)
            XCTAssertEqual(key.currentTitle, L("确认", "Confirm"))
            tap(key)
            XCTAssertTrue(keyboard.developmentRaw.isEmpty)
            XCTAssertFalse(keyboard.developmentBufferSource.text.isEmpty)
            XCTAssertTrue(keyboard.layoutProxy.insertions.isEmpty)
            XCTAssertEqual(key.currentTitle, L("上屏", "Insert"))
            let committed = keyboard.developmentBufferSource.text
            tap(key)
            XCTAssertEqual(keyboard.layoutProxy.insertions, [committed])
            XCTAssertEqual(key.currentTitle, L("发送", "Send"))
        }
    }

    func testPluginReturnKeepsRemainingBlocksAndWaitsForCompletedOutput() throws {
        let (window, keyboard) = host(.qwerty); defer { window.isHidden = true }
        keyboard.layoutProxy.returnKeyType = .send
        keyboard.developmentPlugin(.polish, source: "原文", output: "One.Two.", blocks: ["One.", "Two."])
        let key = try returnKey(keyboard)
        tap(key)
        XCTAssertEqual(keyboard.layoutProxy.insertions, ["One."])
        XCTAssertTrue(keyboard.developmentBufferSource.text.isEmpty)
        XCTAssertEqual(key.currentTitle, L("上屏", "Insert"))
        tap(key)
        XCTAssertEqual(keyboard.layoutProxy.insertions, ["One.", "Two."])
        XCTAssertEqual(key.currentTitle, L("发送", "Send"))
        for generating in [false, true] {
            keyboard.developmentPlugin(.polish, source: "不能直接泄漏原文", generating: generating)
            XCTAssertFalse(key.isEnabled)
            if generating { keyboard.developmentPreview("partial") }
            keyboard.developmentEnter()
            XCTAssertEqual(keyboard.layoutProxy.insertions, ["One.", "Two."])
            XCTAssertEqual(keyboard.developmentBufferSource.text, "不能直接泄漏原文")
        }
    }

    func testCompletedPluginResultCannotSurviveDisableAndReenable() throws {
        let (window, keyboard) = host(.qwerty); defer { window.isHidden = true }
        keyboard.developmentPlugin(.polish, source: "保留原文", output: "Old result")
        let key = try returnKey(keyboard)
        key.sendActions(for: .touchDown)
        try keyboard.developmentRevokePlugin(.polish)
        key.sendActions(for: .touchUpInside)
        keyboard.developmentEnter()
        XCTAssertTrue(keyboard.layoutProxy.insertions.isEmpty)
        XCTAssertEqual(keyboard.developmentBufferSource.text, "保留原文")
        keyboard.developmentPlugin(.polish, source: "新请求", output: "Fresh result")
        tap(key)
        XCTAssertEqual(keyboard.layoutProxy.insertions, ["Fresh result"])
    }

    func testHeldReturnCannotTurnIntoSendAfterAnotherActionDrainsBuffer() throws {
        let (window, keyboard) = host(.orthogonal); defer { window.isHidden = true }
        keyboard.layoutProxy.returnKeyType = .send
        keyboard.developmentBuffer("保留发送边界")
        let key = try returnKey(keyboard)
        key.sendActions(for: .touchDown)
        XCTAssertTrue(keyboard.layoutViews.insert.accessibilityActivate())
        XCTAssertEqual(keyboard.layoutProxy.insertions, ["保留发送边界"])
        key.sendActions(for: .touchUpInside)
        XCTAssertEqual(keyboard.layoutProxy.insertions, ["保留发送边界"])
        tap(key)
        XCTAssertEqual(keyboard.layoutProxy.insertions, ["保留发送边界", "\n"])
        keyboard.developmentBuffer("旧输入框内容")
        key.sendActions(for: .touchDown)
        keyboard.layoutProxy.documentIdentifier = UUID()
        key.sendActions(for: .touchUpInside)
        XCTAssertEqual(keyboard.layoutProxy.insertions, ["保留发送边界", "\n"])
        XCTAssertEqual(keyboard.developmentBufferSource.text, "旧输入框内容")
    }

    func testNativeThemeUsesDotIncludingLegacySelection() throws {
        let window = UIWindow(frame: CGRect(x: 0, y: 0, width: 100, height: 100))
        window.rootViewController = UIViewController(); window.makeKeyAndVisible()
        defer { window.isHidden = true }
        let indicator = StatusLight(frame: CGRect(x: 0, y: 0, width: 40, height: 40))
        window.rootViewController!.view.addSubview(indicator)
        for skin: StatusSkin in [.apple, .light] {
            indicator.skin = skin; indicator.layoutIfNeeded()
            let dot = try XCTUnwrap(indicator.layer.sublayers?.first { $0.name == "status.dot" })
            XCTAssertFalse(dot.isHidden)
            XCTAssertEqual(dot.bounds.size, CGSize(width: 10, height: 10))
            for state: StatusLight.State in [.idle, .waiting, .streaming, .ready, .failed] {
                indicator.set(state)
                XCTAssertFalse(dot.isHidden)
                XCTAssertEqual(dot.backgroundColor, state.color.resolvedColor(with: indicator.traitCollection).cgColor)
            }
            XCTAssertTrue(indicator.subviews.allSatisfy(\.isHidden))
        }
        indicator.skin = .rhino
        XCTAssertEqual(indicator.layer.sublayers?.first { $0.name == "status.dot" }?.isHidden, true)
    }

    func testMenusNameBufferAndKeepIconsWithoutExposingChordLayout() throws {
        let (window, keyboard) = host(.split); defer { window.isHidden = true }
        let originalLayout = keyboard.layoutViews.keys.chordLayout
        let gear = try XCTUnwrap(keyboard.layoutViews.settings.menu)
        let entries = menuEntries(gear)
        XCTAssertFalse(entries.contains { ["Chord layout", "并击布局"].contains($0.title) })
        for title in [L("繁体输出", "Traditional Chinese output"), L("按键震动", "Key haptics")] {
            XCTAssertNotNil(try XCTUnwrap(entries.first { $0.title == title }).image)
        }
        let pluginButton = keyboard.developmentPluginControls.plugin
        let buffer = try XCTUnwrap(pluginButton.menu?.children.first as? UIAction)
        XCTAssertEqual(buffer.title, "Buffer"); XCTAssertNotNil(buffer.image)
        XCTAssertEqual(pluginButton.accessibilityLabel, "Buffer"); XCTAssertNotNil(pluginButton.image(for: .normal))
        keyboard.developmentOpenSettings()
        let chip = try XCTUnwrap(keyboard.developmentPanel?.chips.first { $0.item.id == "default" })
        XCTAssertEqual(chip.accessibilityLabel, "Buffer"); XCTAssertNotNil(chip.icon.image)
        XCTAssertEqual(keyboard.layoutViews.keys.chordLayout, originalLayout)
    }

    private func returnKey(_ keyboard: KeyboardViewController) throws -> KeycapButton {
        try XCTUnwrap(keyboard.developmentStandardFunctions[.enter] as? KeycapButton)
    }
    private func tap(_ key: KeycapButton) { key.sendActions(for: .touchDown); key.sendActions(for: .touchUpInside) }
    private func menuEntries(_ menu: UIMenu) -> [UIMenuElement] {
        menu.children.flatMap { element in [element] + ((element as? UIMenu).map(menuEntries) ?? []) }
    }
    private func host(_ layout: Layout, onBundledRead: (() -> Void)? = nil) -> (UIWindow, KeyboardViewController) {
        let window = UIWindow(frame: CGRect(x: 0, y: 0, width: 393, height: 900))
        let parent = UIViewController(); window.rootViewController = parent; window.makeKeyAndVisible()
        let keyboard = KeyboardViewController(); keyboard.layoutNeedsInputModeSwitchKey = false
        keyboard.loadViewIfNeeded(); keyboard.developmentResetPreferences(bundledData: { entry in
            onBundledRead?(); return try OfficialPluginCatalog.bundledData(entry)
        })
        parent.addChild(keyboard); parent.view.addSubview(keyboard.view); keyboard.didMove(toParent: parent)
        switch layout {
        case .qwerty: keyboard.developmentOrdinaryAppearance(layout: .qwerty)
        case .nineKey: keyboard.developmentOrdinaryAppearance(layout: .nineKey)
        case .custom: keyboard.developmentSetCustomLayout(CustomKeyboardLayout.templates[0])
        case .orthogonal: keyboard.developmentChoose(.chord); keyboard.developmentSetLayout(.orthogonal)
        case .split: keyboard.developmentChoose(.chord); keyboard.developmentSetLayout(.splitOrthogonal)
        }
        parent.view.layoutIfNeeded(); keyboard.view.layoutIfNeeded()
        return (window, keyboard)
    }
}
