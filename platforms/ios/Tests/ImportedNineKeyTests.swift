import XCTest
import UIKit
import ZIPFoundation
import RimesCore
@testable import RIMES

@MainActor final class ImportedNineKeyTests: XCTestCase {
    private let fullSchema = "fixture_pinyin"
    private let nineKeySchema = "fixture_pinyin_t9"
    private let importedWord = "万象测试词"

    func testPrimarySelectionDiscoversItsDeployedNineKeyDependencyAndKeepsImportedDictionary() async throws {
        let root = temporaryRoot()
        defer { try? FileManager.default.removeItem(at: root) }
        let store = RimeSchemeStore(root: root)
        let package = try await installFixture(store: store)
        defer { removeUserData(for: package) }
        let selection = RimeSchemeSelection(packageID: package.id, schemaID: fullSchema)
        XCTAssertEqual(package.schemas.map(\.id), [fullSchema], "A layout dependency must not become another unsolicited selectable input scheme.")
        XCTAssertEqual(store.keyboardLayouts(for: selection)?.qwertySchemaID, fullSchema)
        XCTAssertEqual(store.keyboardLayouts(for: selection)?.nineKeySchemaID, nineKeySchema)
        XCTAssertNil(store.load().active, "Importing must not activate a scheme or change the app's default.")

        let engine = MobileEngine()
        XCTAssertTrue(engine.selectImported(selection, store: store, schemaID: nineKeySchema))
        var state = type("64426", into: engine)
        let index = try XCTUnwrap(state.candidates.firstIndex(of: importedWord), "The numeric layout must use the imported dictionary: \(state.candidates)")
        XCTAssertEqual(engine.candidate(index).commit, importedWord)
        engine.clear()
        XCTAssertTrue(engine.selectImported(selection, store: store, schemaID: fullSchema))
        state = type("nihao", into: engine)
        XCTAssertTrue(state.candidates.contains(importedWord), "Returning to 26 keys must retain the same imported dictionary: \(state.candidates)")
        engine.clear()
    }

    func testImportedNineKeySelectsSpellingDeletesAndCommitsInHostAndBuffer() async throws {
        let root = temporaryRoot()
        defer { try? FileManager.default.removeItem(at: root) }
        let store = RimeSchemeStore(root: root)
        let package = try await installFixture(store: store)
        defer { removeUserData(for: package) }
        let selection = RimeSchemeSelection(packageID: package.id, schemaID: fullSchema)
        for buffered in [false, true] {
            let (window, keyboard) = host()
            defer { window.isHidden = true }
            keyboard.developmentOrdinaryAppearance(layout: .nineKey)
            keyboard.developmentChooseImported(selection, store: store)
            if buffered { keyboard.developmentBuffer("") }
            XCTAssertEqual(keyboard.layoutViews.keys.standardMode, .nineKey)
            XCTAssertEqual(keyboard.developmentImportedSelection, selection)

            keyboard.developmentType("64426")
            XCTAssertEqual(keyboard.developmentRaw, "64426")
            XCTAssertTrue(candidateTitles(keyboard).contains(importedWord), "The layout must not silently fall back to the built-in Pinyin dictionary.")
            keyboard.developmentSelectNineKeySpelling("ni")
            XCTAssertEqual(keyboard.developmentRaw, "ni'426")
            keyboard.developmentBackspace()
            XCTAssertEqual(keyboard.developmentRaw, "ni'42")
            keyboard.developmentType("6")
            keyboard.developmentSelectNineKeySpelling("hao")
            XCTAssertEqual(keyboard.developmentRaw, "ni'hao'")
            keyboard.developmentBackspace()
            XCTAssertEqual(keyboard.developmentRaw, "ni'426", "Deleting a selected syllable's separator must restore its numeric ambiguity.")
            keyboard.developmentBackspace()
            XCTAssertEqual(keyboard.developmentRaw, "ni'42")
            keyboard.developmentType("6")
            let candidate = try XCTUnwrap(keyboard.layoutViews.candidates.buttons.first { $0.accessibilityLabel == importedWord })
            candidate.sendActions(for: .touchUpInside)
            await keyboard.developmentWaitForDelivery()
            XCTAssertEqual(buffered ? keyboard.developmentBufferSource.text : keyboard.layoutProxy.native.text, importedWord)
            XCTAssertTrue(keyboard.developmentRaw.isEmpty)
            if buffered { XCTAssertTrue(keyboard.layoutProxy.native.text.isEmpty) }

            keyboard.developmentOrdinaryAppearance(layout: .qwerty)
            XCTAssertEqual(keyboard.layoutViews.keys.standardMode, .qwerty)
            XCTAssertEqual(keyboard.developmentImportedSelection, selection)
            keyboard.developmentType("nihao")
            XCTAssertTrue(candidateTitles(keyboard).contains(importedWord), "Changing layout must keep the original full-Pinyin schema and dictionary.")
            keyboard.developmentChoose(.pinyin)
        }
    }

    func testImportedNineKeyNumericAndEnglishModesKeepLiteralInput() async throws {
        let root = temporaryRoot()
        defer { try? FileManager.default.removeItem(at: root) }
        let store = RimeSchemeStore(root: root)
        let package = try await installFixture(store: store)
        defer { removeUserData(for: package) }
        let selection = RimeSchemeSelection(packageID: package.id, schemaID: fullSchema)
        let (window, keyboard) = host()
        defer { window.isHidden = true }
        keyboard.developmentOrdinaryAppearance(layout: .nineKey)
        keyboard.developmentChooseImported(selection, store: store)
        keyboard.developmentNumeric()
        keyboard.developmentType("123")
        XCTAssertEqual(keyboard.layoutProxy.native.text, "123")
        XCTAssertTrue(keyboard.developmentRaw.isEmpty)
        keyboard.developmentNumeric()
        keyboard.developmentLanguage()
        keyboard.developmentType("abc")
        XCTAssertEqual(keyboard.layoutProxy.native.text, "123abc")
        XCTAssertEqual(keyboard.layoutViews.keys.standardMode, .qwerty)
        XCTAssertTrue(keyboard.developmentRaw.isEmpty)
        keyboard.developmentLanguage()
        XCTAssertEqual(keyboard.layoutViews.keys.standardMode, .nineKey)
        XCTAssertEqual(keyboard.developmentImportedSelection, selection)
        keyboard.developmentChoose(.pinyin)
    }

    func testImportedNineKeyChoiceLeavesExistingChordGeometryAndMappingUntouched() async throws {
        let root = temporaryRoot()
        defer { try? FileManager.default.removeItem(at: root) }
        let store = RimeSchemeStore(root: root)
        let package = try await installFixture(store: store)
        defer { removeUserData(for: package) }
        let (window, keyboard) = host()
        defer { window.isHidden = true }
        keyboard.developmentChoose(.chord)
        keyboard.developmentSetLayout(.splitOrthogonal)
        window.layoutIfNeeded()
        let frames = keyboard.layoutViews.keys.developmentKeyFrames
        let area = keyboard.layoutViews.keys.frame
        keyboard.developmentOrdinaryAppearance(layout: .nineKey)
        keyboard.developmentChooseImported(.init(packageID: package.id, schemaID: fullSchema), store: store)
        window.layoutIfNeeded()
        XCTAssertEqual(keyboard.layoutViews.keys.standardMode, .nineKey)
        XCTAssertFalse(keyboard.layoutViews.keys.resolvesChords)
        keyboard.developmentChoose(.chord)
        window.layoutIfNeeded()
        XCTAssertNil(keyboard.developmentImportedSelection)
        XCTAssertEqual(keyboard.layoutViews.keys.chordLayout, .splitOrthogonal)
        XCTAssertTrue(keyboard.layoutViews.keys.resolvesChords)
        XCTAssertEqual(keyboard.layoutViews.keys.developmentKeyFrames, frames)
        XCTAssertEqual(keyboard.layoutViews.keys.frame, area)
    }

    func testImportedFullPinyinMenuEnablesNineKeyWhenNativeDependencyExists() async throws {
        let root = temporaryRoot()
        defer { try? FileManager.default.removeItem(at: root) }
        let store = RimeSchemeStore(root: root)
        let package = try await installFixture(store: store)
        defer { removeUserData(for: package) }
        let (window, keyboard) = host()
        defer { window.isHidden = true }
        keyboard.developmentChooseImported(.init(packageID: package.id, schemaID: fullSchema), store: store)
        let action = try XCTUnwrap(menuActions(keyboard.layoutViews.settings.menu).first { $0.title == "9 键 · 全拼" })
        XCTAssertFalse(action.attributes.contains(.disabled))
        XCTAssertNil(action.subtitle)
        XCTAssertEqual(action.state, .off)
        keyboard.developmentOrdinaryAppearance(layout: .nineKey)
        let selected = try XCTUnwrap(menuActions(keyboard.layoutViews.settings.menu).first { $0.title == "9 键 · 全拼" })
        XCTAssertEqual(selected.state, .on)
        XCTAssertEqual(keyboard.layoutViews.keys.standardMode, .nineKey)
        keyboard.developmentChoose(.pinyin)
    }

    func testExplicitNativeNineKeySelectionStartsOnNineKeysAndReturnCommitsImportedWord() async throws {
        let root = temporaryRoot()
        defer { try? FileManager.default.removeItem(at: root) }
        let store = RimeSchemeStore(root: root)
        let package = try await installFixture(store: store, selectedSchemaIDs: [nineKeySchema])
        defer { removeUserData(for: package) }
        XCTAssertEqual(package.schemas.map(\.id), [nineKeySchema])
        let selection = RimeSchemeSelection(packageID: package.id, schemaID: nineKeySchema)
        for buffered in [false, true] {
            let (window, keyboard) = host()
            defer { window.isHidden = true }
            keyboard.developmentOrdinaryAppearance(layout: .qwerty)
            keyboard.developmentChooseImported(selection, store: store)
            if buffered { keyboard.developmentBuffer("") }
            XCTAssertEqual(keyboard.layoutViews.keys.standardMode, .nineKey, "Selecting an explicitly imported native T9 schema must choose its matching layout.")
            XCTAssertEqual(keyboard.developmentImportedSelection, selection)
            keyboard.developmentType("64426")
            XCTAssertEqual(candidateTitles(keyboard).first, importedWord)
            keyboard.developmentEnter()
            await keyboard.developmentWaitForDelivery()
            XCTAssertEqual(buffered ? keyboard.developmentBufferSource.text : keyboard.layoutProxy.native.text, importedWord)
            XCTAssertTrue(keyboard.developmentRaw.isEmpty)
            if buffered { XCTAssertTrue(keyboard.layoutProxy.native.text.isEmpty, "The first Return confirms into Buffer; it must not deliver Buffer into the host.") }
            keyboard.developmentChoose(.pinyin)
        }
    }

    func testCapabilityDetectionDoesNotTreatTableSchemasOrDigitAlphabetsAsNineKeyPinyin() async throws {
        for translator in ["table_translator", "script_translator"] {
            let root = temporaryRoot()
            defer { try? FileManager.default.removeItem(at: root) }
            let store = RimeSchemeStore(root: root)
            // Even a schema with a T9-shaped algebra is not Pinyin when its
            // engine uses table_translator (for example Wubi or shape codes).
            let package = try await installFixture(store: store, translator: translator, includeNineKeyDependency: translator == "table_translator")
            defer { removeUserData(for: package) }
            let selection = RimeSchemeSelection(packageID: package.id, schemaID: fullSchema)
            XCTAssertNil(store.keyboardLayouts(for: selection)?.nineKeySchemaID, "An alphabet containing digits is insufficient evidence of T9 Pinyin support.")
            let (window, keyboard) = host()
            defer { window.isHidden = true }
            keyboard.developmentOrdinaryAppearance(layout: .nineKey)
            keyboard.developmentChooseImported(selection, store: store)
            XCTAssertEqual(keyboard.layoutViews.keys.standardMode, .qwerty)
            XCTAssertEqual(keyboard.developmentImportedSelection, selection)
            let action = try XCTUnwrap(menuActions(keyboard.layoutViews.settings.menu).first { $0.title == "9 键 · 全拼" })
            XCTAssertTrue(action.attributes.contains(.disabled))
            XCTAssertFalse(action.subtitle?.isEmpty ?? true, "A disabled imported layout must explain that the package does not provide its encoder.")
            keyboard.developmentChoose(.pinyin)
        }
    }

    func testExistingLibraryJSONWithoutLayoutMetadataStillResolvesNineKeyDependency() async throws {
        let root = temporaryRoot()
        defer { try? FileManager.default.removeItem(at: root) }
        let store = RimeSchemeStore(root: root)
        let package = try await installFixture(store: store)
        defer { removeUserData(for: package) }
        let selection = RimeSchemeSelection(packageID: package.id, schemaID: fullSchema)
        // This is the persisted format shipped before imported layouts were supported.
        let legacy: [String: Any] = [
            "packages": [["id": package.id, "name": "Legacy imported Pinyin", "schemas": [["id": fullSchema, "name": "全拼"]],
                          "importedAt": 0, "sourceDigest": String(repeating: "a", count: 64), "warnings": [String]()]],
            "active": ["packageID": package.id, "schemaID": fullSchema], "revision": "legacy-revision"
        ]
        try JSONSerialization.data(withJSONObject: legacy).write(to: root.appendingPathComponent("library-v1.json"))
        XCTAssertEqual(store.load().active, selection)
        XCTAssertEqual(store.load().revision, "legacy-revision")
        XCTAssertNotNil(store.resolve(selection))
        XCTAssertEqual(store.keyboardLayouts(for: selection)?.nineKeySchemaID, nineKeySchema)
    }

    /// Opt-in acceptance of the delivered ZIP: copy it into the simulator test
    /// host's Documents/wanxiang-ninekey-acceptance.zip before running this test.
    /// Deployment and user data use independent temporary IDs; the app's real
    /// library and active input scheme are never changed.
    func testDeliveredWanxiangPackageSupportsNineKeyAndFullPinyinInHostAndBuffer() async throws {
        let files = FileManager.default
        let fixture = files.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("wanxiang-ninekey-acceptance.zip")
        guard files.fileExists(atPath: fixture.path) else {
            throw XCTSkip("Optional actual-package acceptance: copy the delivered Wanxiang ZIP to the simulator test host's Documents/wanxiang-ninekey-acceptance.zip.")
        }
        let root = temporaryRoot()
        defer { try? files.removeItem(at: root) }
        let store = RimeSchemeStore(root: root)
        let review = try RimeSchemeImportService.inspect(url: fixture)
        let package = try await RimeSchemeInstaller.install(review: review, selectedSchemaIDs: ["wanxiang"], store: store) { _ in }
        defer { removeUserData(for: package) }
        let selection = RimeSchemeSelection(packageID: package.id, schemaID: "wanxiang")
        XCTAssertEqual(package.schemas.map(\.id), ["wanxiang"])
        XCTAssertEqual(store.keyboardLayouts(for: selection)?.qwertySchemaID, "wanxiang")
        XCTAssertEqual(store.keyboardLayouts(for: selection)?.nineKeySchemaID, "wanxiang_t9")
        XCTAssertNil(store.load().active)

        for buffered in [false, true] {
            let (window, keyboard) = host()
            defer { window.isHidden = true }
            keyboard.developmentOrdinaryAppearance(layout: .nineKey)
            keyboard.developmentChooseImported(selection, store: store)
            if buffered { keyboard.developmentBuffer("") }
            XCTAssertEqual(keyboard.developmentImportedSelection, selection)
            XCTAssertEqual(keyboard.layoutViews.keys.standardMode, .nineKey)
            let action = try XCTUnwrap(menuActions(keyboard.layoutViews.settings.menu).first { $0.title == "9 键 · 全拼" })
            XCTAssertFalse(action.attributes.contains(.disabled))
            XCTAssertEqual(action.state, .on)
            keyboard.developmentType("64426")
            XCTAssertEqual(keyboard.developmentRaw, "64426")
            XCTAssertTrue(candidateTitles(keyboard).contains("你好"), "Actual Wanxiang nine-key candidates: \(candidateTitles(keyboard))")
            window.layoutIfNeeded()
            XCTAssertEqual(keyboard.developmentRaw, "64426", "Layout must preserve the live composition before taking evidence.")
            let renderer = UIGraphicsImageRenderer(bounds: keyboard.view.bounds)
            // drawHierarchy detaches UIInputViewController's view and ends its
            // session. Render layers so evidence capture preserves composition.
            let image = renderer.image { context in
                keyboard.view.layer.render(in: context.cgContext)
            }
            XCTAssertEqual(keyboard.developmentRaw, "64426", "Taking evidence must not detach the keyboard or retire its input session.")
            let attachment = XCTAttachment(image: image)
            attachment.name = "wanxiang-nine-key-\(buffered ? "buffer" : "host")"
            attachment.lifetime = .keepAlways
            add(attachment)

            keyboard.developmentSelectNineKeySpelling("ni")
            XCTAssertEqual(keyboard.developmentRaw, "ni'426")
            keyboard.developmentBackspace()
            XCTAssertEqual(keyboard.developmentRaw, "ni'42")
            keyboard.developmentType("6")
            XCTAssertEqual(keyboard.developmentRaw, "ni'426")
            let candidate = try XCTUnwrap(keyboard.layoutViews.candidates.buttons.first { $0.accessibilityLabel == "你好" }, "Actual Wanxiang constrained candidates: \(candidateTitles(keyboard))")
            candidate.sendActions(for: .touchUpInside)
            await keyboard.developmentWaitForDelivery()
            XCTAssertEqual(buffered ? keyboard.developmentBufferSource.text : keyboard.layoutProxy.native.text, "你好")
            XCTAssertTrue(keyboard.developmentRaw.isEmpty)
            if buffered { XCTAssertTrue(keyboard.layoutProxy.native.text.isEmpty) }

            keyboard.developmentOrdinaryAppearance(layout: .qwerty)
            XCTAssertEqual(keyboard.layoutViews.keys.standardMode, .qwerty)
            XCTAssertEqual(keyboard.developmentImportedSelection, selection)
            keyboard.developmentType("nihao")
            XCTAssertTrue(candidateTitles(keyboard).contains("你好"), "Actual Wanxiang full-Pinyin candidates: \(candidateTitles(keyboard))")
            keyboard.developmentChoose(.pinyin)
        }
        XCTAssertNil(store.load().active, "Acceptance testing must not activate the real or temporary scheme library.")
    }

    private func installFixture(store: RimeSchemeStore, translator: String = "script_translator", includeNineKeyDependency: Bool = true, selectedSchemaIDs: [String]? = nil) async throws -> RimeSchemePackage {
        let alphabet = "abcdefghijklmnopqrstuvwxyz23456789"
        func schema(_ id: String, nineKey: Bool) -> String {
            """
            schema:
              schema_id: \(id)
              name: \(nineKey ? "全拼九键" : "全拼")
              version: '1.0'
              dependencies: [\(!nineKey && includeNineKeyDependency ? nineKeySchema : "")]
            switches:
              - name: ascii_mode
                reset: 0
                states: [中文, English]
            engine:
              processors: [ascii_composer, speller, selector, navigator, express_editor]
              segmentors: [ascii_segmentor, abc_segmentor, fallback_segmentor]
              translators: [\(translator)]
            speller:
              alphabet: \(alphabet)
              initials: \(alphabet)
              delimiter: " '"
              algebra: \(nineKey ? "['derive/^(.*)$/\\U$1/', 'xlit/ABCDEFGHIJKLMNOPQRSTUVWXYZ/22233344455566677778889999/']" : "[]")
            translator:
              dictionary: \(fullSchema)
              prism: \(id)
              enable_user_dict: false
              enable_sentence: false
              enable_completion: false
              spelling_hints: 99
              always_show_comments: true
              preedit_format: []
            menu:
              page_size: 5
            """
        }
        let dictionary = """
        ---
        name: \(fullSchema)
        version: '1.0'
        sort: original
        use_preset_vocabulary: false
        ...
        \(importedWord)\tni hao\t100000
        你好\tni hao\t100
        你\tni\t100
        好\thao\t100

        """
        var files = [("\(fullSchema).schema.yaml", schema(fullSchema, nineKey: false)), ("\(fullSchema).dict.yaml", dictionary)]
        if includeNineKeyDependency { files.append(("\(nineKeySchema).schema.yaml", schema(nineKeySchema, nineKey: true))) }
        let archive = try Archive(data: Data(), accessMode: .create)
        for (path, source) in files {
            let data = Data(source.utf8)
            try archive.addEntry(with: path, type: .file, uncompressedSize: Int64(data.count), compressionMethod: .none) { position, size in
                data.subdata(in: Int(position)..<(Int(position) + size))
            }
        }
        let review = try RimeSchemeImportService.inspect(data: XCTUnwrap(archive.data), sourceName: "Imported Pinyin fixture")
        let primary = try XCTUnwrap(review.schemes.first { $0.id == fullSchema })
        XCTAssertTrue(primary.blockingIssues.isEmpty, "\(primary.blockingIssues)")
        return try await RimeSchemeInstaller.install(review: review, selectedSchemaIDs: selectedSchemaIDs ?? [fullSchema], store: store) { _ in }
    }

    private func type(_ text: String, into engine: MobileEngine) -> EngineSnapshot {
        var result = EngineSnapshot()
        for scalar in text.unicodeScalars { result = engine.process(key: Int32(scalar.value)) }
        return result
    }
    private func candidateTitles(_ keyboard: KeyboardViewController) -> [String] {
        keyboard.layoutViews.candidates.buttons.compactMap(\.accessibilityLabel)
    }
    private func menuActions(_ menu: UIMenu?) -> [UIAction] {
        menu?.children.flatMap { element -> [UIAction] in
            if let action = element as? UIAction { return [action] }
            return menuActions(element as? UIMenu)
        } ?? []
    }
    private func temporaryRoot() -> URL {
        FileManager.default.temporaryDirectory.appendingPathComponent("ImportedNineKeyTests-" + UUID().uuidString, isDirectory: true)
    }
    private func removeUserData(for package: RimeSchemePackage) {
        let user = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("RimeImported/\(package.id)", isDirectory: true)
        try? FileManager.default.removeItem(at: user)
    }
    private func host() -> (UIWindow, KeyboardViewController) {
        let window = UIWindow(frame: CGRect(x: 0, y: 0, width: 393, height: 900))
        let parent = UIViewController()
        window.rootViewController = parent
        window.makeKeyAndVisible()
        let keyboard = KeyboardViewController()
        keyboard.layoutNeedsInputModeSwitchKey = false
        parent.addChild(keyboard)
        parent.view.addSubview(keyboard.view)
        keyboard.didMove(toParent: parent)
        NSLayoutConstraint.activate([
            keyboard.view.leadingAnchor.constraint(equalTo: parent.view.leadingAnchor),
            keyboard.view.trailingAnchor.constraint(equalTo: parent.view.trailingAnchor),
            keyboard.view.bottomAnchor.constraint(equalTo: parent.view.bottomAnchor)
        ])
        keyboard.developmentResetPreferences()
        keyboard.developmentSetCustomLayout(nil)
        keyboard.developmentContent()
        keyboard.developmentBuffer(nil)
        window.layoutIfNeeded()
        return (window, keyboard)
    }
}
