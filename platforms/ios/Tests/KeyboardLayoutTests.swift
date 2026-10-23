import XCTest
import UIKit
import RimesCore
@testable import RIMES

typealias DocumentIdentity = RIMES.DocumentIdentity
typealias MobileEngine = RIMES.MobileEngine
typealias ProxyTextDelivery = RIMES.ProxyTextDelivery
typealias AppConfiguration = RIMES.AppConfiguration
typealias ConfigurationStore = RIMES.ConfigurationStore
typealias KeychainStore = RIMES.KeychainStore
typealias KeyboardPreferenceStore = RIMES.KeyboardPreferenceStore
typealias AppleTranslationPlugin = RIMES.AppleTranslationPlugin
typealias AITextPlugin = RIMES.AITextPlugin
func L(_ zh: String, _ en: String) -> String { RIMES.L(zh, en) }

@MainActor final class KeyboardLayoutTests: XCTestCase {
    func testNativeKeyboardLayoutsAndSnapshots() async throws {
        let variants: [(String, CGFloat, Bool, Bool, Bool, UIUserInterfaceStyle)] = [
            ("portrait", 393, false, false, false, .light),
            ("swipe-symbols", 393, false, false, false, .light),
            ("swipe-symbols-landscape", 852, false, false, false, .dark),
            ("buffer", 393, true, false, true, .light),
            ("buffer-expanded", 393, true, true, true, .light),
            ("narrow-dark", 320, true, false, true, .dark),
            ("landscape", 852, true, true, true, .dark),
            ("idle", 393, false, false, false, .light),
            ("buffer-empty", 393, true, false, false, .light),
            ("long-text", 320, true, true, false, .light),
            ("landscape-idle", 852, false, false, false, .dark),
            ("chord-idle", 393, false, false, true, .light),
            ("emoji", 320, false, false, true, .light),
            ("default-blocks", 393, true, false, true, .light),
            ("default-blocks-narrow", 320, true, false, true, .dark),
            ("default-blocks-landscape", 852, true, false, true, .dark)
        ]
        for (name, width, buffer, expanded, chord, style) in variants {
            let controller = KeyboardViewController(); controller.layoutNeedsInputModeSwitchKey = false
            // Snapshots describe built-in layouts even if an imported scheme or
            // custom layout is currently selected in the simulator's real app.
            controller.loadViewIfNeeded(); controller.developmentResetPreferences()
            controller.layoutProxy.keyboardAppearance = style == .dark ? .dark : .light
            controller.overrideUserInterfaceStyle = style
            let window = UIWindow(frame: CGRect(x: 0, y: 0, width: width, height: 900))
            window.overrideUserInterfaceStyle = style
            let parent = UIViewController(); window.rootViewController = parent; window.makeKeyAndVisible()
            parent.addChild(controller); parent.view.addSubview(controller.view); controller.didMove(toParent: parent)
            controller.view.translatesAutoresizingMaskIntoConstraints = false
            controller.view.tintColor = .systemTeal
            NSLayoutConstraint.activate([controller.view.leadingAnchor.constraint(equalTo: parent.view.leadingAnchor), controller.view.topAnchor.constraint(equalTo: parent.view.topAnchor), controller.view.widthAnchor.constraint(equalToConstant: width)])
            parent.view.layoutIfNeeded()
            defer { window.isHidden = true }
            try await Task.sleep(nanoseconds: 80_000_000)
            controller.overrideUserInterfaceStyle = style
            controller.view.overrideUserInterfaceStyle = style
            let (bufferView, candidates, keys) = controller.developmentLayout(bufferText: buffer ? "你好，这是一段用于检查空间布局的原文。" : nil, expanded: expanded, chord: chord)
            XCTAssertEqual(keys.chordMode, chord, name)
            XCTAssertFalse(keys.usesCustomLayout, name)
            if name.hasPrefix("swipe-symbols") { controller.developmentSwipeSymbols(true) }
            if name.hasSuffix("idle") || name == "emoji" { controller.developmentContent() }
            if name == "buffer-empty" { controller.developmentContent(); controller.developmentBuffer("") }
            if name.hasPrefix("default-blocks") {
                controller.developmentBuffer("第一句。第二句！这是正在编辑的第三块", plugin: false)
                controller.developmentContent()
            }
            if name == "long-text" {
                controller.developmentBuffer(String(repeating: "长原文内容。", count: 100), plugin: true, output: String(repeating: "A longer translation preview. ", count: 100))
                controller.developmentContent(preedit: "changhouxuan", candidates: ["长候选词", "你好", "世界", "用来验证长候选文字不会缩小的句子", "测试", "一", "二", "三", "四", "五"])
            }
            let height = try XCTUnwrap(controller.view.constraints.first { $0.firstItem === controller.view && $0.firstAttribute == .height && $0.secondItem == nil }).constant
            XCTAssertGreaterThan(height, 140)
            parent.view.setNeedsLayout(); parent.view.layoutIfNeeded(); controller.view.layoutIfNeeded()
            XCTAssertEqual(controller.view.traitCollection.userInterfaceStyle, style, name)
            XCTAssertEqual(keys.traitCollection.userInterfaceStyle, style, name)
            let keyFrame = keys.convert(keys.bounds, to: controller.view)
            let candidateFrame = candidates.convert(candidates.bounds, to: controller.view)
            XCTAssertGreaterThanOrEqual(keyFrame.height, chord ? 80 : 100, name)
            if !candidates.isHidden { XCTAssertLessThanOrEqual(candidateFrame.maxY, keyFrame.minY, name) }
            if buffer {
                let bufferFrame = bufferView.convert(bufferView.bounds, to: controller.view)
                XCTAssertLessThanOrEqual(bufferFrame.maxY, candidates.isHidden ? keyFrame.minY : candidateFrame.minY, name)
                XCTAssertLessThanOrEqual(bufferFrame.height, 140, name)
            }
            XCTAssertLessThanOrEqual(keyFrame.maxX, width, name)
            if name == "emoji" {
                XCTAssertTrue(try XCTUnwrap(keys.subviews.first { $0.accessibilityIdentifier == "keyboard.emoji" }).accessibilityActivate())
            } else if chord && name != "chord-idle" { keys.developmentPress("a", end: "s") }
            controller.view.layoutIfNeeded()
            try await Task.sleep(nanoseconds: 80_000_000)
            let renderer = UIGraphicsImageRenderer(bounds: controller.view.bounds)
            let image = renderer.image { _ in controller.view.drawHierarchy(in: controller.view.bounds, afterScreenUpdates: true) }
            let attachment = XCTAttachment(image: image); attachment.name = name; attachment.lifetime = .keepAlways; add(attachment)
            let path = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("build22-keyboard-\(name).png")
            try image.pngData()?.write(to: path)
        }
    }
}

@MainActor final class LayoutTestDocumentProxy: NSObject, UITextDocumentProxy {
    let native = UITextView()
    var keyboardAppearance: UIKeyboardAppearance = .default
    var returnKeyType: UIReturnKeyType = .default
    var documentIdentifier = UUID()
    var identityAvailable = true
    override func responds(to selector: Selector!) -> Bool {
        if selector == #selector(getter: UITextDocumentProxy.documentIdentifier), !identityAvailable { return false }
        return super.responds(to: selector)
    }
    var onProxyWrite: (() -> Void)?
    var insertions: [String] = []
    var markedUpdates: [String] = []
    var contextLimit: Int?
    var ignoresDeletion = false
    var deletionDelay: TimeInterval = 0
    var optimisticContext: UITextView?
    var cursorDelay: TimeInterval = 0
    var acknowledgesCursor = true
    private var context: UITextView { optimisticContext ?? native }
    var documentContextBeforeInput: String? {
        let text = String((context.text as NSString).substring(to: context.selectedRange.location))
        return contextLimit.map { String(text.suffix($0)) } ?? text
    }
    var documentContextAfterInput: String? {
        let text = String((context.text as NSString).substring(from: NSMaxRange(context.selectedRange)))
        return contextLimit.map { String(text.prefix($0)) } ?? text
    }
    var selectedText: String? {
        guard context.selectedRange.length != 0 else { return nil }
        let text = (context.text as NSString).substring(with: context.selectedRange)
        return contextLimit.map { String(text.prefix($0)) } ?? text
    }
    var documentInputMode: UITextInputMode? { nil }
    var hasText: Bool { native.hasText }
    func insertText(_ text: String) { insertions.append(text); native.insertText(text); onProxyWrite?() }
    func deleteBackward() {
        guard !ignoresDeletion else { return }
        if deletionDelay > 0 { DispatchQueue.main.asyncAfter(deadline: .now() + deletionDelay) { self.native.deleteBackward(); self.onProxyWrite?() } }
        else { native.deleteBackward(); onProxyWrite?() }
    }
    func adjustTextPosition(byCharacterOffset offset: Int) {
        func move(_ view: UITextView) {
            view.selectedRange = NSRange(location: max(0, min(view.text.utf16.count, view.selectedRange.location + offset)), length: 0)
        }
        if let optimisticContext { move(optimisticContext) }
        if cursorDelay > 0 {
            DispatchQueue.main.asyncAfter(deadline: .now() + cursorDelay) {
                move(self.native); self.optimisticContext = nil
                if self.acknowledgesCursor { self.onProxyWrite?() }
            }
        } else { move(native); if acknowledgesCursor { onProxyWrite?() } }
    }
    func setMarkedText(_ markedText: String, selectedRange: NSRange) { markedUpdates.append(markedText); native.setMarkedText(markedText, selectedRange: selectedRange); onProxyWrite?() }
    func unmarkText() { native.unmarkText(); onProxyWrite?() }
}

@MainActor final class KeyboardInteractionTests: XCTestCase {
    func testRawEnterShiftAndLanguageRoutingInHostAndBuffer() async throws {
        for buffered in [false, true] {
            let (window, controller) = host(); defer { window.isHidden = true }
            try await Task.sleep(nanoseconds: 80_000_000)
            controller.developmentChoose(.chord); controller.developmentSetLayout(.splitOrthogonal)
            if buffered { controller.developmentBuffer("") }
            func text() -> String { buffered ? controller.developmentBufferSource.text : controller.layoutProxy.native.text }
            controller.developmentChord("ni'hao'")
            XCTAssertFalse(controller.developmentRaw.isEmpty)
            controller.developmentEnter()
            await controller.developmentWaitForDelivery()
            XCTAssertEqual(text(), "nihao"); XCTAssertTrue(controller.developmentRaw.isEmpty)
            XCTAssertNil(controller.layoutProxy.native.markedTextRange)
            controller.developmentEnter()
            await controller.developmentWaitForDelivery()
            let prefix = buffered ? "" : "nihao\n"
            XCTAssertEqual(text(), prefix)
            if buffered { XCTAssertEqual(controller.layoutProxy.native.text, "nihao") }
            controller.developmentType("ni")
            controller.developmentShift()
            await controller.developmentWaitForDelivery()
            XCTAssertEqual(text(), prefix + "ni")
            XCTAssertFalse(controller.layoutViews.keys.resolvesChords)
            XCTAssertEqual(controller.developmentShiftState, "locked")
            controller.developmentType("a"); controller.developmentType("b")
            await controller.developmentWaitForDelivery(); XCTAssertEqual(text(), prefix + "niAB")
            XCTAssertEqual(controller.developmentShiftState, "locked")
            controller.developmentShift(); XCTAssertTrue(controller.layoutViews.keys.resolvesChords)
            controller.developmentType("hao"); controller.developmentLanguage()
            await controller.developmentWaitForDelivery()
            XCTAssertEqual(text(), prefix + "niABhao")
            XCTAssertEqual(controller.layoutViews.keys.chordLayout, .splitOrthogonal)
            XCTAssertFalse(controller.layoutViews.keys.resolvesChords)
            controller.developmentType("test"); await controller.developmentWaitForDelivery(); XCTAssertTrue(text().hasSuffix("haotest"))
            controller.developmentLanguage(); XCTAssertTrue(controller.layoutViews.keys.resolvesChords)
            controller.developmentType("nihk"); controller.developmentSpace()
            await controller.developmentWaitForDelivery()
            XCTAssertTrue(text().hasSuffix("你好"))
            controller.developmentShift(); controller.developmentChoose(.wubi86)
            XCTAssertFalse(controller.layoutViews.keys.shifted)
            controller.developmentType("wq"); controller.developmentEnter()
            await controller.developmentWaitForDelivery()
            XCTAssertTrue(text().hasSuffix("你好wq"))
        }
    }
    func testOrdinaryTapsInGuttersSnapToNearestCapButNeverStartChords() throws {
        let keys = KeySurface(frame: .init(x: 0, y: 0, width: 383, height: 150))
        keys.layoutIfNeeded()
        var frames = keys.developmentKeyFrames
        let q = try XCTUnwrap(frames["q"]), w = try XCTUnwrap(frames["w"]), a = try XCTUnwrap(frames["a"])
        // Horizontal gutter between Q and W, just right of Q.
        XCTAssertNil(keys.developmentKey(at: CGPoint(x: q.maxX + 0.5, y: q.midY)))
        XCTAssertEqual(keys.developmentOrdinaryKey(at: CGPoint(x: q.maxX + 0.5, y: q.midY)), "q")
        XCTAssertEqual(keys.developmentOrdinaryKey(at: CGPoint(x: w.minX - 0.5, y: w.midY)), "w")
        // Vertical gutter between rows resolves to the closer row.
        XCTAssertEqual(keys.developmentOrdinaryKey(at: CGPoint(x: a.midX, y: a.minY - 0.5)), "a")
        // The blank staggered margin left of Z stays dead.
        let z = try XCTUnwrap(frames["z"])
        XCTAssertNil(keys.developmentOrdinaryKey(at: CGPoint(x: z.minX - KeySurface.gapTolerance - 4, y: z.midY)))
        // Chord resolution keeps strict cap hit testing.
        keys.chordMode = true; keys.layoutIfNeeded(); frames = keys.developmentKeyFrames
        let t = try XCTUnwrap(frames["t"]), y = try XCTUnwrap(frames["y"])
        let gutter = CGPoint(x: (t.maxX + y.minX) / 2, y: t.midY)
        if !t.contains(gutter) && !y.contains(gutter) { XCTAssertNil(keys.developmentOrdinaryKey(at: gutter)) }
        keys.shifted = true
        XCTAssertEqual(keys.developmentOrdinaryKey(at: CGPoint(x: t.maxX + 0.25, y: t.midY)), "t")
    }
    func testExpandedCandidatesCollapseAfterSelectionOrTypingAndStayCollapsed() {
        let (window, controller) = host(); defer { window.isHidden = true }
        let strip = controller.layoutViews.candidates
        func expand() { strip.expandButton.sendActions(for: .touchUpInside); controller.view.layoutIfNeeded(); XCTAssertTrue(strip.expanded) }
        controller.developmentType("nihao"); XCTAssertFalse(strip.buttons.isEmpty)
        let singleRow = strip.bounds.height
        expand(); XCTAssertGreaterThan(strip.bounds.height, singleRow)
        strip.buttons[0].sendActions(for: .touchUpInside); controller.view.layoutIfNeeded()
        XCTAssertFalse(strip.expanded)
        // The next composition opens as a single row.
        controller.developmentType("nihao"); controller.view.layoutIfNeeded()
        XCTAssertFalse(strip.expanded); XCTAssertEqual(strip.bounds.height, singleRow, accuracy: 0.5)
        // Typing while the expanded list is open also collapses it.
        expand(); controller.layoutViews.keys.onTypingPress?(); controller.view.layoutIfNeeded()
        XCTAssertFalse(strip.expanded)
        expand(); controller.developmentSpace(); XCTAssertTrue(strip.expanded) // hook bypasses the key press
    }
    func testPoemLinesShowSideBySideAndSendOneLineEachTap() {
        let (window, controller) = host(); defer { window.isHidden = true }
        controller.developmentBuffer("春眠不觉", plugin: true, output: "春眠不觉晓，\n处处闻啼鸟。\n夜来风雨声，\n花落知多少。"); window.layoutIfNeeded()
        let line = controller.layoutViews.result
        XCTAssertEqual(line.text, "春眠不觉晓，处处闻啼鸟。夜来风雨声，花落知多少。")
        line.layoutIfNeeded()
        let frames = line.blockFrames
        XCTAssertEqual(frames.count, 4)
        for (left, right) in zip(frames, frames.dropFirst()) { XCTAssertLessThan(left.maxX, right.minX + 0.5) }
        XCTAssertEqual(frames.dropLast().map(\.width).max()! - frames.dropLast().map(\.width).min()!, 0, accuracy: 1) // same-length lines, same-size blocks
        let image = UIGraphicsImageRenderer(bounds: controller.view.bounds).image { context in controller.view.layer.render(in: context.cgContext) }
        try? image.pngData()?.write(to: FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("keyboard-poem-blocks.png"))
        line.contentOffset.x = line.contentSize.width - line.bounds.width
        let end = UIGraphicsImageRenderer(bounds: line.convert(line.bounds, to: controller.view)).image { context in controller.view.layer.render(in: context.cgContext) }
        try? end.pngData()?.write(to: FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("keyboard-poem-blocks-end.png"))
        line.contentOffset.x = 0
        XCTAssertGreaterThan(line.contentSize.width, line.bounds.width) // overflow scrolls sideways
        XCTAssertEqual(line.contentOffset.x, 0)
        // A tap on a block (or in the gap next to it) picks that block, for reading aloud.
        XCTAssertEqual(line.blockIndex(at: frames[2].midX), 2)
        XCTAssertEqual(line.blockIndex(at: (frames[0].maxX + frames[1].minX) / 2 + 1), 1)
        XCTAssertNotNil(line.onTapBlock)
        let button = controller.layoutViews.insert
        for _ in 0..<3 { button.onPressBegan?(); button.onInsert?(.next) }
        XCTAssertEqual(controller.layoutProxy.insertions, ["春眠不觉晓，\n", "处处闻啼鸟。\n", "夜来风雨声，\n"])
        XCTAssertEqual(line.text, "花落知多少。")
    }
    func testLineBreaksInTheInputLineStayOnOneLine() {
        let (window, controller) = host(); defer { window.isHidden = true }
        controller.developmentBuffer("第一行\n第二行"); window.layoutIfNeeded()
        XCTAssertEqual(controller.layoutViews.source.attributedText?.string, "第一行↵第二行")
    }
    func testTranslationComesBackInBlocksEvenWithoutSourcePunctuation() {
        // "今天天气很好我们去公园吧" has no punctuation; its translation does.
        let english = AppleTranslationPlugin.outputBlocks("The weather is nice today, let's go to the park.", source: "今天天气很好我们去公园吧", last: false, spaced: true)
        XCTAssertGreaterThan(english.count, 1)
        XCTAssertEqual(english.joined(), "The weather is nice today, let's go to the park. ")
        let korean = AppleTranslationPlugin.outputBlocks("오늘 날씨가 좋네요, 공원에 가요.", source: "今天天气很好\n", last: true, spaced: true)
        XCTAssertEqual(korean, ["오늘 날씨가 좋네요, ", "공원에 가요.\n"])
        XCTAssertEqual(AppleTranslationPlugin.outputBlocks("你好。", source: "Hello.", last: false, spaced: false), ["你好。"])
        // Korean without punctuation is cut every three words.
        let long = AppleTranslationPlugin.outputBlocks("나는 오늘 아침에 공원에서 친구를 만났어요", source: "我今天早上在公园见到了朋友", last: true, spaced: true)
        XCTAssertEqual(long, ["나는 오늘 아침에 ", "공원에서 친구를 만났어요"])
    }
    func testEmptyBufferPassesDeleteAndReturnToTheApp() throws {
        let (window, controller) = host(); defer { window.isHidden = true }
        controller.layoutProxy.native.text = "你好"
        let returnKey = try XCTUnwrap(controller.developmentStandardFunctions[.enter] as? KeycapButton)
        let send = controller.layoutViews.insert
        controller.developmentBuffer(""); window.layoutIfNeeded()
        // Empty Buffer: Return is the lit sending key and goes to the app; Delete edits the app.
        XCTAssertTrue(returnKey.isSelected); XCTAssertFalse(send.isSelected)
        controller.developmentBackspace(); XCTAssertEqual(controller.layoutProxy.native.text, "你")
        controller.developmentEnter(); XCTAssertEqual(controller.layoutProxy.insertions.last, "\n")
        XCTAssertEqual(controller.developmentBufferSource.text, "")
        // With Buffer text, Return and the upper insert key both insert a block.
        controller.developmentBuffer("原文"); window.layoutIfNeeded()
        XCTAssertTrue(returnKey.isSelected); XCTAssertTrue(send.isSelected)
        let before = controller.layoutProxy.native.text ?? ""
        controller.developmentEnter(); XCTAssertEqual(controller.developmentBufferSource.text, "")
        let host = before + "原文"
        XCTAssertEqual(controller.layoutProxy.native.text, host)
        XCTAssertTrue(returnKey.isSelected)
        // A held Delete that began in the Buffer stops when the Buffer empties.
        controller.developmentBuffer("字"); window.layoutIfNeeded()
        let delete = controller.developmentDelete
        XCTAssertEqual(delete.onPressBegan?(), true)
        XCTAssertEqual(delete.onDelete?(), true); XCTAssertEqual(controller.developmentBufferSource.text, "")
        XCTAssertEqual(delete.onDelete?(), false); XCTAssertEqual(controller.layoutProxy.native.text, host)
        // Buffer off: unchanged, Return is a plain key.
        controller.developmentBuffer(nil); window.layoutIfNeeded(); XCTAssertFalse(returnKey.isSelected)
    }
    func testDictatedTextMovesIntoTheBuffer() throws {
        let (window, controller) = host(); defer { window.isHidden = true }
        controller.layoutProxy.native.text = "已有文字"
        controller.developmentBuffer(""); window.layoutIfNeeded()
        // The system writes dictation straight into the app, bypassing the keyboard.
        controller.layoutProxy.native.insertText("语音输入的内容")
        controller.developmentHostTextChanged()
        XCTAssertTrue(controller.developmentCapturePending)
        Thread.sleep(forTimeInterval: 0.7) // past this keyboard's own-write window
        controller.developmentCaptureHostText()
        XCTAssertEqual(controller.developmentBufferSource.text, "语音输入的内容")
        XCTAssertEqual(controller.layoutProxy.native.text, "已有文字")
        // Buffer off: dictation stays in the app.
        controller.developmentBuffer(nil); controller.layoutProxy.native.insertText("再说一句")
        controller.developmentHostTextChanged(); XCTAssertFalse(controller.developmentCapturePending)
        XCTAssertEqual(controller.layoutProxy.native.text, "已有文字再说一句")
        XCTAssertEqual(KeyboardViewController.appendedText(before: "……很长的上文结尾在这里", after: "长的上文结尾在这里新增"), "新增")
        XCTAssertNil(KeyboardViewController.appendedText(before: "完全不同的上文内容哈哈哈哈", after: "别的"))
    }
    func testTypingReadoutKeepsMovingAndSignsTheApp() throws {
        let (window, controller) = host(); defer { window.isHidden = true }
        var clock: TimeInterval = 100
        controller.defaultClockNow = { clock }
        controller.layoutProxy.native.text = "正文"; controller.layoutProxy.native.selectedRange = NSRange(location: 0, length: 0)
        controller.developmentBuffer(""); window.layoutIfNeeded()
        let stats = try XCTUnwrap(controller.layoutViews.result.subviews.first { $0.accessibilityIdentifier == "keyboard.buffer.metrics" } as? UILabel)
        XCTAssertNil(controller.typingSignature()) // nothing typed yet
        for step in 0..<6 { clock = 100 + Double(step); controller.developmentType("ni"); controller.developmentSpace() }
        let typing = stats.text ?? ""
        // Paused: speed keeps falling and the pause counts up, without any key press.
        clock += 3; controller.developmentRefreshTypingStats()
        let paused = stats.text ?? ""
        XCTAssertNotEqual(typing, paused)
        XCTAssertTrue(paused.contains("3s"), paused)
        // The ordinary-text action appends the figures and localized product credit.
        let signature = try XCTUnwrap(controller.typingSignature())
        XCTAssertTrue(signature.contains("\(controller.developmentTypingSession.characters)"))
        XCTAssertTrue(signature.hasSuffix(L("来自 \(RIMES.ProductBrand.name) 免费开源输入法", "Sent from \(RIMES.ProductBrand.name), the free open-source input method")))
        controller.developmentBuffer(""); window.layoutIfNeeded() // empty Buffer, same session
        controller.developmentAppendSignature()
        XCTAssertEqual(controller.layoutProxy.native.text, "正文\n\n" + signature)
        // Once sent, the figures start over.
        XCTAssertTrue(controller.developmentTypingSession.isEmpty); XCTAssertTrue(controller.developmentTypingMetrics.isEmpty)
        XCTAssertNil(controller.typingSignature())
        XCTAssertFalse(stats.text?.contains("s") == true && stats.text?.contains(L("停", "idle")) == true)
    }
    func testOptionalImagePreviewPreservesTypingAndPausesAutoInsertion() throws {
        let (window, controller) = host(); defer { window.isHidden = true }
        var clock: TimeInterval = 100; controller.defaultClockNow = { clock }
        controller.developmentBuffer("")
        for step in 0..<6 { clock = 100 + Double(step); controller.developmentType("ni"); controller.developmentSpace() }
        controller.developmentAutoDelay(1); window.layoutIfNeeded()
        let source = controller.developmentBufferSource.text, totals = controller.developmentTypingSession
        let height = controller.view.bounds.height
        controller.developmentCardFullAccess = false
        controller.layoutViews.result.onTapBackground?(); window.layoutIfNeeded()
        let preview = try XCTUnwrap(controller.view.subviews.first { $0.accessibilityIdentifier == "keyboard.typingCard.preview" })
        let picker = try XCTUnwrap(preview.subviews.first { $0.accessibilityIdentifier == "keyboard.typingCard.formats" } as? UISegmentedControl)
        XCTAssertEqual(picker.numberOfSegments, 3)
        XCTAssertEqual(picker.selectedSegmentIndex, TypingCardPreview.Format.blocks.rawValue)
        let menu = try XCTUnwrap(controller.layoutViews.settings.menu)
        XCTAssertFalse(menu.children.contains { ($0 as? UIAction)?.title == L("统计图片（可选）", "Stats image (optional)") })
        let data = try XCTUnwrap(controller.developmentTypingCardPNG)
        try TypingCardStore.validate(data)
        XCTAssertEqual(controller.view.bounds.height, height)
        for _ in 0..<5 { clock += 1; controller.developmentAutoTick() }
        XCTAssertEqual(controller.developmentBufferSource.text, source)
        XCTAssertEqual(controller.developmentTypingSession, totals)
        XCTAssertTrue(controller.layoutProxy.insertions.isEmpty)
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: directory) }
        let store = TypingCardStore(directory: directory)
        controller.developmentSaveTypingCard(store: store); XCTAssertNil(try store.load())
        controller.developmentCardFullAccess = true
        controller.developmentSaveTypingCard(store: store); XCTAssertEqual(try store.load(), data)
        controller.developmentCloseTypingCard(); XCTAssertNil(controller.developmentTypingCardPNG)
        XCTAssertEqual(controller.developmentTypingSession, totals)
        controller.developmentAutoTick(); clock += 2; controller.developmentAutoTick()
        XCTAssertNotEqual(controller.developmentBufferSource.text, source)
    }
    func testStatsTapPreviewsBeforeExplicitMatrixInsertionWithoutFullAccess() throws {
        let (window, controller) = host(); defer { window.isHidden = true }
        controller.developmentCardFullAccess = false
        controller.layoutProxy.native.text = "正文"; controller.developmentBuffer("")
        controller.developmentType("ni"); controller.developmentSpace()
        let expected = TypingStatsText.matrix(try XCTUnwrap(TypingCardSnapshot(session: controller.developmentTypingSession)))
        controller.layoutViews.result.onTapBackground?(); window.layoutIfNeeded()
        XCTAssertEqual(controller.layoutProxy.native.text, "正文")
        XCTAssertFalse(controller.developmentTypingSession.isEmpty)
        func find(_ view: UIView) -> UIButton? {
            if view.accessibilityIdentifier == "keyboard.typingCard.primary" { return view as? UIButton }
            return view.subviews.lazy.compactMap(find).first
        }
        let insert = try XCTUnwrap(find(controller.view)); XCTAssertTrue(insert.isEnabled)
        insert.sendActions(for: .touchUpInside)
        XCTAssertEqual(controller.layoutProxy.native.text, "正文\n\n" + expected)
        XCTAssertTrue(controller.developmentTypingSession.isEmpty)
        XCTAssertNil(controller.developmentTypingCardPNG)
    }
    func testNotoPetsDecodeAsSmallLoops() throws {
        // Every bundled pet is a small animated loop; every Noto skin has its file.
        let folder = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().appendingPathComponent("Resources/Pets")
        for skin in StatusLight.Skin.allCases where skin.isNoto {
            let url = folder.appendingPathComponent(skin.rawValue + ".png")
            let image = try XCTUnwrap(NotoPet.image(at: url), skin.rawValue)
            let frames = try XCTUnwrap(image.images, skin.rawValue)
            XCTAssertGreaterThan(frames.count, 10); XCTAssertLessThanOrEqual(frames.count, 60)
            XCTAssertLessThanOrEqual(frames[0].size.width * frames[0].scale, 96)
            XCTAssertGreaterThan(image.duration, 0.5)
        }
        let files = try FileManager.default.contentsOfDirectory(atPath: folder.path).filter { $0.hasSuffix(".png") }
        let rhino = ["stand", "walk", "sit", "run", "cheer", "wait", "jump", "sigh"].map { "rhino-\($0).png" }
        XCTAssertEqual(Set(files), Set(StatusLight.Skin.allCases.filter(\.isNoto).map { $0.rawValue + ".png" } + rhino))
        // The rhino's actions are cropped to one size: 96 px cells, 5–12 frames each.
        for name in rhino {
            let clip = try XCTUnwrap(NotoPet.image(at: folder.appendingPathComponent(name)), name)
            XCTAssertEqual(clip.images?.first.map { $0.size.width * $0.scale }, 96, name)
            XCTAssertTrue((5...12).contains(clip.images?.count ?? 0), name)
        }
    }
    func testStatusLightAndTranslateSettingsRow() throws {
        let (window, controller) = host(); defer { window.isHidden = true }
        controller.developmentBuffer("原文", plugin: true); window.layoutIfNeeded()
        let light = try XCTUnwrap(controller.layoutViews.buffer.subviews.first { $0.accessibilityIdentifier == "keyboard.buffer.status" } as? StatusLight)
        let controls = controller.developmentPluginControls
        // The light sits above the settings key, both 1U like the keys on the right.
        XCTAssertEqual(light.frame.minX, controls.settings.frame.minX, accuracy: 0.5)
        XCTAssertEqual(light.frame.size, controls.settings.frame.size)
        XCTAssertEqual(light.frame.size, controller.layoutViews.insert.superview!.frame.size)
        XCTAssertLessThan(light.frame.maxX, controller.layoutViews.result.frame.minX)
        XCTAssertEqual(light.state, .idle)
        // Tapping the pet cycles its theme, and the choice is kept.
        XCTAssertEqual(light.skin, .apple)
        light.onCycleSkin?(); XCTAssertEqual(light.skin, .rhino)
        light.onCycleSkin?(); XCTAssertEqual(light.skin, .crab)
        controller.developmentBuffer(nil); controller.developmentBuffer("原文", plugin: true); XCTAssertEqual(light.skin, .crab)
        for state: StatusLight.State in [.idle, .waiting, .streaming, .ready, .failed] {
            light.set(state); window.layoutIfNeeded()
            let shot = UIGraphicsImageRenderer(bounds: light.bounds).image { light.layer.render(in: $0.cgContext) }
            try? shot.pngData()?.write(to: FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("crab-\(state).png"))
        }
        // A large render of each pose, to check the drawing itself.
        for state: StatusLight.State in [.idle, .waiting, .streaming, .ready, .failed] {
            for skin in StatusLight.Skin.themes where skin != .apple {
                let big = StatusLight(frame: CGRect(x: 0, y: 0, width: 220, height: 240)); big.skin = skin; big.set(state)
                window.addSubview(big); big.layoutIfNeeded()
                let shot = UIGraphicsImageRenderer(bounds: big.bounds).image { big.layer.render(in: $0.cgContext) }
                try? shot.pngData()?.write(to: FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("\(skin)-big-\(state).png"))
                big.removeFromSuperview()
            }
        }
        light.set(.idle)
        // Crab → drawn pets → Noto pets → back to the native theme.
        for expected in Array(StatusLight.Skin.themes.dropFirst(3)) + [.apple] { light.onCycleSkin?(); XCTAssertEqual(light.skin, expected) }
        // A tap (not a hold) switches; settings choose which looks the tap rotates through.
        XCTAssertTrue(light.gestureRecognizers?.contains { $0 is UITapGestureRecognizer } == true)
        XCTAssertFalse(light.gestureRecognizers?.contains { $0 is UILongPressGestureRecognizer } == true)
        controls.settings.sendActions(for: .touchUpInside); window.layoutIfNeeded()
        let skinPanel = try XCTUnwrap(controller.developmentPanel)
        func skinChip(_ skin: StatusSkin) -> PanelChip? { skinPanel.chips.first { $0.accessibilityIdentifier?.hasSuffix("." + skin.rawValue) == true } }
        XCTAssertTrue(StatusSkin.themes.allSatisfy { skinChip($0)?.item.selected == true }) // all by default
        // Narrow the rotation to native + fox + panda: deselect everything else.
        for skin in StatusSkin.themes where ![.apple, .fox, .panda].contains(skin) { skinChip(skin)?.sendActions(for: .touchUpInside) }
        XCTAssertEqual(StatusSkin.themes.filter { skinChip($0)?.item.selected == true }, [.apple, .fox, .panda])
        controls.settings.sendActions(for: .touchUpInside) // close
        light.skin = .apple
        for expected: StatusSkin in [.fox, .panda, .apple, .fox] { light.onCycleSkin?(); XCTAssertEqual(light.skin, expected) }
        controller.developmentBuffer("原文", plugin: true, generating: true); XCTAssertEqual(light.state, .waiting)
        controller.developmentBuffer("原文。", plugin: true, output: "Source."); XCTAssertEqual(light.state, .ready)
        // Translate settings: [from] ⇄ [to] on one line, and a switch for reading aloud.
        controls.settings.sendActions(for: .touchUpInside); window.layoutIfNeeded()
        let panel = try XCTUnwrap(controller.developmentPanel)
        let shot = UIGraphicsImageRenderer(bounds: controller.view.bounds).image { controller.view.layer.render(in: $0.cgContext) }
        try? shot.pngData()?.write(to: FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("keyboard-translate-panel.png"))
        func find<T: UIView>(_ id: String, as: T.Type) -> T? {
            var found: T?; func walk(_ v: UIView) { if v.accessibilityIdentifier == id { found = v as? T }; v.subviews.forEach(walk) }; walk(panel); return found
        }
        let source = try XCTUnwrap(find("keyboard.panel.source", as: DrumPicker.self)), target = try XCTUnwrap(find("keyboard.panel.target", as: DrumPicker.self))
        let swap = try XCTUnwrap(find("keyboard.panel.swap", as: UIButton.self))
        XCTAssertEqual(source.frame.minY, target.frame.minY); XCTAssertEqual(source.frame.midY, swap.frame.midY, accuracy: 0.5)
        XCTAssertLessThan(source.frame.maxX, swap.frame.minX); XCTAssertLessThan(swap.frame.maxX, target.frame.minX)
        // Three rows: the current language in the middle band, its neighbours above and below.
        XCTAssertGreaterThanOrEqual(source.bounds.height, 90)
        let from = source.accessibilityValue, to = target.accessibilityValue
        XCTAssertNotNil(from); XCTAssertNotEqual(from, to)
        swap.sendActions(for: .touchUpInside); window.layoutIfNeeded()
        XCTAssertEqual(source.accessibilityValue, to); XCTAssertEqual(target.accessibilityValue, from)
        let toggle = try XCTUnwrap(find("keyboard.panel.speak", as: PanelToggle.self))
        XCTAssertFalse(toggle.isOn)
        toggle.sendActions(for: .touchUpInside); XCTAssertTrue(toggle.isOn)
        controller.developmentBuffer(nil); controller.developmentBuffer("原文", plugin: true)
        controls.settings.sendActions(for: .touchUpInside); window.layoutIfNeeded()
        XCTAssertTrue(try XCTUnwrap(find("keyboard.panel.speak", as: PanelToggle.self)).isOn) // saved
        toggle.sendActions(for: .touchUpInside)
    }
    func testQuickAskUsesSharedPasteButtonAndSurvivesAlerts() throws {
        let (window, controller) = host(); defer { window.isHidden = true }
        controller.developmentBuffer("", plugin: false); window.layoutIfNeeded()
        let bar = controller.developmentShortcuts
        try XCTUnwrap(bar.buttons.first { $0.plugin == .ask }).button.sendActions(for: .touchUpInside); window.layoutIfNeeded()
        XCTAssertEqual(controller.developmentSelectedPlugin, .ask)
        XCTAssertEqual(controller.layoutViews.source.placeholder, KeyboardPlugin.ask.placeholder)
        let paste = try XCTUnwrap(controller.layoutViews.buffer.subviews.first { $0.accessibilityIdentifier == "keyboard.buffer.paste" } as? BufferPasteButton)
        XCTAssertFalse(controller.developmentPluginControls.run.isHidden)
        XCTAssertNotNil(controller.layoutViews.source.onTapBackground)
        controller.developmentClipboardAccess = false
        paste.sendActions(for: .touchUpInside)
        XCTAssertTrue(controller.developmentStatus.contains(L("完全访问", "Full Access")))
        // A system alert briefly takes focus: the Buffer and the plugin stay open.
        controller.developmentHostResigned()
        XCTAssertEqual(controller.developmentSelectedPlugin, .ask)
        XCTAssertFalse(controller.layoutViews.buffer.isHidden)
        // The prompt keeps answers short and never talks about being a model.
        XCTAssertTrue(AIPrompt.ask.contains("不要提自己是 AI 或模型"))
    }
    func testStreamingShowsNoStatusWordsAndRevealsEvenly() {
        let (window, controller) = host(); defer { window.isHidden = true }
        // Waiting for the first words: no words in the line, but pulsing dots so it is never blank.
        controller.developmentBuffer("原文", plugin: true, generating: true); window.layoutIfNeeded()
        XCTAssertEqual(controller.layoutViews.result.text, "")
        XCTAssertTrue(controller.layoutViews.result.waiting)
        controller.developmentBuffer("原文", plugin: true, output: "译文。"); XCTAssertFalse(controller.layoutViews.result.waiting)
        // A burst of text is revealed gradually, then fully.
        let line = SingleLineTextView(frame: CGRect(x: 0, y: 0, width: 120, height: 36)); window.addSubview(line)
        var settled = false
        let reveal = StreamReveal(line: line) { settled = true }
        let burst = String(repeating: "流式输出更自然。", count: 6)
        reveal.start(); reveal.feed(burst, finished: false)
        RunLoop.main.run(until: Date().addingTimeInterval(0.25))
        XCTAssertGreaterThan(line.text.count, 0); XCTAssertLessThan(line.text.count, burst.count)
        // A whole answer arriving at once still streams at a readable pace (≤ ~90 characters a second).
        reveal.feed(burst, finished: true)
        RunLoop.main.run(until: Date().addingTimeInterval(0.2))
        XCTAssertLessThan(line.text.count, burst.count)
        RunLoop.main.run(until: Date().addingTimeInterval(1.5))
        XCTAssertEqual(line.text, burst); XCTAssertTrue(settled); XCTAssertFalse(reveal.isActive)
        XCTAssertGreaterThan(line.contentOffset.x, 0) // it followed the newest text
        line.removeFromSuperview()
        // Thinking shows (dimmed, unlabelled), then the answer replaces it.
        let (window2, c2) = host(); defer { window2.isHidden = true }
        c2.developmentBuffer("问题", plugin: true, generating: true)
        c2.developmentPreview(ThinkingText.marker + "先想一想这个问题。然后"); RunLoop.main.run(until: Date().addingTimeInterval(0.3))
        XCTAssertTrue(c2.layoutViews.result.thinking)
        XCTAssertEqual(c2.layoutViews.result.text, "先想一想这个问题。") // a whole sentence, not a racing ticker
        // Faster thinking skips ahead to the newest sentence once the current one has been readable.
        c2.developmentPreview(ThinkingText.marker + "先想一想这个问题。然后看第二点。再看第三点。")
        RunLoop.main.run(until: Date().addingTimeInterval(0.3))
        XCTAssertEqual(c2.layoutViews.result.text, "先想一想这个问题。") // still held
        RunLoop.main.run(until: Date().addingTimeInterval(1.1))
        XCTAssertEqual(c2.layoutViews.result.text, "再看第三点。")
        XCTAssertEqual(ThinkingCaption.caption("还没想完"), "")
        XCTAssertEqual(ThinkingCaption.caption("Let me think about the ranking of"), "Let me think about the ranking of")
        XCTAssertFalse(c2.layoutViews.result.text.contains(ThinkingText.marker))
        c2.developmentPreview("答案是这样。")
        // CADisplayLink advances per rendered frame and caps long frame gaps.
        // A busy headless simulator may not deliver enough frames in one second;
        // wait for completion within a bound, keeping the partial-reveal checks above.
        let deadline = Date().addingTimeInterval(5)
        while c2.layoutViews.result.text != "答案是这样。", Date() < deadline {
            RunLoop.main.run(until: Date().addingTimeInterval(0.02))
        }
        XCTAssertFalse(c2.layoutViews.result.thinking)
        XCTAssertEqual(c2.layoutViews.result.text, "答案是这样。")
    }
    func testBlockEdgesSnapshot() {
        let (window, controller) = host(); defer { window.isHidden = true }
        func shot(_ name: String) {
            window.layoutIfNeeded()
            let v = controller.layoutViews.buffer
            let image = UIGraphicsImageRenderer(bounds: v.bounds, format: { let f = UIGraphicsImageRendererFormat(); f.scale = 4; return f }()).image { v.layer.render(in: $0.cgContext) }
            try? image.pngData()?.write(to: FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("edges-\(name).png"))
        }
        controller.developmentBuffer("第一句话。第二句比较长一些，看看边缘。第三块，第四块内容继续！最后一块在这里", plugin: false)
        shot("default-start")
        let source = controller.layoutViews.source
        source.contentOffset.x = max(0, (source.contentSize.width - source.bounds.width) / 2); shot("default-middle")
        source.contentOffset.x = max(0, source.contentSize.width - source.bounds.width); shot("default-end")
        controller.developmentBuffer("原文", plugin: true, output: "First sentence here. Second one is longer. Third! Fourth block? Fifth block ends.")
        shot("plugin-start")
        let result = controller.layoutViews.result
        result.contentOffset.x = max(0, (result.contentSize.width - result.bounds.width) / 2); shot("plugin-middle")
        result.contentOffset.x = max(0, result.contentSize.width - result.bounds.width); shot("plugin-end")
        window.overrideUserInterfaceStyle = .dark; result.contentOffset.x = 0; shot("plugin-dark")
        controller.developmentBuffer("第一句话。第二句比较长一些，看看边缘。第三块", plugin: false); shot("default-dark")
    }
    /// Promo footage: real keyboard renders of each feature. Run with TEST_RUNNER_RIMES_PROMO=1.
    func testCapturePromoFootage() throws {
        try XCTSkipUnless(ProcessInfo.processInfo.environment["RIMES_PROMO"] != nil, "promo capture only on request")
        UserDefaults.standard.set(["zh-Hans"], forKey: "AppleLanguages")
        let root = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("promo")
        try? FileManager.default.removeItem(at: root)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        let (window, controller) = host(); defer { window.isHidden = true }
        var clock: TimeInterval = 100; controller.defaultClockNow = { clock }
        func grab(_ name: String) {
            window.layoutIfNeeded(); controller.view.layoutIfNeeded()
            let format = UIGraphicsImageRendererFormat(); format.scale = 3
            let image = UIGraphicsImageRenderer(bounds: controller.view.bounds, format: format).image { controller.view.layer.render(in: $0.cgContext) }
            try? image.pngData()?.write(to: root.appendingPathComponent(name + ".png"))
        }
        func spin(_ seconds: TimeInterval) { RunLoop.main.run(until: Date().addingTimeInterval(seconds)) }
        controller.developmentSkin(.kitten)
        // 1. Offline pinyin and candidates.
        controller.developmentChoose(.pinyin); controller.developmentType("nihao"); grab("01-pinyin")
        controller.developmentContent(); controller.developmentBuffer(nil)
        // 2. Slide chords with the live hand preview.
        controller.developmentChoose(.chord)
        let keys = controller.layoutViews.keys
        keys.developmentPress("d", end: "v", id: 1); keys.developmentPress("k", end: "m", id: 2); grab("02-chord")
        keys.developmentRelease("v", id: 1); keys.developmentRelease("m", id: 2)
        controller.developmentChoose(.pinyin); controller.developmentContent()
        // 3. Plugin shortcuts in the empty candidate row.
        grab("03-shortcuts")
        // 4. Default Buffer: type first, live stats, send block by block.
        controller.developmentBuffer("")
        for (i, word) in ["jintian", "tianqi", "henhao", "women", "qu", "gongyuan", "ba"].enumerated() {
            for _ in word { keys.onTypingPress?() }
            clock = 100 + Double(i) * 0.9; controller.developmentType(word); controller.developmentSpace()
        }
        controller.developmentType("，"); clock += 0.4
        controller.developmentClearAssociations(); controller.developmentRefreshTypingStats(); grab("04-buffer-typed")
        let send = controller.layoutViews.insert
        send.onPressBegan?(); send.onInsert?(.next); clock += 1; controller.developmentClearAssociations(); controller.developmentRefreshTypingStats(); grab("04-buffer-sent")
        // 5. Real-time translation in blocks, with its settings.
        controller.developmentPlugin(.translate, source: "今天天气很好，我们去公园吧。明天见！", output: "The weather is nice today, let's go to the park. See you tomorrow!")
        grab("05-translate")
        controller.developmentOpenSettings(); grab("05-translate-settings"); controller.developmentClosePanel()
        controller.developmentPlugin(.translate, source: "今天天气很好，我们去公园吧。明天见！", output: "The weather is nice today, let's go to the park. See you tomorrow!")
        // 6. AI Poem: an acrostic of 春眠不觉.
        controller.developmentPlugin(.poem, source: "春眠不觉", output: "春风拂面柳丝长，\n眠鸥听雨梦潇湘。\n不问归期何处是，\n觉来花影满东窗。")
        grab("06-poem")
        // 7. AI Text Art: a fixed-width grid, one row per block.
        let heart = ["⬜🟥🟥⬜⬜🟥🟥⬜", "🟥🟥🟥🟥🟥🟥🟥🟥", "🟥🟥🟥🟥🟥🟥🟥🟥", "⬜🟥🟥🟥🟥🟥🟥⬜", "⬜⬜🟥🟥🟥🟥⬜⬜", "⬜⬜⬜🟥🟥⬜⬜⬜"]
        controller.developmentPlugin(.art, source: "一颗红心", output: heart.joined(separator: "\n")); grab("07-art")
        // 8. Quick Q&A: thinking captions, then the answer streaming in evenly.
        controller.developmentPlugin(.ask, source: "为什么天空是蓝色的？", generating: true); grab("08-ask-waiting")
        controller.developmentPreview(ThinkingText.marker + "用户在问天空为什么是蓝色。关键是阳光在空气里的散射。"); spin(0.4); grab("08-ask-thinking")
        let answer = "因为阳光里的蓝光波长短，碰到空气分子更容易被散射，满天都是蓝光。傍晚阳光斜着穿过更厚的空气，蓝光散光了，就剩红橙色。"
        controller.developmentPreview(answer)
        for frame in 0..<36 { spin(1.0 / 15); grab(String(format: "08-ask-stream-%03d", frame)) }
        controller.developmentFinish(answer); spin(0.8); grab("08-ask-done")
        // 9. Pet status light, large, in each state (drawn skins; Noto/rhino come from their files).
        for skin: StatusSkin in [.crab, .kitten, .puppy, .piglet] {
            for state: StatusLight.State in [.idle, .waiting, .streaming, .ready, .failed] {
                let big = StatusLight(frame: CGRect(x: 0, y: 0, width: 200, height: 220)); big.skin = skin; big.set(state)
                window.addSubview(big); big.layoutIfNeeded()
                let format = UIGraphicsImageRendererFormat(); format.scale = 2
                let image = UIGraphicsImageRenderer(bounds: big.bounds, format: format).image { big.layer.render(in: $0.cgContext) }
                try? image.pngData()?.write(to: root.appendingPathComponent("09-pet-\(skin.rawValue)-\(state).png"))
                big.removeFromSuperview()
            }
        }
        // 10. Typing signature text.
        try? (controller.typingSignature() ?? "").write(to: root.appendingPathComponent("10-signature.txt"), atomically: true, encoding: .utf8)
        try? L("中文", "english").write(to: root.appendingPathComponent("language.txt"), atomically: true, encoding: .utf8)
    }
    func testTranslationBlocksWithTrailingSpacesNeverOverlap() {
        let (window, controller) = host(); defer { window.isHidden = true }
        // Real translation output: English chunks, each ending in a space.
        let blocks = ["The weather is nice ", "today, ", "let's go to the ", "park. ", "See you tomorrow!"]
        controller.developmentPlugin(.translate, source: "今天天气很好，我们去公园吧。明天见！", output: blocks.joined(), blocks: blocks)
        window.layoutIfNeeded()
        let line = controller.layoutViews.result; line.layoutIfNeeded()
        let frames = line.blockFrames
        XCTAssertEqual(frames.count, blocks.count)
        for (left, right) in zip(frames, frames.dropFirst()) { XCTAssertLessThanOrEqual(left.maxX, right.minX + 0.5, "\(frames)") }
        // The text is laid out as wide as the blocks say, so the last block is fully inside the content.
        XCTAssertLessThanOrEqual(frames.last!.maxX, line.contentSize.width)
        let format = UIGraphicsImageRendererFormat(); format.scale = 3
        let image = UIGraphicsImageRenderer(bounds: controller.layoutViews.buffer.bounds, format: format).image { controller.layoutViews.buffer.layer.render(in: $0.cgContext) }
        // The real path: the reply streams in as plain text, then settles into blocks with the same
        // characters. The gaps must appear then too, or block backgrounds run into each other.
        controller.developmentPlugin(.translate, source: "今天天气很好。明天见！", generating: true)
        controller.developmentPreview("Nice day! See you tomorrow!"); RunLoop.main.run(until: Date().addingTimeInterval(1.5))
        XCTAssertEqual(line.text, "Nice day! See you tomorrow!")
        controller.developmentFinish("Nice day! See you tomorrow!"); RunLoop.main.run(until: Date().addingTimeInterval(0.3))
        window.layoutIfNeeded(); line.layoutIfNeeded()
        XCTAssertEqual(line.blockRanges.count, 2)
        let firstEnd = NSMaxRange(line.blockRanges[0]) - 1
        XCTAssertNotNil(line.attributedText?.attribute(.kern, at: firstEnd, effectiveRange: nil), "the gap after the first block")
        let settled = line.blockFrames
        XCTAssertEqual(settled.count, 2)
        if settled.count == 2 { XCTAssertLessThanOrEqual(settled[0].maxX, settled[1].minX + 0.5) }
    }
    func testEmptyCandidateRowOpensPluginsWithoutRunningThem() throws {
        let (window, controller) = host(); defer { window.isHidden = true }
        let bar = controller.developmentShortcuts, controls = controller.developmentPluginControls
        XCTAssertFalse(bar.isHidden)
        func snapshot(_ name: String) {
            controller.view.layoutIfNeeded()
            let image = UIGraphicsImageRenderer(bounds: controller.view.bounds).image { context in controller.view.layer.render(in: context.cgContext) }
            try? image.pngData()?.write(to: FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("keyboard-\(name).png"))
        }
        snapshot("plugin-shortcuts")
        XCTAssertEqual(bar.buttons.map(\.plugin), [.translate, .ask, .polish, .poem, .art])
        // Every shortcut keeps its two-square width; when they outgrow the row it scrolls sideways.
        XCTAssertGreaterThanOrEqual(bar.subviews.compactMap { $0 as? UIScrollView }.first?.contentSize.width ?? 0, bar.buttons.last!.button.frame.maxX)
        // Host chrome floating over the keyboard must not blur its rows (iOS 26+ scroll edge effects).
        if #available(iOS 26, *) {
            var scrolls: [UIScrollView] = []
            func collect(_ view: UIView) { if let scroll = view as? UIScrollView { scrolls.append(scroll) }; view.subviews.forEach(collect) }
            collect(controller.view)
            XCTAssertGreaterThanOrEqual(scrolls.count, 4)
            for scroll in scrolls { XCTAssertTrue(scroll.topEdgeEffect.isHidden && scroll.bottomEdgeEffect.isHidden && scroll.leftEdgeEffect.isHidden && scroll.rightEdgeEffect.isHidden, "\(type(of: scroll))") }
        }
        for (_, button) in bar.buttons { XCTAssertEqual(button.bounds.width, 2 * bar.bounds.height + 4, accuracy: 0.5) }
        controller.developmentContent(preedit: "ni", candidates: ["你"]); window.layoutIfNeeded()
        XCTAssertTrue(bar.isHidden)
        controller.developmentContent(); window.layoutIfNeeded()
        XCTAssertFalse(bar.isHidden)
        // Opening a plugin turns the Buffer on and waits: no request, no output.
        try XCTUnwrap(bar.buttons.first { $0.plugin == .poem }).button.sendActions(for: .touchUpInside); window.layoutIfNeeded()
        XCTAssertEqual(controller.developmentSelectedPlugin, .poem); XCTAssertEqual(bar.selected, .poem)
        XCTAssertFalse(controller.layoutViews.buffer.isHidden)
        XCTAssertEqual(controller.layoutViews.result.text, "")
        XCTAssertFalse(controls.run.isHidden); XCTAssertFalse(controls.run.isEnabled) // nothing written yet
        XCTAssertLessThan(controls.settings.frame.maxX, controller.layoutViews.source.frame.minX + 0.5)
        XCTAssertLessThan(controller.layoutViews.source.frame.maxX, controls.run.frame.minX + 0.5)
        XCTAssertLessThan(controls.run.frame.maxX, controls.plugin.frame.minX + 0.5)
        snapshot("plugin-poem-open")
        // The settings key opens RIMES's own panel over the keys; the plugin key keeps its chooser menu.
        XCTAssertNotNil(controls.plugin.menu); XCTAssertNil(controller.developmentPanel)
        // Every plugin's settings key is the same 1U icon key, nothing drawn past its cap.
        XCTAssertNil(controls.settings.title(for: .normal)); XCTAssertNotNil(controls.settings.image(for: .normal))
        XCTAssertEqual(controls.settings.bounds.size, controls.run.bounds.size)
        controls.settings.sendActions(for: .touchUpInside); window.layoutIfNeeded()
        let panel = try XCTUnwrap(controller.developmentPanel)
        XCTAssertEqual(panel.frame.minY, controller.layoutViews.keys.frame.minY, accuracy: 0.5)
        XCTAssertGreaterThan(panel.frame.minY, controller.layoutViews.buffer.frame.maxY)
        func chip(_ id: String) -> PanelChip? { panel.chips.first { $0.accessibilityIdentifier?.hasSuffix(".\(id)") == true } }
        XCTAssertEqual(chip(KeyboardPlugin.poem.rawValue)?.item.selected, true)
        XCTAssertNotNil(chip("improvise")); XCTAssertNotNil(chip("builtin.lushi")); XCTAssertNotNil(chip("clear"))
        // AI plugins can set the thinking depth right here.
        XCTAssertEqual(AIThinking.allCases.compactMap { chip($0.rawValue) }.count, AIThinking.allCases.count)
        XCTAssertEqual(chip("minimal")?.item.selected, true)
        try XCTUnwrap(chip("off")).sendActions(for: .touchUpInside)
        XCTAssertEqual(chip("off")?.item.selected, true)
        try XCTUnwrap(chip("acrosticHead")).sendActions(for: .touchUpInside)
        XCTAssertEqual(chip("acrosticHead")?.item.selected, true); XCTAssertEqual(chip("improvise")?.item.selected, false)
        snapshot("plugin-panel-poem")
        XCTAssertNil(controls.settings.title(for: .normal))
        controls.settings.sendActions(for: .touchUpInside); XCTAssertNil(controller.developmentPanel)
        // Polish also has Run; tapping the open plugin again returns to Default.
        try XCTUnwrap(bar.buttons.first { $0.plugin == .polish }).button.sendActions(for: .touchUpInside); window.layoutIfNeeded()
        XCTAssertFalse(controls.run.isHidden)
        try XCTUnwrap(bar.buttons.first { $0.plugin == .polish }).button.sendActions(for: .touchUpInside); window.layoutIfNeeded()
        XCTAssertNil(controller.developmentSelectedPlugin); XCTAssertTrue(controls.run.isHidden)
    }
    func testHeldChordTemporarilyShowsBothHandsInCandidateRow() {
        let (window, controller) = host(); defer { window.isHidden = true }
        controller.developmentChoose(.chord)
        let keys = controller.layoutViews.keys, preview = controller.developmentHandPreview, strip = controller.layoutViews.candidates
        XCTAssertTrue(preview.isHidden); XCTAssertFalse(strip.isHidden)
        keys.developmentPress("d", end: "v", id: 1)
        XCTAssertFalse(preview.isHidden); XCTAssertTrue(strip.isHidden)
        XCTAssertNotNil(preview.preview?.left); XCTAssertNil(preview.preview?.right)
        XCTAssertEqual(preview.texts.first, "DV"); XCTAssertEqual(preview.texts.last, "")
        keys.developmentPress("k", end: "m", id: 2)
        let expected = ChordProfile.builtIn.resolve(Set("dvkm"))?.preview
        XCTAssertNotNil(expected)
        XCTAssertEqual(preview.preview?.combined, expected)
        XCTAssertEqual(preview.preview?.left?.keys, "dv"); XCTAssertEqual(preview.preview?.right?.keys, "km")
        XCTAssertEqual(preview.texts, ["DV", "n", expected!, "ong", "KM"])
        controller.view.layoutIfNeeded()
        let image = UIGraphicsImageRenderer(bounds: controller.view.bounds).image { context in controller.view.layer.render(in: context.cgContext) }
        let attachment = XCTAttachment(image: image); attachment.name = "chord-hand-preview"; attachment.lifetime = .keepAlways; add(attachment)
        try? image.pngData()?.write(to: FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("keyboard-chord-hand-preview.png"))
        keys.developmentRelease("v", id: 1)
        XCTAssertFalse(preview.isHidden) // one thumb still holds the chord
        keys.developmentRelease("m", id: 2)
        XCTAssertTrue(preview.isHidden); XCTAssertFalse(strip.isHidden); XCTAssertNil(preview.preview)
    }
    func testBufferLinesStayOneLineAndScrollHorizontally() {
        let (window, controller) = host(); defer { window.isHidden = true }
        let long = String(repeating: "长原文内容。", count: 12)
        controller.developmentBuffer(long, plugin: true, output: String(repeating: "A longer translation preview. ", count: 8)); window.layoutIfNeeded()
        for line in [controller.layoutViews.source, controller.layoutViews.result] {
            line.layoutIfNeeded()
            XCTAssertEqual(line.contentSize.height, line.bounds.height, accuracy: 0.5)
            XCTAssertGreaterThan(line.contentSize.width, line.bounds.width * 2)
            let label = line.subviews.flatMap { [$0] + $0.subviews }.compactMap { $0 as? UILabel }.first // inside the clipped canvas
            XCTAssertEqual(label?.numberOfLines, 1)
        }
        // The caret is at the end of the source, so the source line follows it.
        let source = controller.layoutViews.source
        XCTAssertGreaterThan(source.contentOffset.x, 0)
        XCTAssertEqual(controller.layoutViews.result.contentOffset.x, 0)
        XCTAssertEqual(controller.layoutViews.source.caretLocation, (controller.layoutViews.source.text as NSString).length)
        XCTAssertFalse(controller.layoutViews.source.text.contains("▏")) // the caret is an overlay, not a character
    }
    func testCursorDragStepsAndCarriesRemainder() {
        var drag = CursorDrag()
        XCTAssertEqual(drag.steps(to: 100), 0) // inactive
        drag.activate(at: 100)
        XCTAssertEqual(drag.steps(to: 104), 0)
        XCTAssertEqual(drag.steps(to: 112), 1)
        XCTAssertEqual(drag.steps(to: 135), 2)
        XCTAssertEqual(drag.steps(to: 99), -3)
        XCTAssertEqual(drag.steps(to: 100 - CursorDrag.step * 0.9), 0) // less than one step: no move
        drag.cancel(); XCTAssertEqual(drag.steps(to: 0), 0)
    }
    func testSpaceHoldDragMovesCaretInHostAndBufferWithoutTypingSpace() {
        for buffered in [false, true] {
            let (window, controller) = host(); defer { window.isHidden = true }
            controller.developmentChoose(.english)
            let space = controller.developmentSpaceKey
            if buffered { controller.developmentBuffer("ab😀cd") }
            else { controller.layoutProxy.native.text = "ab😀cd"; controller.layoutProxy.native.selectedRange = NSRange(location: 6, length: 0) }
            func caret() -> Int { buffered ? controller.developmentBufferCursor : controller.layoutProxy.native.selectedRange.location }
            let start = caret()
            space.beginCursor(requireTracking: false); XCTAssertTrue(space.isMovingCursor)
            space.moveCursor(to: -CursorDrag.step * 3) // three characters left, including the emoji
            XCTAssertEqual(caret(), buffered ? start - 3 : 2)
            space.moveCursor(to: -CursorDrag.step * 2)
            XCTAssertEqual(caret(), buffered ? start - 2 : 4)
            space.finish(); XCTAssertFalse(space.isMovingCursor)
            XCTAssertFalse(space.consumeTap()) // release after a hold types nothing
            XCTAssertTrue(space.consumeTap())
            XCTAssertEqual(buffered ? controller.developmentBufferSource.text : controller.layoutProxy.native.text, "ab😀cd")
        }
        // Holding during composition does not start caret movement.
        let (window, controller) = host(); defer { window.isHidden = true }
        controller.developmentType("ni")
        controller.developmentSpaceKey.beginCursor(requireTracking: false)
        XCTAssertFalse(controller.developmentSpaceKey.isMovingCursor)
    }
    func testChordModeSwapsLanguageToggleAndDelete() throws {
        let (window, controller) = host(); defer { window.isHidden = true }
        let v = controller.layoutViews
        func view(_ id: String, in root: UIView) -> UIView? { root.subviews.first { $0.accessibilityIdentifier == id } }
        let bottomDelete = try XCTUnwrap(controller.developmentStandardFunctions[.backspace]), bottomLanguage = try XCTUnwrap(controller.developmentStandardFunctions[.language])
        let chordDelete = try XCTUnwrap(view("keyboard.delete.chord", in: v.keys) as? RepeatKeycapButton)
        // Pinyin: ordinary layout.
        XCTAssertFalse(bottomDelete.isHidden); XCTAssertFalse(bottomLanguage.isHidden); XCTAssertTrue(chordDelete.isHidden)
        XCTAssertTrue(bottomDelete.superview === v.keys)
        controller.developmentChoose(.chord); window.layoutIfNeeded()
        XCTAssertTrue(bottomDelete.isHidden); XCTAssertFalse(bottomLanguage.isHidden); XCTAssertFalse(chordDelete.isHidden)
        XCTAssertNil(view("keyboard.mode", in: v.keys).flatMap { $0.isHidden ? nil : $0 })
        let frames = v.keys.developmentKeyFrames
        XCTAssertEqual(chordDelete.frame, try XCTUnwrap(frames["mode"]))
        XCTAssertEqual(chordDelete.frame.minX, try XCTUnwrap(frames["p"]).minX, accuracy: 0.5)
        XCTAssertGreaterThan(bottomLanguage.bounds.width, 30)
        // The bottom toggle switches language; the grid Delete deletes.
        (bottomLanguage as? UIControl)?.sendActions(for: .touchUpInside)
        XCTAssertFalse(v.keys.resolvesChords); XCTAssertEqual((bottomLanguage as? UIButton)?.title(for: .normal), "EN")
        controller.developmentType("ab"); XCTAssertEqual(controller.layoutProxy.native.text, "ab")
        chordDelete.beginPress(); chordDelete.cancelPress()
        XCTAssertEqual(controller.layoutProxy.native.text, "a")
        (bottomLanguage as? UIControl)?.sendActions(for: .touchUpInside)
        XCTAssertTrue(v.keys.resolvesChords); XCTAssertEqual((bottomLanguage as? UIButton)?.title(for: .normal), "中")
        // Numbers restore the ordinary Delete position.
        controller.developmentNumeric(); window.layoutIfNeeded()
        XCTAssertFalse(bottomDelete.isHidden); XCTAssertTrue(bottomLanguage.isHidden); XCTAssertTrue(chordDelete.isHidden)
    }
    func testEdgeGesturesNoLongerDelayKeyTouches() {
        let (window, controller) = host(); defer { window.isHidden = true }
        let edge = UIScreenEdgePanGestureRecognizer(); edge.edges = .left; edge.delaysTouchesBegan = true
        let ancestor = UITapGestureRecognizer(); ancestor.delaysTouchesBegan = true
        window.addGestureRecognizer(edge); controller.view.superview?.addGestureRecognizer(ancestor)
        controller.developmentReleaseEdgeTouchDelay()
        XCTAssertFalse(edge.delaysTouchesBegan); XCTAssertFalse(ancestor.delaysTouchesBegan)
        XCTAssertTrue(edge.isEnabled) // system gestures still work
    }
    func testChordSpaceSplitsLeftSelectsInBufferRightMoves() throws {
        UserDefaults.standard.removeObject(forKey: "rimes.keyboard.hostSelectionNoteShown")
        let (window, controller) = host(); defer { window.isHidden = true }
        let left = controller.developmentSpaceKey, right = controller.developmentRightSpaceKey
        XCTAssertTrue(right.isHidden)
        controller.developmentChoose(.chord); window.layoutIfNeeded()
        XCTAssertFalse(right.isHidden)
        func hold(_ key: SpaceCursorButton, steps: Int) {
            key.moveCursor(to: 0) // each real press starts from its own touch point
            key.beginCursor(requireTracking: false)
            key.moveCursor(to: CursorDrag.step * CGFloat(steps)); key.finish()
            XCTAssertFalse(key.consumeTap()) // a hold never types a space
        }
        // Buffer: the left Space selects from the cursor; Delete removes the selection.
        controller.developmentBuffer("ab😀cd")
        hold(left, steps: -3)
        XCTAssertEqual(controller.developmentBufferSelection, 2..<5)
        let highlighted = controller.layoutViews.source.attributedText!
        var selectedText = ""
        highlighted.enumerateAttribute(.backgroundColor, in: NSRange(location: 0, length: highlighted.length)) { value, range, _ in
            if let color = value as? UIColor, color != UIColor.systemTeal.withAlphaComponent(0.1) { selectedText += (highlighted.string as NSString).substring(with: range) }
        }
        XCTAssertEqual(selectedText, "😀cd")
        controller.developmentBackspace()
        XCTAssertEqual(controller.developmentBufferSource.text, "ab"); XCTAssertNil(controller.developmentBufferSelection)
        // The right Space only moves; any typing key clears a selection without deleting it.
        hold(right, steps: -1); XCTAssertNil(controller.developmentBufferSelection); XCTAssertEqual(controller.developmentBufferCursor, 1)
        hold(left, steps: 1); XCTAssertEqual(controller.developmentBufferSelection, 1..<2)
        controller.layoutViews.keys.onTypingPress?()
        XCTAssertNil(controller.developmentBufferSelection); XCTAssertEqual(controller.developmentBufferSource.text, "ab")
        // Host field: iOS allows no selection, so the left Space moves the caret and explains once.
        controller.developmentBuffer(nil)
        controller.layoutProxy.native.text = "hello"; controller.layoutProxy.native.selectedRange = NSRange(location: 5, length: 0)
        hold(left, steps: -2)
        XCTAssertEqual(controller.layoutProxy.native.selectedRange, NSRange(location: 3, length: 0))
        XCTAssertFalse(controller.developmentStatus.isEmpty)
        controller.developmentContent(); XCTAssertTrue(UserDefaults.standard.bool(forKey: "rimes.keyboard.hostSelectionNoteShown"))
        // Ordinary layouts restore their single Space control.
        controller.developmentChoose(.pinyin); XCTAssertTrue(right.isHidden)
        XCTAssertTrue(left.superview === controller.layoutViews.keys)
    }
    func testChordSpacesHaveSeparateCapsHitTargetsAndActionsAcrossLayoutSwitches() async throws {
        for width: CGFloat in [320, 393, 852] {
            let (window, controller) = host(width: width); defer { window.isHidden = true }
            try await Task.sleep(nanoseconds: 80_000_000)
            let left = controller.developmentSpaceKey, right = controller.developmentRightSpaceKey
            for layout in ChordLayout.allCases {
                for needsGlobe in [false, true] {
                    controller.layoutNeedsInputModeSwitchKey = needsGlobe
                    controller.developmentChoose(.chord); controller.developmentSetLayout(layout)
                    window.layoutIfNeeded()
                    let group = try XCTUnwrap(left.superview as? UIStackView)
                    XCTAssertTrue(right.superview === group)
                    XCTAssertEqual(group.arrangedSubviews.count, 2)
                    XCTAssertFalse(left.isHidden); XCTAssertFalse(right.isHidden)
                    XCTAssertGreaterThan(left.bounds.width, 35)
                    XCTAssertEqual(left.bounds.width, right.bounds.width, accuracy: 0.5)
                    XCTAssertEqual(right.frame.minX - left.frame.maxX, 2, accuracy: 0.5)
                    for key in [left, right] {
                        let center = CGPoint(x: key.frame.midX, y: key.frame.midY)
                        XCTAssertTrue(group.hitTest(center, with: nil) === key)
                    }
                    let gap = CGPoint(x: (left.frame.maxX + right.frame.minX) / 2, y: group.bounds.midY)
                    XCTAssertTrue(group.hitTest(gap, with: nil) === group)
                    for skin: StatusSkin in [.apple, .rhino] {
                        controller.developmentSkin(skin)
                        XCTAssertEqual(left.theme, skin); XCTAssertEqual(right.theme, skin)
                        left.isHighlighted = true; XCTAssertFalse(right.isHighlighted)
                        left.isHighlighted = false; right.isHighlighted = true; XCTAssertFalse(left.isHighlighted)
                        right.isHighlighted = false
                    }
                    controller.layoutProxy.native.text = ""
                    left.sendActions(for: .touchUpInside); right.sendActions(for: .touchUpInside)
                    await controller.developmentWaitForDelivery()
                    XCTAssertEqual(controller.layoutProxy.native.text, "  ")
                    for key in [left, right] {
                        let before = controller.layoutProxy.native.text ?? ""
                        controller.developmentType("ni")
                        let candidate = try XCTUnwrap(controller.layoutViews.candidates.buttons.first?.currentTitle)
                        key.sendActions(for: .touchUpInside)
                        await controller.developmentWaitForDelivery()
                        XCTAssertTrue(controller.developmentRaw.isEmpty)
                        XCTAssertEqual(controller.layoutProxy.native.text, before + candidate)
                    }
                    // Moving the ordinary Space into the managed layout and back
                    // must not leave an extra control or a broken stack constraint.
                    controller.developmentChoose(.pinyin)
                    controller.developmentOrdinaryAppearance(layout: .nineKey)
                    XCTAssertTrue(right.isHidden)
                    XCTAssertTrue(left.superview === controller.layoutViews.keys)
                }
            }
        }
    }
    func testBufferBlocksAndOverlayCaretKeepTextLayout() throws {
        let font = UIFont.systemFont(ofSize: 15)
        // Blocks follow the delivery segmenter; unconfirmed text joins the block it ends.
        let blocks = ["第一句。", "第二句！", "未完成"]
        let composed = BufferComposition(source: blocks.joined(), cursor: 8, preedit: "ni", font: font, blocks: blocks)
        XCTAssertEqual(composed.text.string, "第一句。第二句！ni未完成")
        XCTAssertEqual(composed.blockRanges, [NSRange(location: 0, length: 4), NSRange(location: 4, length: 6), NSRange(location: 10, length: 3)])
        XCTAssertEqual(composed.activeBlock, 1); XCTAssertEqual(composed.caretRange, NSRange(location: 10, length: 0))
        // The caret adds no width: text with and without a caret measures the same.
        let atStart = BufferComposition(source: "你好", cursor: 0, preedit: "", font: font)
        let atEnd = BufferComposition(source: "你好", cursor: 2, preedit: "", font: font)
        XCTAssertEqual(atStart.text.size().width, atEnd.text.size().width, accuracy: 0.01)

        let (window, controller) = host(); defer { window.isHidden = true }
        controller.developmentBuffer("第一句。第二句！未完成", plugin: false); window.layoutIfNeeded()
        let line = controller.layoutViews.source; line.layoutIfNeeded()
        XCTAssertEqual(line.text, "第一句。第二句！未完成")
        XCTAssertEqual(line.blockRanges.count, 3); XCTAssertEqual(line.activeBlock, 2)
        XCTAssertEqual(line.blockFrames.count, 3)
        for (left, right) in zip(line.blockFrames, line.blockFrames.dropFirst()) { XCTAssertLessThan(left.maxX, right.minX) } // visible gaps
        let caret = try XCTUnwrap(line.subviews.flatMap { [$0] + $0.subviews }.first { $0.bounds.width == 2 && !$0.isHidden })
        XCTAssertEqual(caret.frame.midX, line.blockFrames[2].maxX - 4, accuracy: 1.5) // after the last character
        controller.developmentBufferCursor(4); window.layoutIfNeeded(); line.layoutIfNeeded()
        XCTAssertEqual(line.activeBlock, 0); XCTAssertEqual(line.caretLocation, 4)
        XCTAssertLessThan(caret.frame.maxX, line.blockFrames[1].minX) // caret sits before the gap, not inside the next block
        // Plugin output shows its result blocks; the head (next to send) is emphasised.
        controller.developmentBuffer("原文。", plugin: true, output: "One. Two."); window.layoutIfNeeded()
        let output = controller.layoutViews.result
        XCTAssertEqual(output.text, "One. Two."); XCTAssertEqual(output.activeBlock, 0)
        XCTAssertTrue(line.blockRanges.isEmpty)
    }
    func testAssociationsFollowCommitsLearnAndClearOnOtherKeys() async throws {
        let (window, controller) = host(); defer { window.isHidden = true }
        try await Task.sleep(nanoseconds: 80_000_000)
        // Keep persisted app reset requests out of this two-controller fixture.
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        let suite = "rimes-association-fixture-\(UUID().uuidString)"
        let defaults = try XCTUnwrap(UserDefaults(suiteName: suite))
        defer { try? FileManager.default.removeItem(at: directory); defaults.removePersistentDomain(forName: suite) }
        let store = AssociationHistoryStore(root: directory, defaults: defaults)
        controller.developmentAssociationStore(store)
        controller.developmentClearAssociationHistory()
        let strip = controller.layoutViews.candidates
        func text() -> String { controller.layoutProxy.native.text }
        // Bundled dictionary continuations appear after a commit.
        controller.developmentType("xiexie"); controller.developmentSpace()
        XCTAssertEqual(text(), "谢谢")
        XCTAssertEqual(Array(controller.developmentAssociations.prefix(3)), ["了", "大家", "合作"])
        XCTAssertEqual(strip.buttons.map { $0.title(for: .normal) ?? "" }.prefix(3), ["了", "大家", "合作"])
        let toggle = try XCTUnwrap(strip.superview?.subviews.first { $0.accessibilityIdentifier == "keyboard.buffer" })
        XCTAssertFalse(controller.layoutViews.settings.isHidden); XCTAssertFalse(toggle.isHidden)
        // Tapping one inserts it and chains to the next associations.
        strip.buttons[1].sendActions(for: .touchUpInside)
        await controller.developmentWaitForDelivery()
        XCTAssertEqual(text(), "谢谢大家")
        // Any other key hides them; Space types a space instead of picking one.
        controller.developmentType("zhongguo"); controller.developmentSpace()
        XCTAssertFalse(controller.developmentAssociations.isEmpty)
        controller.layoutViews.keys.onTypingPress?(); XCTAssertTrue(controller.developmentAssociations.isEmpty)
        controller.developmentType("ni"); XCTAssertFalse(strip.buttons.isEmpty) // composition candidates, not associations
        XCTAssertTrue(controller.layoutViews.settings.isHidden); XCTAssertTrue(toggle.isHidden)
        controller.developmentEnter()
        // Learned: consecutive commits teach what follows, ahead of the dictionary.
        controller.developmentClearAssociationHistory()
        for _ in 0..<2 { controller.developmentType("xiexie"); controller.developmentSpace(); controller.developmentType("ni"); controller.developmentSpace(); controller.developmentEnter() }
        controller.developmentType("xiexie"); controller.developmentSpace()
        XCTAssertEqual(controller.developmentAssociations.first, "你")
        // Delete ends the chain: the next commit is not learned as a continuation.
        controller.developmentBackspace(); controller.layoutViews.keys.onTypingPress?()
        XCTAssertTrue(controller.developmentAssociations.isEmpty)
        // History persists across keyboard sessions and can be cleared.
        controller.developmentSaveAssociationHistory()
        await controller.developmentWaitForDelivery()
        let (window2, second) = host(); defer { window2.isHidden = true }
        second.developmentAssociationStore(store)
        second.developmentType("xiexie"); second.developmentSpace()
        XCTAssertEqual(second.developmentAssociations.first, "你")
        second.developmentClearAssociationHistory()
        second.developmentType("xiexie"); second.developmentSpace()
        XCTAssertEqual(second.developmentAssociations.first, "了")
        // English and punctuation never produce associations.
        second.developmentChoose(.english); second.developmentType("hello")
        XCTAssertTrue(second.developmentAssociations.isEmpty)
    }
    func testTypedWordsBecomeTheirOwnBlocksAndSendOneAtATime() throws {
        let (window, controller) = host(); defer { window.isHidden = true }
        controller.developmentBuffer("")
        controller.developmentType("nihao"); controller.developmentSpace()
        let line = controller.layoutViews.source; window.layoutIfNeeded(); line.layoutIfNeeded()
        XCTAssertEqual(line.blockRanges.count, 1) // a single word is a single block
        controller.developmentType("shijie"); controller.developmentSpace()
        controller.developmentType(","); window.layoutIfNeeded(); line.layoutIfNeeded()
        XCTAssertEqual(controller.developmentBufferSource.text, "你好世界，")
        XCTAssertEqual(line.blockRanges.map { (line.text as NSString).substring(with: $0) }, ["你好", "世界，"])
        XCTAssertEqual(line.activeBlock, 1); XCTAssertEqual(line.blockFrames.count, 2)
        let image = UIGraphicsImageRenderer(bounds: controller.view.bounds).image { context in controller.view.layer.render(in: context.cgContext) }
        try? image.pngData()?.write(to: FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("keyboard-word-blocks.png"))
        // Delete removes the whole last block, then the one before; pinyin in
        // composition still deletes letter by letter.
        controller.developmentType("dajia"); controller.developmentBackspace()
        XCTAssertEqual(controller.developmentRaw, "daji"); XCTAssertEqual(controller.developmentBufferSource.text, "你好世界，")
        controller.developmentSpace(); XCTAssertGreaterThan(controller.developmentBufferSource.text.count, 5) // committed a new word
        controller.developmentBackspace(); XCTAssertEqual(controller.developmentBufferSource.text, "你好世界，") // that word, whole
        controller.developmentBackspace(); XCTAssertEqual(controller.developmentBufferSource.text, "你好") // 世界， in one press
        // Plugin input keeps character deletion.
        controller.developmentBuffer("原文。", plugin: true, output: "Text."); controller.developmentBackspace()
        XCTAssertEqual(controller.developmentBufferSource.text, "原文")
        controller.developmentBuffer(""); ["nihao", "shijie"].forEach { controller.developmentType($0); controller.developmentSpace() }
        controller.developmentType(","); window.layoutIfNeeded(); line.layoutIfNeeded()
        // Send inserts exactly the head block; the rest keeps its blocks.
        XCTAssertTrue(controller.layoutViews.insert.accessibilityActivate())
        XCTAssertEqual(controller.layoutProxy.native.text, "你好")
        window.layoutIfNeeded(); line.layoutIfNeeded()
        XCTAssertEqual(line.text, "世界，"); XCTAssertEqual(line.blockRanges.count, 1)
    }
    func testDefaultChordSerialGHReachesGangInHostAndBuffer() {
        for buffered in [false, true] {
            let (window, controller) = host(); defer { window.isHidden = true }
            controller.developmentChoose(.chord)
            if buffered { controller.developmentBuffer("") }
            controller.developmentType("g"); controller.developmentType("h")
            XCTAssertEqual(controller.developmentRaw, "gh")
            let preedit = buffered ? controller.layoutViews.source.text : controller.layoutProxy.native.text
            XCTAssertEqual(preedit, "gang")
            controller.developmentEnter()
            XCTAssertEqual(buffered ? controller.developmentBufferSource.text : controller.layoutProxy.native.text, "gh")
            XCTAssertTrue(controller.developmentRaw.isEmpty)
        }
    }
    func testProxyWriteCallbacksPreserveCompositionAndAutomaticDelivery() async throws {
        let (window, controller) = host(); defer { window.isHidden = true; controller.developmentAutoDelay(0) }
        try await Task.sleep(nanoseconds: 80_000_000)
        controller.layoutProxy.onProxyWrite = { [weak controller] in
            controller?.selectionWillChange(nil); controller?.textWillChange(nil); controller?.textDidChange(nil)
        }
        controller.developmentChoose(.pinyin)
        controller.developmentType("ni")
        XCTAssertEqual(controller.developmentRaw, "ni")
        XCTAssertEqual(controller.layoutProxy.native.text, "ni")
        controller.developmentEnter()
        XCTAssertEqual(controller.layoutProxy.native.text, "ni")
        XCTAssertTrue(controller.developmentRaw.isEmpty)
        var time: TimeInterval = 0; controller.defaultClockNow = { time }
        controller.developmentBuffer("A。B。"); controller.developmentAutoDelay(1, manualTicks: true)
        controller.developmentAutoTick(); time = 1; controller.developmentAutoTick()
        await controller.developmentWaitForDelivery()
        XCTAssertEqual(controller.layoutProxy.native.text, "niA。")
        // A host may also deliver selection notifications asynchronously.
        controller.selectionWillChange(nil)
        time = 1.1; controller.developmentAutoTick()
        await controller.developmentWaitForDelivery()
        XCTAssertEqual(controller.layoutProxy.native.text, "niA。B。")
    }
    func testDefaultBufferAutoDeliveryIsBoundToVisibleTargetAndOriginalMode() {
        let (window, controller) = host(); defer { window.isHidden = true; controller.developmentAutoDelay(0) }
        var time: TimeInterval = 0; controller.defaultClockNow = { time }
        controller.developmentChoose(.english); controller.developmentBuffer("")
        controller.developmentAutoDelay(1)
        controller.layoutViews.keys.onTypingPress?()
        for key in ["A", "。", "B", "。"] { controller.developmentType(key) } // one commit per key, as typed
        XCTAssertEqual(controller.developmentTypingMetrics.committedCharacterCount, 4)
        XCTAssertEqual(controller.developmentTypingMetrics.keyCount, 1)
        controller.developmentAutoTick()
        time = 1; controller.developmentAutoTick()
        XCTAssertEqual(controller.layoutProxy.native.text, "A。")
        XCTAssertEqual(controller.developmentBufferSource.text, "B。")
        time = 1.1; controller.developmentAutoTick()
        XCTAssertEqual(controller.layoutProxy.native.text, "A。B。")
        controller.developmentType("held")
        controller.developmentAutoTick()
        controller.layoutProxy.documentIdentifier = UUID(); controller.textDidChange(nil)
        time = 5; controller.developmentAutoTick()
        XCTAssertEqual(controller.layoutProxy.native.text, "A。B。")
        controller.developmentAutoDelay(1)
        controller.developmentBuffer("source", plugin: true, output: "translation")
        controller.developmentAutoTick(); time = 10; controller.developmentAutoTick()
        XCTAssertEqual(controller.layoutProxy.native.text, "A。B。")
        controller.developmentBuffer("hide"); controller.developmentAutoDelay(1)
        controller.developmentAutoTick(); controller.viewWillDisappear(false)
        time = 20; controller.developmentAutoTick()
        XCTAssertEqual(controller.layoutProxy.native.text, "A。B。")
    }
    func testDeleteRepeatsAcrossCompositionThenBufferAndStopsOnTargetChange() {
        let (window, controller) = host(); defer { window.isHidden = true }
        controller.developmentChoose(.pinyin); controller.developmentBuffer("甲。乙。丙。") // three blocks
        controller.developmentType("ni")
        let button = controller.developmentDelete
        var time: TimeInterval = 10; button.clock = { time }
        let feedback = controller.layoutViews.keys.feedback
        feedback.enabled = true; feedback.reset(); feedback.clock = { time }
        var pulses = 0; feedback.onFeedback = { _ in pulses += 1 }
        button.sendActions(for: .touchDown)
        XCTAssertEqual(controller.developmentRaw, "n"); XCTAssertEqual(pulses, 1)
        time = 10.399; button.advance(); XCTAssertEqual(controller.developmentRaw, "n")
        time = 10.4; button.advance(); XCTAssertTrue(controller.developmentRaw.isEmpty)
        time = 10.475; button.advance(); XCTAssertEqual(controller.developmentBufferSource.text, "甲。乙。"); XCTAssertEqual(pulses, 3) // one whole block per repeat
        button.sendActions(for: .touchUpInside); time = 20; button.advance(); XCTAssertEqual(pulses, 3)
        XCTAssertEqual(controller.developmentBufferSource.text, "甲。乙。")
        button.sendActions(for: .touchDown); XCTAssertEqual(controller.developmentBufferSource.text, "甲。")
        controller.layoutProxy.documentIdentifier = UUID(); controller.textDidChange(nil)
        time = 21; button.advance(); XCTAssertEqual(controller.developmentBufferSource.text, "甲。")
        controller.developmentBuffer(nil); controller.developmentChoose(.english)
        controller.layoutProxy.native.text = "abcd"; controller.layoutProxy.native.selectedRange = NSRange(location: 4, length: 0)
        button.sendActions(for: .touchDown); XCTAssertEqual(controller.layoutProxy.native.text, "abc")
        time += 0.4; button.advance(); XCTAssertEqual(controller.layoutProxy.native.text, "ab")
        button.sendActions(for: .touchDragExit); time += 1; button.advance()
        XCTAssertEqual(controller.layoutProxy.native.text, "ab")
        button.sendActions(for: .touchDown); controller.viewWillDisappear(false); time += 1; button.advance()
        XCTAssertEqual(controller.layoutProxy.native.text, "a")
        controller.viewWillAppear(false); controller.developmentBuffer("protected")
        controller.layoutProxy.identityAvailable = false; controller.textDidChange(nil)
        button.sendActions(for: .touchDown); time += 1; button.advance()
        XCTAssertEqual(controller.developmentBufferSource.text, "protected")
    }
    func testRepeatClockCancelsAndDoesNotCatchUpOrFireOnRelease() {
        var press = RepeatingPress(); press.begin(at: 0)
        XCTAssertFalse(press.advance(to: 0.399)); XCTAssertTrue(press.advance(to: 0.4))
        XCTAssertFalse(press.advance(to: 0.474)); XCTAssertTrue(press.advance(to: 5))
        XCTAssertFalse(press.advance(to: 5)); press.cancel(); XCTAssertFalse(press.advance(to: 10))
    }
    func testRepeatButtonCancellationAndVisiblePressedState() {
        let button = RepeatKeycapButton()
        var time: TimeInterval = 0, count = 0
        button.clock = { time }; button.onPressBegan = { true }
        button.onDelete = { count += 1; return true }
        for event in [UIControl.Event.touchUpInside, .touchUpOutside, .touchCancel, .touchDragExit] {
            button.sendActions(for: .touchDown)
            XCTAssertTrue(button.isHighlighted)
            let first = count
            time += 0.4; button.advance(); XCTAssertEqual(count, first + 1)
            time += 0.074; button.advance(); XCTAssertEqual(count, first + 1)
            time += 0.002; button.advance(); XCTAssertEqual(count, first + 2)
            button.sendActions(for: event); XCTAssertFalse(button.isHighlighted)
            time += 1; button.advance(); XCTAssertEqual(count, first + 2)
        }
        button.sendActions(for: .touchDown); let first = count
        button.isEnabled = false; time += 1; button.advance(); XCTAssertEqual(count, first)
        button.isEnabled = true; button.sendActions(for: .touchDown)
        button.didMoveToWindow(); time += 1; button.advance(); XCTAssertEqual(count, first + 1)
    }
    func testCustomProfileLayoutUsesItsHandOrderAndActualHitAndAccessibilityFrames() throws {
        var profile = ChordProfile.builtIn.copy()
        // A custom profile can put keys across the familiar screen midpoint.
        swap(&profile.leftKeys, &profile.rightKeys)
        let surface = KeySurface(); surface.profile = profile; surface.chordMode = true
        for layout in ChordLayout.allCases {
            surface.chordLayout = layout
            surface.frame = CGRect(x: 0, y: 0, width: 310, height: KeyboardGeometry.height(layout: layout, chord: true, numeric: false, emoji: false, landscape: false))
            surface.layoutIfNeeded()
            let elements = try XCTUnwrap(surface.accessibilityElements)
            for key in profile.leftKeys + profile.rightKeys {
                let frame = try XCTUnwrap(surface.developmentKeyFrames[String(key)])
                XCTAssertEqual(surface.developmentKey(at: CGPoint(x: frame.midX, y: frame.midY)), String(key))
                let element = try XCTUnwrap(elements.compactMap { $0 as? UIAccessibilityElement }.first { $0.accessibilityLabel == String(key).uppercased() })
                XCTAssertEqual(element.accessibilityFrameInContainerSpace, frame)
            }
            XCTAssertEqual(profile.hand(for: "q"), .right); XCTAssertEqual(profile.hand(for: "y"), .left)

        }
    }
    func testAllChordGeometryKeepsSquareKeysAndHandIdentity() throws {
        let profile = ChordProfile.builtIn
        for layout in ChordLayout.allCases {
            for width: CGFloat in [310, 383, 842] {
                let height = KeyboardGeometry.height(layout: layout, chord: true, numeric: false, emoji: false, landscape: width > 600, width: width)
                let geometry = KeyboardGeometry.make(size: CGSize(width: width, height: height), profile: profile, chord: true, numeric: false, layout: layout)
                let keys = Dictionary(uniqueKeysWithValues: geometry.keys)
                XCTAssertEqual(Set(keys.keys), Set((profile.leftKeys + profile.rightKeys).map(String.init)))
                let reference = try XCTUnwrap(keys["q"])
                XCTAssertEqual(reference.width, reference.height, accuracy: 0.001)
                let frames = geometry.keys.map(\.1) + [geometry.emoji, geometry.language].compactMap { $0 }
                for (i, rect) in frames.enumerated() {
                    XCTAssertEqual(rect.size, reference.size)
                    XCTAssertTrue(CGRect(x: 0, y: 0, width: width, height: height).contains(rect))
                    for other in frames.dropFirst(i + 1) { XCTAssertFalse(rect.intersects(other)) }
                }
                XCTAssertEqual(keys["w"]!.minX - keys["q"]!.maxX, 2, accuracy: 0.001)
                XCTAssertEqual(keys["y"]!.minX - keys["t"]!.maxX, layout == .splitOrthogonal ? 14 : 2, accuracy: 0.001)
            }
        }
    }
    func testLayoutSwitchRetiresChordAndPreservesCompositionAndScreenshots() async throws {
        for (width, style) in [(CGFloat(320), UIUserInterfaceStyle.light), (393, .dark), (852, .light)] {
            let (window, controller) = host(width: width); defer { window.isHidden = true }
            controller.overrideUserInterfaceStyle = style; window.overrideUserInterfaceStyle = style
            controller.layoutProxy.keyboardAppearance = style == .dark ? .dark : .light
            // Finish initial UIKit appearance before establishing the test session.
            try await Task.sleep(nanoseconds: 80_000_000)
            controller.developmentChoose(.chord); controller.developmentType("ni")
            for layout in ChordLayout.allCases {
                do {
                    let keys = controller.layoutViews.keys
                    keys.developmentPress("a", end: "s")
                    controller.developmentSetLayout(layout)
                    XCTAssertFalse(keys.isChordActive); XCTAssertEqual(controller.developmentRaw, "ni")
                    controller.developmentBuffer("这是 Buffer 原文，用于检查正交键盘和候选的位置。")
                    XCTAssertEqual(controller.developmentRaw, "ni", "after enabling Buffer")
                    window.layoutIfNeeded(); controller.view.layoutIfNeeded(); keys.layoutIfNeeded()
                    try await Task.sleep(nanoseconds: 40_000_000)
                    XCTAssertEqual(controller.developmentRaw, "ni", "after awaiting layout")
                    let before = keys.convert(keys.bounds, to: window)
                    controller.developmentContent(preedit: "ni", candidates: ["你", "拟", "这个长候选不应缩小字体"])
                    window.layoutIfNeeded(); controller.view.layoutIfNeeded()
                    XCTAssertEqual(keys.convert(keys.bounds, to: window), before)
                    XCTAssertEqual(controller.layoutViews.settings.bounds.width, 32)
                    XCTAssertGreaterThanOrEqual(controller.layoutViews.insert.bounds.width, 24)
                    // drawHierarchy temporarily detaches UIInputViewController's view,
                    // legitimately ending its session. Render layers to keep this session alive.
                    let image = UIGraphicsImageRenderer(bounds: controller.view.bounds).image { controller.view.layer.render(in: $0.cgContext) }
                    XCTAssertEqual(controller.developmentRaw, "ni", "after snapshot")
                    let build = Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "development"
                    let name = "build\(build)-\(layout.rawValue)-\(Int(width))-\(style == .dark ? "dark" : "light")"
                    let attachment = XCTAttachment(image: image); attachment.name = name; attachment.lifetime = .keepAlways; add(attachment)
                    let url = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent(name + ".png")
                    try image.pngData()?.write(to: url)
                }
            }
        }
    }

    func testInlineHostCompositionCommitsAndBackspacesWithoutDuplicates() throws {
        let (window, controller) = host(); defer { window.isHidden = true }
        controller.developmentChoose(.pinyin)
        let proxy = controller.layoutProxy
        proxy.native.text = "前😀后"; proxy.native.selectedRange = NSRange(location: 3, length: 0)
        controller.developmentType("nihao")
        let range = try XCTUnwrap(proxy.native.markedTextRange)
        XCTAssertEqual(proxy.native.text(in: range)?.replacingOccurrences(of: " ", with: ""), "nihao")
        XCTAssertTrue(proxy.native.text.hasPrefix("前😀")); XCTAssertTrue(proxy.native.text.hasSuffix("后"))
        let index = try XCTUnwrap(controller.layoutViews.candidates.buttons.firstIndex { $0.currentTitle == "你好" })
        controller.layoutViews.candidates.onSelect?(index)
        XCTAssertEqual(proxy.native.text, "前😀你好后"); XCTAssertNil(proxy.native.markedTextRange)
        controller.developmentType("ni")
        let delete = controller.developmentDelete
        delete.sendActions(for: .touchDown); delete.sendActions(for: .touchUpInside); delete.sendActions(for: .touchDown); delete.sendActions(for: .touchUpInside)
        XCTAssertEqual(proxy.native.text, "前😀你好后"); XCTAssertNil(proxy.native.markedTextRange)
        controller.developmentType("hao"); controller.viewWillDisappear(false)
        XCTAssertEqual(proxy.native.text, "前😀你好后"); XCTAssertNil(proxy.native.markedTextRange)
    }
    func testInlineCompositionNeverEditsDifferentTargetOrMovedSelection() {
        let (window, controller) = host(); defer { window.isHidden = true }
        controller.developmentChoose(.pinyin)
        let proxy = controller.layoutProxy
        controller.developmentType("ni")
        let count = proxy.markedUpdates.count
        proxy.native.text = "新输入框"; proxy.documentIdentifier = UUID()
        controller.textDidChange(nil)
        XCTAssertEqual(proxy.native.text, "新输入框"); XCTAssertEqual(proxy.markedUpdates.count, count)
        controller.developmentType("ni")
        let beforeAppearance = proxy.markedUpdates.count
        proxy.native.text = "再次切换"; proxy.documentIdentifier = UUID()
        controller.viewWillAppear(false)
        XCTAssertEqual(proxy.native.text, "再次切换"); XCTAssertEqual(proxy.markedUpdates.count, beforeAppearance)
        controller.developmentType("hao")
        proxy.native.unmarkText(); proxy.native.selectedRange = NSRange(location: 0, length: 0)
        let text = proxy.native.text, updates = proxy.markedUpdates.count
        controller.selectionWillChange(nil)
        XCTAssertEqual(proxy.native.text, text); XCTAssertEqual(proxy.markedUpdates.count, updates)
        XCTAssertTrue(controller.layoutViews.candidates.buttons.isEmpty)
        controller.viewWillDisappear(false)
        XCTAssertEqual(proxy.native.text, text); XCTAssertEqual(proxy.markedUpdates.count, updates)
    }
    func testBufferCompositionStaysOutOfSourceUntilCandidateConfirmation() throws {
        let (window, controller) = host(); defer { window.isHidden = true }
        controller.developmentChoose(.pinyin); controller.developmentBuffer("前😀后")
        controller.developmentBufferCursor(2)
        let before = controller.developmentBufferSource
        controller.developmentType("nihao")
        XCTAssertEqual(controller.developmentBufferSource.text, before.text)
        XCTAssertEqual(controller.developmentBufferSource.revision, before.revision)
        XCTAssertTrue(controller.layoutProxy.markedUpdates.isEmpty)
        XCTAssertTrue(controller.layoutProxy.insertions.isEmpty)
        XCTAssertEqual(controller.layoutViews.source.text.replacingOccurrences(of: " ", with: ""), "前😀nihao后")
        XCTAssertFalse(controller.layoutViews.insert.isEnabled)
        let index = try XCTUnwrap(controller.layoutViews.candidates.buttons.firstIndex { $0.currentTitle == "你好" })
        controller.layoutViews.candidates.onSelect?(index)
        XCTAssertEqual(controller.developmentBufferSource.text, "前😀你好后")
        XCTAssertEqual(controller.layoutViews.source.text, "前😀你好后")
        XCTAssertTrue(controller.layoutProxy.native.text.isEmpty)
        XCTAssertTrue(controller.layoutViews.insert.isEnabled)
    }
    func testOldFieldProxyIsUnmarkedWithoutChangingEitherFieldsContent() {
        let (window, controller) = host(); defer { window.isHidden = true }
        controller.developmentChoose(.pinyin); controller.developmentType("ni")
        let old = controller.layoutProxy
        XCTAssertNotNil(old.native.markedTextRange)
        let next = LayoutTestDocumentProxy(); next.native.text = "新输入框"
        controller.layoutProxy = next; controller.textWillChange(nil); controller.textDidChange(nil)
        XCTAssertNil(old.native.markedTextRange); XCTAssertEqual(old.native.text, "ni")
        XCTAssertEqual(next.native.text, "新输入框"); XCTAssertTrue(next.markedUpdates.isEmpty)
    }
    func testRefocusedMarkedFieldRecoversDuringNilDocumentReset() throws {
        let (window, controller) = host(); defer { window.isHidden = true }
        controller.developmentChoose(.pinyin)
        let proxy = controller.layoutProxy
        proxy.native.setMarkedText("ni", selectedRange: NSRange(location: 2, length: 0))
        proxy.identityAvailable = false
        controller.textDidChange(nil)
        XCTAssertNil(proxy.native.markedTextRange); XCTAssertEqual(proxy.native.text, "ni")
        proxy.identityAvailable = true; proxy.documentIdentifier = UUID()
        controller.textDidChange(nil); controller.developmentType("hao")
        XCTAssertEqual(proxy.native.text, "nihao")
        let range = try XCTUnwrap(proxy.native.markedTextRange)
        XCTAssertEqual(proxy.native.text(in: range), "hao")
        controller.layoutViews.candidates.onSelect?(0)
        XCTAssertEqual(proxy.native.text, "ni好"); XCTAssertNil(proxy.native.markedTextRange)
        proxy.native.selectedRange = NSRange(location: 1, length: 1); proxy.identityAvailable = false
        controller.textDidChange(nil)
        XCTAssertEqual(proxy.native.text, "ni好"); XCTAssertEqual(proxy.native.selectedRange, NSRange(location: 1, length: 1))
    }
    func testBufferCompositionUsesUTF16RangesForEmojiAndInlineUnderline() {
        let prefix = "前👩🏽‍💻"
        let display = BufferComposition(source: prefix + "后", cursor: 2, preedit: "ni hao", font: .systemFont(ofSize: 15))
        XCTAssertEqual(display.text.string, prefix + "ni hao后")
        XCTAssertEqual(display.markedRange, NSRange(location: prefix.utf16.count, length: 6))
        XCTAssertEqual(display.caretRange.location, prefix.utf16.count + 6)
        XCTAssertEqual(display.text.attribute(.underlineStyle, at: display.markedRange.location, effectiveRange: nil) as? Int, NSUnderlineStyle.single.rawValue)
        XCTAssertNil(display.text.attribute(.underlineStyle, at: 0, effectiveRange: nil))
        let empty = BufferComposition(source: "", cursor: 50, preedit: "", font: .systemFont(ofSize: 15))
        XCTAssertEqual(empty.text.string, ""); XCTAssertEqual(empty.caretRange, NSRange(location: 0, length: 0)); XCTAssertEqual(empty.markedRange.length, 0)
    }
    func testChordCapsShareDimensionsIncludingRightHandUtilities() throws {
        var custom = ChordProfile.builtIn.copy()
        custom.leftKeys = "qwertyuiopasdfg"; custom.rightKeys = "hjklzxcvbnm,."
        custom.mappings = [.init(keys: "ah", output: "ni", kind: .syllable)]
        custom = try custom.validated()
        for profile in [ChordProfile.builtIn, custom] {
            for (width, height): (CGFloat, CGFloat) in [(310, 150), (365, 150), (383, 150), (842, 108)] {
                let keys = KeySurface(frame: .init(x: 0, y: 0, width: width, height: height))
                keys.profile = profile; keys.chordMode = true; keys.layoutIfNeeded()
                let frames = keys.developmentKeyFrames, reference = try XCTUnwrap(frames["q"])
                XCTAssertEqual(frames.count, 30)
                for (name, frame) in frames {
                    XCTAssertEqual(frame.width, reference.width, accuracy: 0.001, name)
                    XCTAssertEqual(frame.height, reference.height, accuracy: 0.001, name)
                    XCTAssertTrue(keys.bounds.contains(frame), name)
                }
                for utility in ["emoji", "mode"] {
                    let frame = try XCTUnwrap(frames[utility])
                    XCTAssertNil(keys.developmentKey(at: CGPoint(x: frame.midX, y: frame.midY)))
                }
                if profile.id == ChordProfile.builtIn.id {
                    XCTAssertEqual(frames["emoji"]!.minX, frames["p"]!.minX, accuracy: 0.001)
                    XCTAssertEqual(frames["mode"]!.minX, frames["p"]!.minX, accuracy: 0.001)
                    let ordinaryGap = frames["w"]!.minX - frames["q"]!.maxX
                    XCTAssertEqual(frames["y"]!.minX - frames["t"]!.maxX, ordinaryGap, accuracy: 0.001)
                    XCTAssertEqual(frames["h"]!.minX - frames["g"]!.maxX, ordinaryGap, accuracy: 0.001)
                }
            }
        }
    }
    func testChordUtilitiesIgnoreHeldChordsAndCancelledPresses() throws {
        let keys = KeySurface(frame: .init(x: 0, y: 0, width: 310, height: 150))
        keys.chordMode = true; keys.layoutIfNeeded()
        func control(_ id: String) throws -> UIControl {
            try XCTUnwrap(keys.subviews.first { $0.accessibilityIdentifier == id } as? UIControl)
        }
        func tap(_ button: UIControl) { button.sendActions(for: .touchDown); button.sendActions(for: .touchUpInside) }
        let emoji = try control("keyboard.emoji"), script = try control("keyboard.mode")
        var toggles = 0, inserted = [String]()
        keys.onLanguageToggle = { toggles += 1 }; keys.onEmoji = { inserted.append($0) }
        keys.developmentPress("a", end: "s")
        tap(script); tap(emoji)
        XCTAssertEqual(toggles, 0); XCTAssertFalse(keys.emojiMode)
        XCTAssertFalse(script.accessibilityActivate()); XCTAssertEqual(emoji.alpha, 0.3, accuracy: 0.001)
        keys.retire(); keys.layoutIfNeeded()
        script.sendActions(for: .touchDown); keys.retire(); script.sendActions(for: .touchUpInside)
        XCTAssertEqual(toggles, 0)
        script.sendActions(for: .touchDown); script.sendActions(for: .touchCancel); script.sendActions(for: .touchUpInside)
        XCTAssertEqual(toggles, 0)
        XCTAssertTrue(script.accessibilityActivate()); XCTAssertEqual(toggles, 1)
        let before = keys.developmentKeyFrames
        tap(emoji); keys.layoutIfNeeded(); XCTAssertTrue(keys.emojiMode)
        let preset = try control("keyboard.emoji.preset.0")
        tap(preset); XCTAssertEqual(inserted, ["😀"])
        for case let button as UIButton in keys.subviews where !button.isHidden {
            button.layoutIfNeeded()
            if let text = button.titleLabel?.text, let font = button.titleLabel?.font {
                XCTAssertGreaterThanOrEqual(button.titleLabel!.bounds.width, ceil((text as NSString).size(withAttributes: [.font: font]).width))
            }
        }
        XCTAssertTrue(try control("keyboard.emoji.back").accessibilityActivate())
        keys.layoutIfNeeded(); XCTAssertFalse(keys.emojiMode); XCTAssertEqual(keys.developmentKeyFrames, before)
        XCTAssertEqual(emoji.alpha, 1)
    }
    func testEmojiUsesBufferAndExplicitDelivery() throws {
        let (window, controller) = host(); defer { window.isHidden = true }
        controller.developmentBuffer("")
        let keys = controller.layoutViews.keys
        keys.chordMode = true; window.layoutIfNeeded(); keys.layoutIfNeeded()
        let emoji = try XCTUnwrap(keys.subviews.first { $0.accessibilityIdentifier == "keyboard.emoji" })
        XCTAssertTrue(emoji.accessibilityActivate()); keys.layoutIfNeeded()
        let preset = try XCTUnwrap(keys.subviews.first { $0.accessibilityIdentifier == "keyboard.emoji.preset.0" })
        XCTAssertTrue(preset.accessibilityActivate())
        XCTAssertTrue(controller.layoutProxy.insertions.isEmpty)
        XCTAssertTrue(controller.layoutViews.insert.accessibilityActivate())
        XCTAssertEqual(controller.layoutProxy.insertions, ["😀"])
        controller.developmentBuffer(nil); window.layoutIfNeeded()
        XCTAssertTrue(preset.accessibilityActivate())
        XCTAssertEqual(controller.layoutProxy.insertions, ["😀", "😀"])
    }
    private func host(width: CGFloat = 393) -> (UIWindow, KeyboardViewController) {
        let window = UIWindow(frame: CGRect(x: 0, y: 0, width: width, height: 874))
        let parent = UIViewController(); window.rootViewController = parent; window.makeKeyAndVisible()
        let controller = KeyboardViewController(); controller.layoutNeedsInputModeSwitchKey = false
        parent.addChild(controller); parent.view.addSubview(controller.view); controller.didMove(toParent: parent)
        controller.view.translatesAutoresizingMaskIntoConstraints = false
        NSLayoutConstraint.activate([controller.view.leadingAnchor.constraint(equalTo: parent.view.leadingAnchor), controller.view.trailingAnchor.constraint(equalTo: parent.view.trailingAnchor), controller.view.bottomAnchor.constraint(equalTo: parent.view.bottomAnchor)])
        controller.developmentResetPreferences()
        controller.developmentContent(); controller.developmentBuffer(nil); window.layoutIfNeeded()
        return (window, controller)
    }
    func testReservedCandidateRowAndBufferRowOrder() throws {
        let (window, controller) = host(); defer { window.isHidden = true }
        controller.developmentChoose(.chord); window.layoutIfNeeded()
        let v = controller.layoutViews
        func frame(_ view: UIView) -> CGRect { view.convert(view.bounds, to: window) }
        let keyFrame = frame(v.keys), candidateFrame = frame(v.candidates), idleHeight = controller.view.bounds.height
        XCTAssertFalse(v.candidates.isHidden); XCTAssertGreaterThanOrEqual(candidateFrame.height, 32)
        XCTAssertTrue(v.buffer.isHidden)
        XCTAssertEqual(frame(v.settings).minY, candidateFrame.minY)
        XCTAssertEqual(frame(v.settings).minX, frame(controller.view).minX + 5)
        XCTAssertGreaterThan(frame(v.candidates.scroll).minX, frame(v.settings).maxX)
        XCTAssertEqual(candidateFrame.minY, frame(controller.view).minY + 5, accuracy: 0.5)
        let schemes = try XCTUnwrap(v.settings.menu?.children.first as? UIMenu)
        XCTAssertEqual(schemes.children.count, InputScheme.allCases.count)
        XCTAssertEqual((schemes.children.compactMap { $0 as? UIAction }.first { $0.state == .on })?.title, InputScheme.chord.title)
        let toggle = try XCTUnwrap(v.candidates.superview?.subviews.first { $0.accessibilityIdentifier == "keyboard.buffer" })
        XCTAssertGreaterThan(frame(toggle).minX, frame(v.candidates.scroll).maxX)
        XCTAssertEqual(frame(toggle).maxX, frame(controller.view).maxX - 5)
        controller.developmentContent(preedit: "ni", candidates: ["你", "拟"]); window.layoutIfNeeded()
        XCTAssertEqual(controller.view.bounds.height, idleHeight)
        XCTAssertEqual(frame(v.keys), keyFrame)
        XCTAssertTrue(v.settings.isHidden); XCTAssertTrue(toggle.isHidden)
        XCTAssertTrue(controller.developmentShortcuts.isHidden)
        XCTAssertEqual(frame(v.candidates).minX, frame(controller.view).minX + 5)
        XCTAssertEqual(frame(v.candidates).maxX, frame(controller.view).maxX - 5)
        XCTAssertEqual(frame(v.candidates.scroll).minX, frame(v.candidates).minX)
        XCTAssertEqual(frame(v.candidates.scroll).maxX, frame(v.candidates.expandButton).minX)
        XCTAssertEqual(frame(v.candidates).height, candidateFrame.height)
        controller.developmentContent(); controller.developmentBuffer("", plugin: true); window.layoutIfNeeded()
        XCTAssertFalse(v.settings.isHidden); XCTAssertFalse(toggle.isHidden)
        XCTAssertEqual(frame(v.candidates).width, candidateFrame.width)
        XCTAssertFalse(v.buffer.isHidden); XCTAssertFalse(v.result.isHidden); XCTAssertFalse(v.source.isHidden)
        // Plugins: their output (what gets sent) on top beside Send, the input line below.
        XCTAssertLessThan(frame(v.result).maxY, frame(v.source).minY)
        XCTAssertLessThan(frame(v.source).maxY, frame(v.candidates).minY)
        XCTAssertLessThan(frame(v.candidates).maxY, frame(v.keys).minY)
        XCTAssertEqual(frame(v.keys), keyFrame)
        let plugin = try XCTUnwrap(v.buffer.subviews.first { $0.accessibilityIdentifier == "keyboard.plugin" })
        XCTAssertGreaterThan(frame(plugin).minX, frame(v.source).maxX)
        XCTAssertEqual(frame(plugin).minY, frame(v.source).minY)
        XCTAssertGreaterThan(frame(v.insert).minX, frame(v.result).maxX)
        // Send, plugin and Buffer switch form one right-hand column.
        XCTAssertEqual(frame(v.insert).minX, frame(plugin).minX, accuracy: 0.5)
        XCTAssertEqual(frame(plugin).minX, frame(toggle).minX, accuracy: 0.5)
        XCTAssertEqual(frame(v.insert).minY, frame(v.result).minY)
        // The settings key sits left of the input line.
        let settings = try XCTUnwrap(v.buffer.subviews.first { $0.accessibilityIdentifier == "keyboard.buffer.settings" })
        XCTAssertFalse(settings.isHidden); XCTAssertLessThan(frame(settings).maxX, frame(v.source).minX)
        XCTAssertEqual(frame(settings).minY, frame(v.source).minY)
        XCTAssertEqual(v.bottom.spacing, 2)
        controller.developmentBuffer("原文。", plugin: false); window.layoutIfNeeded()
        // Default sends what you type: the input line moves up beside Send, the typing readout below it.
        XCTAssertLessThan(frame(v.source).maxY, frame(v.result).minY)
        XCTAssertEqual(frame(v.insert).minY, frame(v.source).minY)
        XCTAssertEqual(frame(settings).minY, frame(v.result).minY)
        XCTAssertEqual(frame(v.source).minX, frame(v.result).minX, accuracy: 0.5)
        XCTAssertEqual(v.result.text, ""); XCTAssertFalse(v.result.isHidden); XCTAssertFalse(v.source.isHidden)
        XCTAssertEqual(v.source.role, .input); XCTAssertEqual(v.result.role, .output)
        XCTAssertEqual(v.source.text, "原文。")
        XCTAssertFalse(v.buffer.subviews.contains { String(describing: type(of: $0)).contains("BlockStrip") })
        let stats = try XCTUnwrap(v.result.subviews.first { $0.accessibilityIdentifier == "keyboard.buffer.metrics" } as? UILabel)
        XCTAssertFalse(stats.isHidden)
        XCTAssertEqual(stats.textAlignment, .center); XCTAssertGreaterThanOrEqual(stats.font.pointSize, 16)
        XCTAssertEqual(stats.frame.height, v.result.frame.height)
        XCTAssertEqual(stats.frame.midX, v.result.bounds.midX, accuracy: 0.5)
        XCTAssertEqual(v.buffer.bounds.height, 76)
        controller.developmentBuffer("原文。", plugin: true, output: "Text."); window.layoutIfNeeded()
        XCTAssertEqual(v.result.text, "Text."); XCTAssertFalse(v.result.isHidden); XCTAssertFalse(v.source.isHidden); XCTAssertTrue(stats.isHidden)
        controller.developmentBuffer(nil); window.layoutIfNeeded()
        XCTAssertTrue(v.buffer.isHidden); XCTAssertEqual(frame(v.keys), keyFrame)
        XCTAssertEqual(controller.view.bounds.height, idleHeight)
    }
    func testSystemGlobeOnlyWhenRequiredAndSpaceReceivesFreedWidth() {
        let (window, controller) = host(); defer { window.isHidden = true }
        controller.developmentChoose(.chord); window.layoutIfNeeded()
        let views = controller.layoutViews
        let space = views.bottom.arrangedSubviews.first { $0.accessibilityIdentifier == "keyboard.spaces" }!
        let without = space.bounds.width
        XCTAssertTrue(views.globe.isHidden)
        controller.layoutNeedsInputModeSwitchKey = true; controller.developmentContent(); window.layoutIfNeeded()
        XCTAssertFalse(views.globe.isHidden); XCTAssertLessThan(space.bounds.width, without)
        let otherWidths = views.bottom.arrangedSubviews.filter { $0 !== space && !$0.isHidden }.map { $0.bounds.width }
        XCTAssertTrue(otherWidths.allSatisfy { abs($0 - otherWidths[0]) <= 0.5 }, "Pixel-aligned widths: \(otherWidths)")
        controller.layoutNeedsInputModeSwitchKey = false; controller.developmentContent(); window.layoutIfNeeded()
        XCTAssertTrue(views.globe.isHidden); XCTAssertEqual(space.bounds.width, without, accuracy: 0.01)
    }
    func testCandidateWidthsUseTextAndExpandedRowsWrap() {
        let texts = ["你", "你好", "你好世界", String(repeating: "长候选", count: 12), "好"]
        for width: CGFloat in [274, 347, 806] {
            for landscape in [false, true] {
                let collapsed = CandidateLayout.measure(texts, width: width, expanded: false, landscape: landscape)
                let expanded = CandidateLayout.measure(texts, width: width, expanded: true, landscape: landscape)
                XCTAssertGreaterThan(collapsed.frames[2].width, collapsed.frames[1].width)
                XCTAssertGreaterThan(collapsed.frames[1].width, collapsed.frames[0].width)
                XCTAssertEqual(Set(collapsed.frames.map(\.minY)).count, 1)
                XCTAssertTrue(expanded.frames.allSatisfy { $0.minX >= 0 && $0.maxX <= width })
                XCTAssertGreaterThan(expanded.contentSize.height, expanded.frames[0].height)
                XCTAssertLessThanOrEqual(expanded.height, 110)
                let font = UIFont.systemFont(ofSize: landscape ? 18 : 20)
                let glyphWidth = (texts[1] as NSString).size(withAttributes: [.font: font]).width
                XCTAssertEqual(collapsed.frames[1].width, ceil(glyphWidth) + CandidateLayout.padding)
            }
        }
        XCTAssertEqual(CandidateLayout.measure([], width: 300, expanded: true, landscape: false).height, 0)
    }
    func testCandidateGlyphsFitActualLabelsAndPressDoesNotDrift() {
        let strip = CandidateStrip(); strip.update(["你", "你好", "你好世界"])
        strip.frame = CGRect(x: 0, y: 0, width: 393, height: 32); strip.layoutIfNeeded()
        for button in strip.buttons {
            button.layoutIfNeeded()
            let label = button.titleLabel!
            let width = (label.text! as NSString).size(withAttributes: [.font: label.font!]).width
            XCTAssertGreaterThanOrEqual(label.bounds.width, ceil(width))
            XCTAssertGreaterThanOrEqual(label.bounds.height, ceil(label.font.lineHeight))
            let normal = label.frame
            button.isHighlighted = true; button.layoutIfNeeded()
            XCTAssertEqual(label.frame, normal)
            button.setNeedsLayout(); button.layoutIfNeeded()
            XCTAssertEqual(label.frame, normal)
            button.isHighlighted = false; button.layoutIfNeeded(); XCTAssertEqual(label.frame, normal)
            XCTAssertEqual(button.backgroundColor, .clear)
            XCTAssertEqual(button.layer.borderWidth, 0); XCTAssertEqual(button.layer.shadowOpacity, 0)
        }
    }
    func testInsertionTapHoldThresholdCancellationAndNoDuplicateRelease() {
        var press = InsertionPress()
        press.begin(at: 0); XCTAssertNil(press.advance(to: 0.999))
        XCTAssertEqual(press.end(at: 0.999, inside: true), .next)
        press.begin(at: 2); XCTAssertEqual(press.advance(to: 3), .all)
        XCTAssertNil(press.advance(to: 4)); XCTAssertNil(press.end(at: 4, inside: true))
        // A delayed run-loop timer cannot convert a held press back into a tap.
        press.begin(at: 5); XCTAssertEqual(press.end(at: 6, inside: true), .all)
        press.begin(at: 7); press.cancel(); XCTAssertNil(press.advance(to: 8)); XCTAssertNil(press.end(at: 8, inside: true))
        press.begin(at: 9); XCTAssertNil(press.end(at: 9.5, inside: false)); XCTAssertNil(press.advance(to: 10))
        press.begin(at: 11); XCTAssertEqual(press.end(at: 11.1, inside: true), .next)
    }
    func testMissingObjectiveCDocumentIdentityIsSafeAndHasNoFallbackTarget() {
        let stub = NullableDocumentIdentity()
        XCTAssertNil(DocumentIdentity.readObject(stub))
        let id = UUID(); stub.documentIdentifier = id as NSUUID
        XCTAssertEqual(DocumentIdentity.readObject(stub), id)
        stub.documentIdentifier = nil
        XCTAssertNil(DocumentIdentity.readObject(stub))
        XCTAssertNil(DocumentIdentity.readObject(NSObject()))
    }
    func testBufferInsertionRoutesBlocksAndRejectsStalePress() {
        let (window, controller) = host(); defer { window.isHidden = true }
        controller.developmentBuffer("第一块。第二块。第三块。"); window.layoutIfNeeded()
        let button = controller.layoutViews.insert
        XCTAssertTrue(button.isEnabled)
        XCTAssertTrue(button.accessibilityActivate())
        XCTAssertEqual(controller.layoutProxy.insertions, ["第一块。"])
        button.onPressBegan?(); button.onInsert?(.all)
        XCTAssertEqual(controller.layoutProxy.insertions, ["第一块。", "第二块。第三块。"])
        XCTAssertFalse(button.isEnabled)
        controller.developmentBuffer("源文。", plugin: true, output: "First!Second!")
        XCTAssertTrue(button.accessibilityActivate())
        XCTAssertEqual(controller.layoutProxy.insertions.last, "First!")
        button.onPressBegan?()
        controller.developmentBuffer("已修改原文。", plugin: true) // Old result must never be sent.
        button.onInsert?(.all)
        XCTAssertEqual(controller.layoutProxy.insertions.count, 3)
        XCTAssertFalse(button.isEnabled)
        controller.developmentBuffer("正在处理。", plugin: true, generating: true)
        XCTAssertFalse(button.isEnabled); XCTAssertFalse(controller.layoutViews.stop.isHidden)
        controller.developmentBuffer("新的原文。")
        button.onPressBegan?(); controller.layoutProxy.documentIdentifier = UUID()
        button.onInsert?(.all)
        XCTAssertEqual(controller.layoutProxy.insertions.count, 3)
    }
}

@MainActor private final class NullableDocumentIdentity: NSObject {
    @objc var documentIdentifier: NSUUID?
}
