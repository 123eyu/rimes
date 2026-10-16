import Cocoa

enum MacShiftKeyRouting {
    private static func unmodifiedShift(_ event: NSEvent) -> Bool {
        let flags = event.modifierFlags.intersection(.deviceIndependentFlagsMask)
        return flags.contains(.shift)
            && flags.intersection([.control, .option, .command, .function]).isEmpty
    }

    static func directText(for event: NSEvent) -> String? {
        guard unmodifiedShift(event), let text = event.characters, !text.isEmpty,
              text.unicodeScalars.allSatisfy({
                  $0.value >= 0x20 && $0.value != 0x7f
                      && !(0xf700...0xf8ff).contains($0.value)
              })
        else { return nil }
        // Only a single ASCII symbol takes the schema punctuation path.
        // Preserve composed text and symbols on other keyboard layouts (e.g.
        // German Shift+3 = §), which the physical US key table cannot express.
        if text.unicodeScalars.count == 1, let scalar = text.unicodeScalars.first,
           scalar.value <= 0x7e, !CharacterSet.letters.contains(scalar) { return nil }
        return text
    }

    /// Shift+6 is '^', not '6'. Give the produced printable symbol to Rime so
    /// the schema and ascii_mode/ascii_punct options decide its final form.
    /// Control/Option/Command combinations retain their physical-key bindings.
    static func symbolKeysym(for event: NSEvent) -> Int32? {
        guard unmodifiedShift(event), let scalars = event.characters?.unicodeScalars,
              scalars.count == 1, let scalar = scalars.first,
              (0x20...0x7e).contains(scalar.value) else { return nil }
        return Int32(scalar.value)
    }
}
