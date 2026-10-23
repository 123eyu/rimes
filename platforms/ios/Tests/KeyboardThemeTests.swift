import XCTest
import UIKit
import RimesCore
@testable import RIMES

@MainActor final class KeyboardThemeTests: XCTestCase {
    func testMigrationKeepsExistingPetAndChordPreferencesAndHonorsExplicitAppSelectionOnce() throws {
        for pet in StatusSkin.themes {
            var prefs = KeyboardPreferences()
            prefs.select(.chord); prefs.chordLayout = .splitOrthogonal
            prefs.statusSkin = pet.rawValue; prefs.keySounds = false
            prefs.reconcileTheme(.init())
            XCTAssertEqual(prefs.resolvedTheme, pet)
            XCTAssertEqual(prefs.scheme, .chord); XCTAssertEqual(prefs.chordLayout, .splitOrthogonal)
            XCTAssertFalse(prefs.keySounds)
            let saved = KeyboardThemeConfiguration(selection: .fox, revision: UUID())
            prefs.reconcileTheme(saved); XCTAssertEqual(prefs.resolvedTheme, .fox)
            prefs.statusSkin = StatusSkin.panda.rawValue
            prefs.reconcileTheme(saved); XCTAssertEqual(prefs.resolvedTheme, .panda)
            let restored = try JSONDecoder().decode(KeyboardPreferences.self, from: JSONEncoder().encode(prefs))
            XCTAssertEqual(restored.resolvedTheme, .panda)
            XCTAssertEqual(restored.appliedKeyboardThemeRevision, saved.revision)
            XCTAssertEqual(restored.keyboardThemeMigrationVersion, 1)
        }
        var old = try JSONDecoder().decode(KeyboardPreferences.self, from: Data("{}".utf8))
        old.reconcileTheme(.init()); XCTAssertEqual(old.resolvedTheme, .apple)
        old = .init(); old.keyboardSkin = .rimes
        old.reconcileTheme(.init()); XCTAssertEqual(old.resolvedTheme, .rhino)
        old.reconcileTheme(.init()); XCTAssertEqual(old.resolvedTheme, .rhino)
        XCTAssertEqual(StatusSkin.rotation(["light", "apple", "unknown", "rhino"]), [.apple, .rhino])
    }

    func testThemeFileDoesNotChangeLayoutsAndOrdinaryLayoutEditsDoNotResetTheme() throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: root) }
        let custom = CustomLayoutStore(root: root), appearance = KeyboardAppearanceStore(root: root)
        let theme = KeyboardThemeStore(root: root)
        try custom.apply(CustomKeyboardLayout.templates[0])
        let layoutBefore = try Data(contentsOf: root.appendingPathComponent("keyboard-layouts-v1.json"))
        try appearance.save(.init(layout: .nineKey, skin: .system))
        let appearanceBefore = appearance.load()
        let saved = try theme.save(.rhino)
        XCTAssertEqual(theme.load(), saved)
        XCTAssertEqual(appearance.load(), appearanceBefore)
        XCTAssertEqual(try Data(contentsOf: root.appendingPathComponent("keyboard-layouts-v1.json")), layoutBefore)
        try appearance.save(.init(layout: .qwerty, skin: .system))
        XCTAssertEqual(theme.load(), saved)
    }

    func testEveryPetHasLegibleKeyAndPressedColorsInBothAppearances() {
        XCTAssertEqual(StatusSkin.themes.count, 18)
        for style: UIUserInterfaceStyle in [.light, .dark] {
            let traits = UITraitCollection(userInterfaceStyle: style)
            for pet in StatusSkin.themes {
                let palette = pet.palette
                for background in [palette.key, palette.functional] {
                    XCTAssertGreaterThanOrEqual(KeyboardPalette.contrast(palette.ink.resolvedColor(with: traits), background.resolvedColor(with: traits)), 4.5, "\(pet), \(style)")
                }
                // Native uses Apple's familiar white-on-blue feedback; pet accents choose black or white.
                if pet != .apple {
                    XCTAssertGreaterThanOrEqual(KeyboardPalette.contrast(palette.accentInk.resolvedColor(with: traits), palette.accent.resolvedColor(with: traits)), 4.5, "\(pet), \(style)")
                }
                XCTAssertGreaterThanOrEqual(KeyboardPalette.contrast(palette.pressedSelectedInk.resolvedColor(with: traits), palette.pressedSelected.resolvedColor(with: traits)), 4.5)
            }
        }
    }

    func testThemeAndPetCycleRecolorEveryLayoutWithoutMovingKeysOrChangingComposition() throws {
        let window = UIWindow(frame: CGRect(x: 0, y: 0, width: 393, height: 900))
        let parent = UIViewController(); window.rootViewController = parent; window.makeKeyAndVisible()
        defer { window.isHidden = true }
        let keyboard = KeyboardViewController(); keyboard.layoutNeedsInputModeSwitchKey = false
        keyboard.loadViewIfNeeded(); keyboard.developmentResetPreferences()
        parent.addChild(keyboard); parent.view.addSubview(keyboard.view); keyboard.didMove(toParent: parent)
        for mode in 0..<4 {
            keyboard.developmentChoose(mode < 2 ? .pinyin : .chord)
            keyboard.developmentOrdinaryAppearance(layout: mode >= 2 ? .orthogonal : mode == 1 ? .nineKey : .qwerty)
            keyboard.developmentSetLayout(mode == 3 ? .splitOrthogonal : .orthogonal)
            keyboard.developmentType(mode == 1 ? "64" : "ni")
            window.layoutIfNeeded()
            let keys = keyboard.layoutViews.keys.developmentKeyFrames
            let raw = keyboard.developmentRaw
            let functions = keyboard.developmentStandardFunctions.mapValues(\.frame)
            for theme in StatusSkin.themes {
                keyboard.developmentSkin(theme); window.layoutIfNeeded()
                XCTAssertEqual(keyboard.layoutViews.keys.theme, theme)
                XCTAssertEqual(keyboard.layoutViews.keys.developmentKeyFrames, keys)
                XCTAssertEqual(keyboard.developmentStandardFunctions.mapValues(\.frame), functions)
                XCTAssertEqual(keyboard.developmentRaw, raw)
                if mode >= 2 { XCTAssertTrue(keyboard.layoutViews.keys.resolvesChords) }
                let caps = descendants(keyboard.view).compactMap { $0 as? KeycapButton }.filter { !$0.isHidden }
                XCTAssertFalse(caps.isEmpty)
                for cap in caps {
                    XCTAssertEqual(cap.theme, theme, cap.accessibilityIdentifier ?? "cap")
                    guard cap.isEnabled else { continue }
                    cap.isHighlighted = true
                    let traits = cap.traitCollection
                    let ink = cap.tintColor!.resolvedColor(with: traits)
                    let expected = (cap.isSelected && (cap.skin != .system || cap.accentCap) ? theme.palette.pressedSelectedInk : theme.palette.accentInk).resolvedColor(with: traits)
                    XCTAssertEqual(ink, expected)
                    cap.isHighlighted = false
                }
            }
        }
        keyboard.developmentBuffer("")
        keyboard.developmentSkin(.apple)
        let pet = try XCTUnwrap(descendants(keyboard.view).first { $0 is StatusLight } as? StatusLight)
        pet.onCycleSkin?()
        XCTAssertEqual(pet.skin, .rhino); XCTAssertEqual(keyboard.layoutViews.keys.theme, .rhino)
        XCTAssertEqual(keyboard.layoutViews.insert.theme, .rhino)
    }
    private func descendants(_ view: UIView) -> [UIView] { [view] + view.subviews.flatMap(descendants) }
}
