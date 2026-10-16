import Cocoa

enum MacShiftKeyRouting {
    private static func unmodifiedShift(_ event: NSEvent) -> Bool {
        let flags = event.modifierFlags.intersection(.deviceIndependentFlagsMask)
        return flags.contains(.shift)
            && flags.intersection([.control, .option, .command, .function]).isEmpty
    }

    static func directText(for event: NSEvent) -> String? {
        guard unmodifiedShift(event), let text = event.characters, !text.isEmpty,
              text.unicodeScalars.allSatisfy({ CharacterSet.letters.contains($0) })
        else { return nil }
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
