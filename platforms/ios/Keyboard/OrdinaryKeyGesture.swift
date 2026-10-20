import UIKit

/// A hold must start on one cap before an upward slide can select its alternate.
/// Moving early keeps ordinary slide-to-correct typing and cannot become a hold later.
struct OrdinaryKeyGesture {
    static let holdDuration: TimeInterval = 0.32
    static let upwardDistance: CGFloat = 18
    static let holdSlop: CGFloat = 12

    let initialKey: String
    let origin: CGPoint
    let beganAt: TimeInterval
    let alternate: String?
    let direct: Bool
    let horizontalTolerance: CGFloat
    private(set) var key: String
    private(set) var armed = false
    private(set) var selected = false
    private var eligible = true

    init(key: String, origin: CGPoint, at time: TimeInterval, alternate: String?, keyWidth: CGFloat, direct: Bool = false) {
        initialKey = key; self.key = key; self.origin = origin; beganAt = time
        self.direct = direct
        self.alternate = alternate; horizontalTolerance = max(24, keyWidth * 0.8)
    }

    @discardableResult mutating func arm(at time: TimeInterval) -> Bool {
        guard !direct, !armed, eligible, alternate != nil, time - beganAt >= Self.holdDuration else { return false }
        armed = true; return true
    }
    mutating func move(to point: CGPoint, key hit: String?, at time: TimeInterval) {
        arm(at: time)
        if direct && eligible && alternate != nil && origin.y - point.y >= Self.upwardDistance && abs(point.x - origin.x) <= horizontalTolerance { armed = true }
        if armed {
            selected = origin.y - point.y >= Self.upwardDistance && abs(point.x - origin.x) <= horizontalTolerance
            key = initialKey
        } else {
            if (direct ? (abs(point.x - origin.x) > horizontalTolerance || point.y - origin.y > Self.holdSlop) : (hypot(point.x - origin.x, point.y - origin.y) > Self.holdSlop || (hit != nil && hit != initialKey))) { eligible = false }
            if let hit { key = hit }
        }
    }
    var selectedAlternate: String? { selected ? alternate : nil }

    private static let englishAlternates = mapping([",", ".", "?", "!", ":", ";", "/", "(", ")", "\"", "'", "<", ">", "-", "…", "@"])
    private static let chineseAlternates = mapping(["，", "。", "？", "！", "：", "；", "、", "（", "）", "“", "”", "《", "》", "—", "…", "·"])
    private static func mapping(_ punctuation: [String]) -> [String: String] {
        Dictionary(uniqueKeysWithValues: zip(Array("qwertyuiopasdfghjklzxcvbnm").map(String.init), ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0"] + punctuation))
    }
    static func alternate(for key: String, nineKey: Bool, english: Bool) -> String? {
        if nineKey {
            let values = english ? [",", ".", "?", "!", ":", ";", "/", "…"] : ["，", "。", "？", "！", "：", "；", "、", "…"]
            guard let digit = Int(key), (2...9).contains(digit) else { return nil }
            return values[digit - 2]
        }
        return (english ? englishAlternates : chineseAlternates)[key.lowercased()]
    }
}
