#include "claude_channel.hpp"
#include "native_claude_contract.hpp"
#include <set>
namespace rimes::runtime {
const Json& ClaudeContract() {
    static const auto value = Json::parse(native::kClaudeContract);
    return value;
}
ClaudeProtocol::ClaudeProtocol(ClaudeReceive receive, std::string model)
    : receive_(std::move(receive)), model_(std::move(model)) {}
CLIAction ClaudeProtocol::Fail(ClaudeError error) {
    error_ = error;
    return CLIAction::Fail;
}
CLIAction ClaudeProtocol::Feed(std::string_view bytes) {
    if (error_ != ClaudeError::None)
        return CLIAction::Fail;
    const auto maximum = ClaudeContract().at("maximumWireBytes").get<std::size_t>();
    if (bytes.size() > maximum - total_)
        return Fail(ClaudeError::TooLarge);
    total_ += bytes.size();
    buffer_.append(bytes);
    for (;;) {
        const auto end = buffer_.find('\n');
        if (end == buffer_.npos)
            break;
        const auto line = buffer_.substr(0, end);
        buffer_.erase(0, end + 1);
        if (line.size() > ClaudeContract().at("maximumLineBytes").get<std::size_t>())
            return Fail(ClaudeError::TooLarge);
        if (Record(line) == CLIAction::Fail)
            return CLIAction::Fail;
    }
    if (buffer_.size() > ClaudeContract().at("maximumLineBytes").get<std::size_t>())
        return Fail(ClaudeError::TooLarge);
    return CLIAction::Continue;
}
CLIAction ClaudeProtocol::Finish() {
    if (error_ != ClaudeError::None)
        return CLIAction::Fail;
    if (!buffer_.empty()) {
        const auto line = std::move(buffer_);
        buffer_.clear();
        if (Record(line) == CLIAction::Fail)
            return CLIAction::Fail;
    }
    return initialized_ && completed_ && !final_.empty() ? CLIAction::Complete : Fail();
}
CLIAction ClaudeProtocol::Record(const std::string& line) {
    if (line.find_first_not_of(" \r\t") == std::string::npos)
        return CLIAction::Continue;
    try {
        std::vector<std::set<std::string>> keys;
        auto callback = [&](int depth, Json::parse_event_t event, Json& value) {
            if (depth > 32)
                throw std::runtime_error("depth");
            if (event == Json::parse_event_t::object_start)
                keys.emplace_back();
            else if (event == Json::parse_event_t::key &&
                     !keys.back().insert(value.get<std::string>()).second)
                throw std::runtime_error("duplicate key");
            else if (event == Json::parse_event_t::object_end)
                keys.pop_back();
            return true;
        };
        const auto value = Json::parse(line, callback);
        if (!value.is_object() || !value.contains("type") || !value.at("type").is_string())
            return Fail();
        const auto type = value.at("type").get<std::string>();
        if (value.contains("session_id")) {
            const auto id = value.at("session_id").get<std::string>();
            if (id.empty() || id.size() > 256 || (!session_.empty() && session_ != id))
                return Fail();
            session_ = id;
        }
        auto activity = [&](const char* text) {
            ClaudeEvent event;
            event.activity = text;
            receive_(std::move(event));
        };
        if (type == "system") {
            const auto subtype = value.value("subtype", "");
            if (subtype == "init") {
                if (initialized_ || completed_)
                    return Fail();
                for (const auto* key : {"tools", "mcp_servers"})
                    if (value.contains(key) &&
                        (!value.at(key).is_array() || !value.at(key).empty()))
                        return Fail();
                if (value.contains("permissionMode") && value.at("permissionMode") != "dontAsk")
                    return Fail();
                if (model_.rfind("claude-", 0) == 0 && value.contains("model") &&
                    value.at("model") != model_)
                    return Fail();
                initialized_ = true;
                activity("已连接 Claude Code");
            } else if (subtype == "api_retry")
                activity("Claude Code 正在重试");
        } else if (type == "rate_limit_event") {
            activity("Claude Code 正在等待用量恢复");
        } else if (type == "stream_event") {
            if (!initialized_ || completed_)
                return Fail();
            const auto& event = value.at("event");
            const auto event_type = event.at("type").get<std::string>();
            if (event_type == "content_block_start") {
                const auto kind = event.at("content_block").at("type").get<std::string>();
                if (kind != "text" && kind != "thinking" && kind != "redacted_thinking")
                    return Fail();
                activity(kind == "text" ? "Claude Code 正在组织回复" : "Claude Code 正在思考");
            } else if (event_type == "content_block_delta") {
                const auto& delta = event.at("delta");
                const auto kind = delta.at("type").get<std::string>();
                if (kind == "text_delta") {
                    const auto text = delta.at("text").get<std::string>();
                    if (text.size() > 256 * 1024 - latest_.size())
                        return Fail(ClaudeError::TooLarge);
                    latest_ += text;
                    ClaudeEvent update;
                    update.text = latest_;
                    receive_(std::move(update));
                } else if (kind == "thinking_delta" || kind == "signature_delta")
                    activity("Claude Code 正在思考");
                else
                    return Fail();
            } else if (event_type == "message_start")
                activity("Claude Code 正在思考");
            else if (event_type == "message_stop")
                activity("正在校验 Claude Code 的回复");
        } else if (type == "assistant") {
            if (!initialized_ || completed_)
                return Fail();
            for (const auto& block : value.at("message").at("content")) {
                const auto kind = block.at("type").get<std::string>();
                if (kind != "text" && kind != "thinking" && kind != "redacted_thinking")
                    return Fail();
            }
        } else if (type == "result") {
            if (!initialized_ || completed_ || value.value("is_error", false) ||
                value.value("subtype", "") != "success")
                return Fail();
            if (value.contains("structured_output"))
                final_ = value.at("structured_output").dump();
            else
                final_ = value.at("result").get<std::string>();
            if (final_.empty() || final_.size() > 256 * 1024 || final_.find('\0') != final_.npos)
                return Fail();
            completed_ = true;
        } else if (type == "error" || type == "control_request" || type == "user")
            return Fail();
        return CLIAction::Continue;
    } catch (...) {
        return Fail();
    }
}
} // namespace rimes::runtime
