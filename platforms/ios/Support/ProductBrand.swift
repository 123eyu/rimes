import Foundation

/// Display only. App Groups, Keychain Groups, package and schema IDs stay frozen.
enum ProductBrand {
    static func name(language: String) -> String {
        let language = language.lowercased().replacingOccurrences(of: "_", with: "-")
        guard language == "zh" || language.hasPrefix("zh-") else { return "XSIME" }
        let traditional = !language.contains("hans") &&
            (language.contains("hant") || ["tw", "hk", "mo"].contains(language.split(separator: "-").last.map(String.init) ?? ""))
        return traditional ? "犀術IME" : "犀术IME"
    }
    static var name: String { name(language: Locale.preferredLanguages.first ?? "en") }
}
