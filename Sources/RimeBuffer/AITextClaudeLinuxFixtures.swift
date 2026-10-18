import Foundation

enum AITextClaudeLinuxContract {
    static func value() throws -> [String: Any] {
        let text = "{\"blocks\":[{\"text\":\"公开🙂。\",\"title\":null}]}"
        func event(_ type: String, _ fields: [String: Any]) -> [String: Any] {
            ["type": "stream_event", "event": fields.merging(["type": type]) { _, right in right }]
        }
        let start: [String: Any] = ["type": "system", "subtype": "init", "tools": []]
        let result: [String: Any] = ["type": "result", "subtype": "success", "is_error": false, "result": text]
        let cases: [(String, [[String: Any]])] = [
            ("text", [start, event("content_block_start", ["content_block": ["type": "text"]]),
                      event("content_block_delta", ["delta": ["type": "text_delta", "text": text]]), result]),
            ("reasoning", [start, event("content_block_start", ["content_block": ["type": "thinking"]]),
                           event("content_block_delta", ["delta": ["type": "thinking_delta", "thinking": "private fixture reasoning"]]), result]),
            ("tools", [["type": "system", "subtype": "init", "tools": ["Bash"]], result]),
            ("tool-block", [start, event("content_block_start", ["content_block": ["type": "tool_use"]]), result]),
            ("unknown-delta", [start, event("content_block_delta", ["delta": ["type": "input_json_delta", "partial_json": "{}"]]), result]),
            ("structured", [start, ["type": "result", "subtype": "success", "is_error": false,
                                   "structured_output": ["blocks": [["text": "公开🙂。", "title": NSNull()]]]]]),
        ]
        let fixtures: [[String: Any]] = try cases.map { name, records in
            let data = try records.reduce(into: Data()) { output, record in
                output += try JSONSerialization.data(withJSONObject: record, options: [.sortedKeys])
                output.append(0x0A)
            }
            var parser = AITextClaudeJSONStreamParser()
            var snapshots: [String] = []
            var accepted = true
            do {
                for byte in data { snapshots += try parser.append(Data([byte])).snapshots }
                snapshots += try parser.finish().snapshots
            } catch { accepted = false }
            return ["name": name, "wire": String(decoding: data, as: UTF8.self),
                    "accepted": accepted, "snapshots": snapshots,
                    "finalText": parser.finalText ?? ""]
        }
        return ["schemaVersion": 1, "purpose": "public-claude-channel-contract",
                "arguments": AITextClaudeInferenceArguments.request(model: nil, effort: nil),
                "statusArguments": AITextClaudeAuthentication.statusArguments,
                "loginArguments": AITextClaudeLoginOperation.loginArguments,
                "requiredOptions": AITextClaudeCompatibility.requiredHelpOptions,
                "prompt": AITextClaudeInferenceArguments.linuxFixturePrompt,
                "maximumSourceBytes": AITextRuntimeLimits.maximumSourceBytes,
                "maximumWireBytes": AITextRuntimeLimits.maximumWireBytes,
                "maximumLineBytes": AITextRuntimeLimits.maximumLineBytes,
                "defaultTimeout": AITextRuntimeLimits.defaultTimeout,
                "fixtures": fixtures]
    }
}
