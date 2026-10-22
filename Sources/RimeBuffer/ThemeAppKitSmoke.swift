import AppKit
import Darwin

private final class FixedAccentSwitchActionProbe: NSObject {
    private(set) var count = 0
    private(set) weak var lastSender: RimeFixedAccentSwitch?

    @objc func changed(_ sender: RimeFixedAccentSwitch) {
        count += 1
        lastSender = sender
    }
}

private final class FixedAccentChoiceActionProbe: NSObject {
    private(set) var count = 0
    private(set) weak var lastSender: RimeFixedAccentChoiceButton?

    @objc func changed(_ sender: RimeFixedAccentChoiceButton) {
        count += 1
        lastSender = sender
    }
}

private final class FixedAccentPopUpActionProbe: NSObject {
    private(set) var count = 0

    @objc func changed(_ sender: RimeFixedAccentPopUpButton) {
        count += 1
    }
}

private func fixedAccentSwitchKeyEvent(
    modifiers: NSEvent.ModifierFlags = [],
    isRepeat: Bool = false
) -> NSEvent? {
    NSEvent.keyEvent(
        with: .keyDown,
        location: .zero,
        modifierFlags: modifiers,
        timestamp: 0,
        windowNumber: 0,
        context: nil,
        characters: " ",
        charactersIgnoringModifiers: " ",
        isARepeat: isRepeat,
        keyCode: 49
    )
}

/// AppKit interaction coverage for the product-owned accent controls and the
/// live Settings appearance observer. This intentionally uses no Rime engine,
/// client proxy, network provider, or live input-method state.
func runThemeAppKitSmokeTest() -> Bool {
    print("== \(ProductIdentity.displayName) theme AppKit smoke test ==")
    var ok = true
    func check(_ condition: @autoclosure () -> Bool, _ message: String) {
        if !condition() {
            print("FAILED: \(message)")
            ok = false
        }
    }

    check(Thread.isMainThread, "AppKit smoke must run on the main thread")
    // `NSControl.sendAction` is routed through NSApplication even with an
    // explicit target, so launch the minimal accessory application before
    // exercising the control.
    let app = NSApplication.shared
    app.setActivationPolicy(.accessory)
    app.finishLaunching()

    let control = RimeFixedAccentSwitch(frame: .zero)
    let probe = FixedAccentSwitchActionProbe()
    control.target = probe
    control.action = #selector(FixedAccentSwitchActionProbe.changed(_:))
    control.setAccessibilityLabel("启用实时翻译")

    check(control.state == .off, "switch should begin off")
    check(control.accessibilityValue() as? NSNumber == NSNumber(value: false),
          "off state should expose a false NSNumber accessibility value")
    check(control.accessibilityRole() == .button,
          "switch should expose the AppKit button accessibility role")
    check(control.accessibilitySubrole() == .switch,
          "switch should expose the switch accessibility subrole")
    check(control.accessibilityLabel() == "启用实时翻译",
          "switch should retain a readable explicit accessibility label")

    control.performClick(nil)
    check(control.state == .on, "performClick should toggle on")
    check(probe.count == 1 && probe.lastSender === control,
          "performClick should send its configured action exactly once")
    check(control.accessibilityValue() as? NSNumber == NSNumber(value: true),
          "on state should expose a true NSNumber accessibility value")

    control.performClick(nil)
    check(control.state == .off && probe.count == 2,
          "a second performClick should toggle off with one more action")

    control.state = .mixed
    check(control.state == .on,
          "the two-state switch should normalize mixed to on")
    check(control.accessibilityValue() as? NSNumber == NSNumber(value: true),
          "normalized mixed state should expose true to accessibility")
    control.state = .off

    if let space = fixedAccentSwitchKeyEvent() {
        let before = probe.count
        control.keyDown(with: space)
        check(control.state == .on && probe.count == before + 1,
              "bare Space should toggle and dispatch exactly once")
    } else {
        check(false, "could not construct a bare Space key event")
    }

    if let controlSpace = fixedAccentSwitchKeyEvent(modifiers: .control) {
        let stateBefore = control.state
        let actionCountBefore = probe.count
        control.keyDown(with: controlSpace)
        check(control.state == stateBefore && probe.count == actionCountBefore,
              "Control+Space must remain a host shortcut")
    } else {
        check(false, "could not construct a Control+Space key event")
    }

    if let repeatedSpace = fixedAccentSwitchKeyEvent(isRepeat: true) {
        let stateBefore = control.state
        let actionCountBefore = probe.count
        control.keyDown(with: repeatedSpace)
        check(control.state == stateBefore && probe.count == actionCountBefore,
              "repeated Space must not toggle or dispatch")
    } else {
        check(false, "could not construct a repeated Space key event")
    }

    control.isEnabled = false
    let disabledState = control.state
    let disabledActionCount = probe.count
    control.performClick(nil)
    check(control.state == disabledState && probe.count == disabledActionCount,
          "a disabled switch must ignore performClick")
    check(!control.accessibilityPerformPress(),
          "a disabled switch must reject accessibility press")

    let checkbox = RimeFixedAccentChoiceButton.checkbox(title: "启用缓冲模式")
    let checkboxProbe = FixedAccentChoiceActionProbe()
    checkbox.target = checkboxProbe
    checkbox.action = #selector(FixedAccentChoiceActionProbe.changed(_:))

    check(checkbox.state == .off,
          "checkbox should begin off")
    check(checkbox.accessibilityValue() as? NSNumber == NSNumber(value: false),
          "off checkbox should expose a false NSNumber accessibility value")
    check(checkbox.accessibilityRole() == .checkBox,
          "checkbox should expose the checkbox accessibility role")
    check(checkbox.accessibilityLabel() == "启用缓冲模式",
          "checkbox title should be its readable accessibility label")

    checkbox.performClick(nil)
    check(checkbox.state == .on,
          "checkbox performClick should toggle on")
    check(checkboxProbe.count == 1 && checkboxProbe.lastSender === checkbox,
          "checkbox performClick should dispatch exactly once")
    check(checkbox.accessibilityValue() as? NSNumber == NSNumber(value: true),
          "on checkbox should expose a true NSNumber accessibility value")

    checkbox.performClick(nil)
    check(checkbox.state == .off && checkboxProbe.count == 2,
          "a second checkbox click should toggle off with one more action")

    if let space = fixedAccentSwitchKeyEvent() {
        let before = checkboxProbe.count
        checkbox.keyDown(with: space)
        check(checkbox.state == .on && checkboxProbe.count == before + 1,
              "bare Space should toggle a checkbox and dispatch exactly once")
    } else {
        check(false, "could not construct checkbox Space key event")
    }

    if let controlSpace = fixedAccentSwitchKeyEvent(modifiers: .control) {
        let stateBefore = checkbox.state
        let actionCountBefore = checkboxProbe.count
        checkbox.keyDown(with: controlSpace)
        check(checkbox.state == stateBefore
                && checkboxProbe.count == actionCountBefore,
              "Control+Space must not change or dispatch a checkbox")
    } else {
        check(false, "could not construct checkbox Control+Space key event")
    }

    if let repeatedSpace = fixedAccentSwitchKeyEvent(isRepeat: true) {
        let stateBefore = checkbox.state
        let actionCountBefore = checkboxProbe.count
        checkbox.keyDown(with: repeatedSpace)
        check(checkbox.state == stateBefore
                && checkboxProbe.count == actionCountBefore,
              "repeated Space must not change or dispatch a checkbox")
    } else {
        check(false, "could not construct repeated checkbox Space key event")
    }

    checkbox.isEnabled = false
    let disabledCheckboxState = checkbox.state
    let disabledCheckboxActionCount = checkboxProbe.count
    checkbox.performClick(nil)
    check(checkbox.state == disabledCheckboxState
            && checkboxProbe.count == disabledCheckboxActionCount,
          "a disabled checkbox must ignore performClick")
    check(!checkbox.accessibilityPerformPress(),
          "a disabled checkbox must reject accessibility press")

    let radio = RimeFixedAccentChoiceButton.radio(title: "墨竹")
    let radioProbe = FixedAccentChoiceActionProbe()
    radio.target = radioProbe
    radio.action = #selector(FixedAccentChoiceActionProbe.changed(_:))

    check(radio.state == .off,
          "radio button should begin off")
    check(radio.accessibilityValue() as? NSNumber == NSNumber(value: false),
          "off radio should expose a false NSNumber accessibility value")
    check(radio.accessibilityRole() == .radioButton,
          "radio should expose the radio-button accessibility role")
    check(radio.accessibilityLabel() == "墨竹",
          "radio title should be its readable accessibility label")

    radio.performClick(nil)
    check(radio.state == .on,
          "radio performClick should select the radio")
    check(radioProbe.count == 1 && radioProbe.lastSender === radio,
          "radio performClick should dispatch exactly once")
    check(radio.accessibilityValue() as? NSNumber == NSNumber(value: true),
          "on radio should expose a true NSNumber accessibility value")

    radio.performClick(nil)
    check(radio.state == .on && radioProbe.count == 2,
          "a selected radio should stay on and dispatch once per click")

    radio.state = .off
    if let space = fixedAccentSwitchKeyEvent() {
        let before = radioProbe.count
        radio.keyDown(with: space)
        check(radio.state == .on && radioProbe.count == before + 1,
              "bare Space should select a radio and dispatch exactly once")
    } else {
        check(false, "could not construct radio Space key event")
    }

    if let controlSpace = fixedAccentSwitchKeyEvent(modifiers: .control) {
        let stateBefore = radio.state
        let actionCountBefore = radioProbe.count
        radio.keyDown(with: controlSpace)
        check(radio.state == stateBefore && radioProbe.count == actionCountBefore,
              "Control+Space must not change or dispatch a radio")
    } else {
        check(false, "could not construct radio Control+Space key event")
    }

    if let repeatedSpace = fixedAccentSwitchKeyEvent(isRepeat: true) {
        let stateBefore = radio.state
        let actionCountBefore = radioProbe.count
        radio.keyDown(with: repeatedSpace)
        check(radio.state == stateBefore && radioProbe.count == actionCountBefore,
              "repeated Space must not change or dispatch a radio")
    } else {
        check(false, "could not construct repeated radio Space key event")
    }

    radio.isEnabled = false
    let disabledRadioState = radio.state
    let disabledRadioActionCount = radioProbe.count
    radio.performClick(nil)
    check(radio.state == disabledRadioState
            && radioProbe.count == disabledRadioActionCount,
          "a disabled radio must ignore performClick")
    check(!radio.accessibilityPerformPress(),
          "a disabled radio must reject accessibility press")

    let popup = RimeFixedAccentPopUpButton()
    popup.addItems(withTitles: ["墨竹", "翡翠", "静谧"])
    let popupProbe = FixedAccentPopUpActionProbe()
    popup.target = popupProbe
    popup.action = #selector(FixedAccentPopUpActionProbe.changed(_:))
    check(popup.numberOfItems == 3 && popup.selectedItem?.title == "墨竹",
          "fixed-accent popup should retain native menu selection behavior")
    popup.selectItem(at: 2)
    check(popup.selectedItem?.title == "静谧" && popupProbe.count == 0,
          "programmatic popup selection should not dispatch an action")
    let nativePopup = NSPopUpButton()
    check(popup.accessibilityRole() == nativePopup.accessibilityRole()
            && popup.accessibilitySubrole() == nativePopup.accessibilitySubrole(),
          "fixed-accent popup should preserve native popup accessibility")

    // Exercise the actual Settings observer in one window. Both the environment
    // override and persisted preference are restored so this standalone smoke
    // cannot change the user's selected theme.
    let defaults = UserDefaults.standard
    let appearanceKey = "appearanceMode"
    let previousPreference = defaults.object(forKey: appearanceKey)
    let previousClassicPreference = defaults.object(forKey: "appearanceMode.classic.last.v1")
    defer {
        if let previousClassicPreference {
            defaults.set(previousClassicPreference, forKey: "appearanceMode.classic.last.v1")
        } else {
            defaults.removeObject(forKey: "appearanceMode.classic.last.v1")
        }
    }
    let previousEnvironment = ProcessInfo.processInfo.environment[
        "RIMEBUFFER_APPEARANCE_MODE"
    ]
    unsetenv("RIMEBUFFER_APPEARANCE_MODE")
    defaults.set(RimeAppearanceMode.night.rawValue, forKey: appearanceKey)

    SettingsWindowController.shared.show()
    let settingsWindow = app.windows.first {
        $0.title == "\(ProductIdentity.displayName) 设置"
    }

    var appearanceNotificationCount = 0
    let observer = NotificationCenter.default.addObserver(
        forName: .rimeAppearanceDidChange,
        object: nil,
        queue: nil
    ) { _ in
        appearanceNotificationCount += 1
    }

    func drainMainRunLoop(until condition: () -> Bool) {
        let deadline = Date(timeIntervalSinceNow: 1)
        while !condition(), Date() < deadline {
            _ = RunLoop.main.run(mode: .default, before: Date(timeIntervalSinceNow: 0.01))
        }
    }

    func matches(_ window: NSWindow?, mode: RimeAppearanceMode) -> Bool {
        let expected = mode.appKitAppearanceName(
            increasedContrast: NSWorkspace.shared
                .accessibilityDisplayShouldIncreaseContrast
        )
        return window?.appearance?.name == expected
            && window?.effectiveAppearance.name == expected
    }

    check(settingsWindow != nil && settingsWindow?.isVisible == true,
          "settings smoke should locate the live settings window")
    // The Settings shortcut also dismisses an open window; a closed one stays
    // closed (the caller then opens it) and the window reopens afterwards.
    check(SettingsWindowController.shared.closeIfShownOnActiveSpace(),
          "settings shortcut should close an open settings window")
    check(settingsWindow?.isVisible == false,
          "settings window should be hidden after the shortcut closes it")
    check(!SettingsWindowController.shared.closeIfShownOnActiveSpace(),
          "settings shortcut should not claim to close an already closed window")
    SettingsWindowController.shared.show()
    check(settingsWindow?.isVisible == true,
          "settings window should reopen after being closed by the shortcut")
    check(SettingsWindowController.shared.validateChoiceCardHitTestingForSmoke(),
          "every theme and input-encoding card center should hit its own control")
    check(matches(settingsWindow, mode: .night),
          "settings should initially use 墨竹 AppKit appearance")

    let buffer = BufferWindowController.shared
    let darkBuffer = buffer.themeSurfaceSnapshotForSmoke
    RimeUI.appearance = .day
    drainMainRunLoop { matches(settingsWindow, mode: .day) }
    check(matches(settingsWindow, mode: .day),
          "the same settings window should transition to 翡翠")

    let jadeBuffer = buffer.themeSurfaceSnapshotForSmoke
    check(jadeBuffer.source == jadeBuffer.target
            && jadeBuffer.source != darkBuffer.source,
          "both Buffer action fades must leave the dark palette when switching to Jade")
    check(!jadeBuffer.glass && jadeBuffer.railAlpha == 1,
          "Jade must retain its solid rail without a Glass material")

    RimeUI.appearance = .quiet
    drainMainRunLoop {
        RimeUI.appearance == .quiet && matches(settingsWindow, mode: .quiet)
    }
    check(RimeUI.appearance == .quiet
            && matches(settingsWindow, mode: .quiet),
          "the same settings window should transition to dark 静谧")

    RimeUI.appearance = .night
    drainMainRunLoop { matches(settingsWindow, mode: .night) }
    check(matches(settingsWindow, mode: .night),
          "the same settings window should transition back to 墨竹")
    check(appearanceNotificationCount == 3,
          "墨竹→翡翠→静谧→墨竹 should emit exactly three appearance notifications")

    // Exercise the actual candidate container, not just its palette. Glass is
    // decorative and must never swallow candidate clicks or move controls.
    let candidateSurface = RimeCandidateSurfaceView(isPreedit: false)
    candidateSurface.frame = NSRect(x: 0, y: 0, width: 200, height: 40)
    let candidate = NSButton(title: "1 你好", target: nil, action: nil)
    candidate.frame = NSRect(x: 10, y: 5, width: 100, height: 30)
    candidateSurface.addSubview(candidate)
    let candidateFrame = candidate.frame
    let clipboardPane = MainActor.assumeIsolated {
        ClipboardHistorySmoke.makeThemeProbePane()
    }
    RimeUI.appearance = .liquidGlass
    drainMainRunLoop { settingsWindow?.appearance == nil }
    let clipboardGlassAlpha: CGFloat = RimeUI.usesLiquidGlassTransparency ? 0 : 1
    drainMainRunLoop { clipboardPane.layer?.backgroundColor?.alpha == clipboardGlassAlpha }
    check(clipboardPane.layer?.backgroundColor?.alpha == clipboardGlassAlpha,
          "clipboard content must not cover the Glass backdrop with an opaque fill")
    check(RimeUI.appearance == .liquidGlass
            && defaults.string(forKey: appearanceKey) == "liquidGlass",
          "Glass must persist as an opt-in choice")
    let glassBuffer = buffer.themeSurfaceSnapshotForSmoke
    check(glassBuffer.source.isEmpty && glassBuffer.target.isEmpty
            && glassBuffer.toolbarAlpha == 0,
          "Glass must not cover its material with toolbar or action color plates")
    check(glassBuffer.glass == RimeUI.usesLiquidGlassTransparency
            && glassBuffer.railAlpha == (RimeUI.usesLiquidGlassTransparency ? 0 : 1),
          "Buffer must expose its native material without an opaque rail covering it")
    check(SettingsWindowController.shared.validateTitlebarCoverageForSmoke(),
          "Glass must cover the entire native titlebar while keeping controls below it")
    check(SettingsWindowController.shared.validateSidebarLayoutForSmoke(),
          "sidebar icons, labels, focus rings and click targets must share consistent row geometry")
    let settingsFrame = settingsWindow?.frame
    settingsWindow?.setContentSize(NSSize(width: 860, height: 600))
    check(SettingsWindowController.shared.validateTitlebarCoverageForSmoke(),
          "Glass titlebar coverage must survive resizing to the minimum window size")
    if let settingsFrame { settingsWindow?.setFrame(settingsFrame, display: false) }
    check(settingsWindow?.appearance == nil,
          "Glass should inherit the system appearance")
    check(candidateSurface.layer?.cornerRadius == 12,
          "Glass candidates should use the rounded Apple shape")
    check(candidateSurface.hasVisibleMaterial == RimeUI.usesLiquidGlassTransparency,
          "native or fallback material should respect transparency and contrast settings")
    check(candidateSurface.hitTest(NSPoint(x: 50, y: 20)) === candidate,
          "Glass candidate backing must leave clicks to the candidate button")
    check(SettingsWindowController.shared.validateChoiceCardHitTestingForSmoke(),
          "all settings choices should remain reachable with Glass enabled")
    check(RimeUI.accentBlue == NSColor.controlAccentColor
            && RimeUI.accentSecondary == NSColor.controlAccentColor,
          "Glass primary and secondary accents must use the user's system accent")
    let beforeSystemColorChange = appearanceNotificationCount
    let beforeSystemColorRender = buffer.themeSurfaceSnapshotForSmoke.renderPasses
    NotificationCenter.default.post(name: NSColor.systemColorsDidChangeNotification, object: nil)
    drainMainRunLoop { appearanceNotificationCount > beforeSystemColorChange }
    check(appearanceNotificationCount > beforeSystemColorChange,
          "changing system colors must refresh existing Glass windows without a restart")
    check(buffer.themeSurfaceSnapshotForSmoke.renderPasses > beforeSystemColorRender,
          "system color changes must invalidate cached Buffer text even with the same theme and content")
    let previousAppAppearance = app.appearance
    app.appearance = NSAppearance(named: .aqua)
    check(!RimeUI.isDark && RimeUI.palette.candidateBackground == RimeThemePalettes.day.candidateBackground,
          "Glass must use a readable light palette in system light appearance")
    app.appearance = NSAppearance(named: .darkAqua)
    check(RimeUI.isDark && RimeUI.palette.candidateBackground == RimeThemePalettes.night.candidateBackground,
          "Glass must use a readable dark palette in system dark appearance")
    app.appearance = previousAppAppearance
    RimeUI.selectThemeFamily(.classic)
    drainMainRunLoop { matches(settingsWindow, mode: .night) }
    drainMainRunLoop { clipboardPane.layer?.backgroundColor?.alpha == 1 }
    check(clipboardPane.layer?.backgroundColor?.alpha == 1,
          "clipboard content must restore its opaque background when leaving Glass")
    check(RimeUI.appearance == .night && !candidateSurface.hasVisibleMaterial,
          "switching away from Glass should restore the remembered Classic colorway")
    check(SettingsWindowController.shared.validateTitlebarCoverageForSmoke(),
          "Classic settings must retain the same safe titlebar/content geometry")
    check(candidateSurface.layer?.cornerRadius == 6,
          "leaving Glass should restore the Classic candidate corners")
    check(candidate.frame == candidateFrame && candidate.superview === candidateSurface,
          "theme switching must preserve candidate geometry and hierarchy")
    check(candidateSurface.hitTest(NSPoint(x: 50, y: 20)) === candidate,
          "candidate clicks should survive switching back to a legacy theme")

    NotificationCenter.default.removeObserver(observer)
    settingsWindow?.close()
    if let previousPreference {
        defaults.set(previousPreference, forKey: appearanceKey)
    } else {
        defaults.removeObject(forKey: appearanceKey)
    }
    NotificationCenter.default.post(name: .rimeAppearanceDidChange, object: nil)
    if let previousEnvironment {
        setenv("RIMEBUFFER_APPEARANCE_MODE", previousEnvironment, 1)
    } else {
        unsetenv("RIMEBUFFER_APPEARANCE_MODE")
    }

    if ok { print("theme AppKit smoke: OK") }
    return ok
}
