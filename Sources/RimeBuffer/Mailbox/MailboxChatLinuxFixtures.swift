import Foundation

enum MailboxChatLinuxFixtures {
    static func value() throws -> [String: Any] {
        let text = "公开问题：路径 /tmp/example，\"引号\"与🙂。"
        let turns = [AITextConversationPromptTurn(role: .user, content: text),
                     AITextConversationPromptTurn(role: .assistant, content: "这是公开的上下文。"),
                     AITextConversationPromptTurn(role: .user, content: "继续解释。")]
        let initial = try AITextRequestPlanner.initialPrompt(sourceText: text, mode: .ask, format: .plain)
        let continuation = try AITextRequestPlanner.continuationPrompt(turns: turns)
        var continuations: [String: String] = [:]
        for format in [AITextContentFormat.plain, .markdown, .json] {
            continuations[format.rawValue] = try AITextRequestPlanner.continuationPrompt(turns: turns, format: format)
        }
        return ["purpose": "public-mailbox-chat-contract", "schemaVersion": 1,
                "source": text, "initial": initial, "continuation": continuation,
                "turns": [["role": "user", "content": text],
                          ["role": "assistant", "content": "这是公开的上下文。"],
                          ["role": "user", "content": "继续解释。"]],
                "continuations": continuations,
                "maximumSourceBytes": AITextRuntimeLimits.maximumSourceBytes,
                "maximumWireBytes": AITextRuntimeLimits.maximumWireBytes,
                "previewMinimumInterval": 0.05]
    }
}
