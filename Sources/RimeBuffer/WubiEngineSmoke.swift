import Foundation

func runWubiEngineSmokeTest(engine: RimeEngine, session: UInt64) -> Bool {
    for (schema, code, expected, hint) in [
        ("wubi86", "wq", "你", ""),
        ("wubi86", "zni", "你", "wq"),
        ("wubi86", "znihao", "你好", "wqvb"),
        ("lingxi_wubi", "wq", "你", ""),
        ("lingxi_wubi", "nihao", "你好", "wqvb"),
        ("lingxi_wubi", "zni", "你", "wq")
    ] {
        guard engine.selectSchema(schema, session: session) else { return false }
        engine.setOption("ascii_mode", false, session: session)
        engine.clearComposition(session: session)
        for scalar in code.unicodeScalars { _ = engine.processKey(Int32(scalar.value), session: session) }
        let context = engine.getContext(session: session)
        guard let index = context.candidates.firstIndex(where: { $0.text == expected }) else {
            print("FAILED: Wubi candidate \(schema)/\(code)"); return false
        }
        let comment = context.candidates[index].comment
        if !hint.isEmpty && !(comment.hasPrefix("(") && comment.hasSuffix(")") && comment.contains(hint)) {
            print("FAILED: Wubi hint \(schema)/\(code): \(comment)"); return false
        }
        guard (code != "wq" || index == 0),
              engine.selectCandidate(onPage: index, session: session),
              engine.takeCommit(session: session) == expected else {
            print("FAILED: Wubi commit \(schema)/\(code)"); return false
        }
    }
    for schema in ["wubi86", "lingxi_wubi"] {
        guard engine.selectSchema(schema, session: session) else { return false }
        for code in ["voni", "wqiy", "zfiao", "fiao"] where code != "fiao" || schema == "lingxi_wubi" {
            engine.clearComposition(session: session)
            for (index, scalar) in code.unicodeScalars.enumerated() {
                _ = engine.processKey(Int32(scalar.value), session: session)
                let expected = code == "voni" && index == 3 ? "灵犀" : ""
                guard (engine.takeCommit(session: session) ?? "") == expected else {
                    print("FAILED: four-key auto commit \(schema)/\(code) key \(index + 1)"); return false
                }
            }
        }
        engine.clearComposition(session: session)
        for manual in [false, true] {
            for fifth in [false, true] {
                engine.setOption("rimes_wubi_manual_commit", manual, session: session)
                engine.setOption("rimes_wubi_fifth_code", fifth, session: session)
                for scalar in "voni".unicodeScalars { _ = engine.processKey(Int32(scalar.value), session: session) }
                guard (engine.takeCommit(session: session) ?? "") == (manual ? "" : "灵犀") else { return false }
                if manual && fifth {
                    _ = engine.processKey(119, session: session)
                    guard engine.takeCommit(session: session) == "灵犀", engine.getContext(session: session).preedit == "w" else { return false }
                }
                engine.clearComposition(session: session)
                for scalar in "wqiy".unicodeScalars { _ = engine.processKey(Int32(scalar.value), session: session) }
                guard (engine.takeCommit(session: session) ?? "").isEmpty else { return false }
                if fifth {
                    let expected = engine.getContext(session: session).candidates.first?.text
                    _ = engine.processKey(119, session: session)
                    guard expected != nil, engine.takeCommit(session: session) == expected,
                          engine.getContext(session: session).preedit == "w" else { return false }
                }
                engine.clearComposition(session: session)
                for scalar in "znihao".unicodeScalars {
                    _ = engine.processKey(Int32(scalar.value), session: session)
                    guard (engine.takeCommit(session: session) ?? "").isEmpty else { return false }
                }
                engine.clearComposition(session: session)
            }
        }
        engine.setOption("rimes_wubi_manual_commit", false, session: session)
        engine.setOption("rimes_wubi_fifth_code", false, session: session)
    }
    print("Wubi smoke: OK z lookup, mixed input, code hints, exact selection, four-key unique auto commit")
    return true
}
