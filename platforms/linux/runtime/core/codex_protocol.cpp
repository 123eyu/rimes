#include "codex_channel.hpp"
#include "native_codex_contract.hpp"
#include <algorithm>
#include <cctype>
#include <set>

namespace rimes::runtime {
namespace {
std::string RPC(int id, const std::string& method, const Json& params) {
    return Json{{"id", id}, {"method", method}, {"params", params}}.dump() + "\n";
}
bool Text(const Json& value, std::size_t limit = 512) {
    return value.is_string() && !value.get_ref<const std::string&>().empty() &&
           value.get_ref<const std::string&>().size() <= limit &&
           value.get_ref<const std::string&>().find('\0') == std::string::npos;
}
Json Parse(const std::string& raw) {
    std::vector<std::set<std::string>> keys;
    return Json::parse(raw, [&](int depth, Json::parse_event_t event, Json& value) {
        if (depth > 32)
            throw std::runtime_error("Invalid Codex message");
        if (event == Json::parse_event_t::object_start)
            keys.emplace_back();
        if (event == Json::parse_event_t::key &&
            !keys.back().insert(value.get<std::string>()).second)
            throw std::runtime_error("Duplicate Codex key");
        if (event == Json::parse_event_t::object_end)
            keys.pop_back();
        return true;
    });
}
} // namespace
Json CodexContract() { return Json::parse(native::kCodexContract); }
bool SafeCodexAuthorizationURL(const std::string& url) {
    if (url.size() > 8192 || std::any_of(url.begin(), url.end(), [](unsigned char c) {
            return c <= 32 || c == 127 || c == '\\';
        }))
        return false;
    if (url.compare(0, 8, "https://") != 0)
        return false;
    auto authority = url.substr(8, url.find_first_of("/?#", 8) - 8);
    std::transform(authority.begin(), authority.end(), authority.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return authority == "auth.openai.com" || authority == "chatgpt.com" ||
           authority == "auth.openai.com:443" || authority == "chatgpt.com:443";
}
CodexProtocol::CodexProtocol(CodexRequest request, std::filesystem::path workspace,
                             CodexReceive receive, bool verify_account)
    : request_(std::move(request)), workspace_(std::move(workspace)), receive_(std::move(receive)),
      verify_account_(verify_account) {}
std::string CodexProtocol::Start() const {
    return RPC(1, "initialize",
               {{"clientInfo", {{"name", "rimebuffer"}, {"title", "XSIME"}, {"version", "1"}}}});
}
CLIAction CodexProtocol::Fail(CodexError error) {
    error_ = error;
    stage_ = Stage::Failed;
    return CLIAction::Fail;
}
void CodexProtocol::Begin(std::string& outbound) {
    if (request_.mode == CodexMode::Probe) {
        stage_ = Stage::Done;
        return;
    }
    if (request_.mode == CodexMode::Login) {
        stage_ = Stage::Login;
        outbound += RPC(10, "account/login/start", {{"type", "chatgpt"}});
        return;
    }
    Json params{{"cwd", workspace_.string()},
                {"approvalPolicy", "never"},
                {"ephemeral", true},
                {"personality", "none"}};
    if (!request_.model.empty()) {
        params["model"] = request_.model;
        params["allowProviderModelFallback"] = false;
    }
    if (!request_.effort.empty())
        params["config"] = {{"model_reasoning_effort", request_.effort}};
    stage_ = Stage::Thread;
    outbound += RPC(3, "thread/start", params);
}
CLIAction CodexProtocol::Feed(std::string_view bytes, std::string& outbound) {
    if (stage_ == Stage::Done)
        return CLIAction::Complete;
    if (stage_ == Stage::Failed)
        return CLIAction::Fail;
    const std::size_t limit = request_.mode == CodexMode::Image ? 24 * 1048576 : 512 * 1024;
    for (const auto byte : bytes) {
        if (byte != '\n') {
            if (buffer_.size() >= limit)
                return Fail(CodexError::TooLarge);
            buffer_ += byte;
            continue;
        }
        if (buffer_.empty())
            continue;
        auto line = std::move(buffer_);
        buffer_.clear();
        try {
            const auto action = Consume(Parse(line), outbound);
            if (action != CLIAction::Continue)
                return action;
        } catch (...) {
            return Fail();
        }
    }
    return CLIAction::Continue;
}
CLIAction CodexProtocol::Consume(const Json& value, std::string& outbound) {
    if (!value.is_object())
        return Fail();
    if (value.contains("id")) {
        // No unsolicited request, approval, tool or elicitation is accepted.
        if (value.contains("method") || !value.at("id").is_number_integer() ||
            value.contains("error") || !value.contains("result") || !value.at("result").is_object())
            return Fail();
        const auto id = value.at("id").get<std::int64_t>();
        const auto& result = value.at("result");
        if (id == 1 && stage_ == Stage::Initialize) {
            stage_ = Stage::MCP;
            outbound += Json{{"method", "initialized"}, {"params", Json::object()}}.dump() + "\n";
            outbound +=
                RPC(2, "mcpServerStatus/list", {{"detail", "toolsAndAuthOnly"}, {"limit", 100}});
        } else if (id == 2 && stage_ == Stage::MCP) {
            if (!result.contains("data") || !result.at("data").is_array() ||
                !result.at("data").empty() ||
                (result.contains("nextCursor") && !result.at("nextCursor").is_null()))
                return Fail(CodexError::Incompatible);
            if (verify_account_ && request_.mode != CodexMode::Login) {
                stage_ = Stage::Account;
                outbound += RPC(6, "account/read", {{"refreshToken", false}});
            } else
                Begin(outbound);
        } else if ((id == 6 && stage_ == Stage::Account) ||
                   (id == 11 && stage_ == Stage::VerifyLogin)) {
            if (!result.contains("account") || !result.at("account").is_object() ||
                result.at("account").value("type", "") != "chatgpt")
                return Fail(CodexError::LoginRequired);
            if (id == 11)
                stage_ = Stage::Done;
            else
                Begin(outbound);
        } else if (id == 10 && stage_ == Stage::Login) {
            if (result.value("type", "") != "chatgpt" || !result.contains("loginId") ||
                !Text(result.at("loginId")) || !result.contains("authUrl") ||
                !Text(result.at("authUrl"), 8192) ||
                !SafeCodexAuthorizationURL(result.at("authUrl")))
                return Fail();
            login_ = result.at("loginId");
            stage_ = Stage::WaitingLogin;
            CodexEvent event;
            event.authorization_url = result.at("authUrl");
            receive_(std::move(event));
        } else if (id == 3 && stage_ == Stage::Thread) {
            const auto& thread = result.at("thread");
            if (!thread.is_object() || !Text(thread.at("id")) ||
                (!request_.model.empty() && result.value("model", "") != request_.model))
                return Fail();
            thread_ = thread.at("id");
            Json input = Json::array();
            if (request_.mode == CodexMode::Image)
                input.push_back({{"type", "skill"},
                                 {"name", "imagegen"},
                                 {"path", (workspace_ / "skills/imagegen/SKILL.md").string()}});
            auto prompt = CodexContract().at("prompt").get<std::string>();
            const auto marker = prompt.find("RIMES_SOURCE_TEXT");
            if (marker == prompt.npos)
                return Fail();
            prompt.replace(marker, 17, request_.text);
            if (request_.mode == CodexMode::Image)
                prompt =
                    "请使用已挂载的 imagegen "
                    "Skill，调用图片生成工具，按下面的用户描述创作一张图片。\n图片由应用单独保存和"
                    "展示；请勿把本地路径、Markdown 图片或 base64 放入回复。\n\n用户描述：" +
                    request_.text;
            if (request_.mode != CodexMode::Image && !request_.prepared_prompt.empty()) {
                if (request_.prepared_prompt.size() > CodexContract().at("maximumWireBytes").get<std::size_t>() ||
                    request_.prepared_prompt.find('\0') != request_.prepared_prompt.npos) return Fail(CodexError::Protocol);
                prompt = request_.prepared_prompt;
            }
            input.push_back({{"type", "text"}, {"text", prompt}});
            Json params{{"threadId", thread_},
                        {"input", input},
                        {"outputSchema", CodexContract().at("schema")},
                        {"summary", "concise"}};
            if (!request_.model.empty())
                params["model"] = request_.model;
            if (!request_.effort.empty())
                params["effort"] = request_.effort;
            stage_ = Stage::Turn;
            outbound += RPC(4, "turn/start", params);
        } else if (id == 4 && stage_ == Stage::Turn) {
            const auto& turn = result.at("turn");
            if (!turn.is_object() || !Text(turn.at("id")) ||
                (!turn_.empty() && turn.at("id") != turn_))
                return Fail();
            turn_ = turn.at("id");
            stage_ = Stage::Active;
        } else
            return Fail();
        return stage_ == Stage::Done ? CLIAction::Complete : CLIAction::Continue;
    }
    if (!value.contains("method") || !Text(value.at("method"), 128))
        return Fail();
    const auto method = value.at("method").get<std::string>();
    const auto params = value.value("params", Json::object());
    if (!params.is_object())
        return Fail();
    if (method == "account/login/completed" && stage_ == Stage::WaitingLogin) {
        if (params.value("loginId", "") != login_)
            return CLIAction::Continue;
        if (!params.value("success", false))
            return Fail(CodexError::LoginRequired);
        stage_ = Stage::VerifyLogin;
        outbound += RPC(11, "account/read", {{"refreshToken", false}});
        return CLIAction::Continue;
    }
    if (stage_ != Stage::Turn && stage_ != Stage::Active)
        return CLIAction::Continue;
    // Older Codex notifications omit scope; when supplied it must match this
    // ephemeral session. A late event from another turn is never deliverable.
    if (params.contains("threadId") && params.at("threadId") != thread_)
        return CLIAction::Continue;
    if (params.contains("turnId") && !turn_.empty() && params.at("turnId") != turn_)
        return CLIAction::Continue;
    if (method == "turn/started") {
        const auto& turn = params.at("turn");
        if (!Text(turn.at("id")) || (!turn_.empty() && turn.at("id") != turn_))
            return Fail();
        turn_ = turn.at("id");
    } else if (method == "model/rerouted" && !request_.model.empty())
        return Fail();
    else if (method == "item/started" || method == "item/completed") {
        const auto& item = params.at("item");
        const auto type = item.value("type", "");
        if (type == "agentMessage") {
            if (!item.contains("id") || !Text(item.at("id")))
                return Fail();
            const auto id = item.at("id").get<std::string>();
            if (messages_.size() >= 128 && !messages_.count(id))
                return Fail(CodexError::TooLarge);
            if (item.contains("phase") && !item.at("phase").is_null())
                phases_[id] = item.at("phase");
            if (method == "item/completed" && item.contains("text") &&
                Text(item.at("text"), 256 * 1024)) {
                messages_[id] = item.at("text");
                if (phases_[id] == "final_answer")
                    final_ = messages_[id];
                else if (phases_[id] != "commentary")
                    fallback_ = messages_[id];
                if (phases_[id] != "commentary" && request_.mode == CodexMode::Text) {
                    CodexEvent event;
                    event.text = messages_[id];
                    receive_(std::move(event));
                }
            }
        } else if (type == "imageGeneration" && request_.mode == CodexMode::Image) {
            if (method == "item/started") {
                image_started_ = true;
                CodexEvent event;
                event.image_started = true;
                receive_(std::move(event));
            }
            if (method == "item/completed" && item.value("status", "") == "completed" &&
                item.contains("savedPath") && Text(item.at("savedPath"), 4096))
                image_ = item.at("savedPath");
        }
    } else if (method == "item/agentMessage/delta") {
        if (!params.contains("itemId") || !Text(params.at("itemId")) || !params.contains("delta") ||
            !params.at("delta").is_string())
            return Fail();
        const auto id = params.at("itemId").get<std::string>(),
                   delta = params.at("delta").get<std::string>();
        if ((messages_.size() >= 128 && !messages_.count(id)) ||
            delta.size() > 256 * 1024 - messages_[id].size())
            return Fail(CodexError::TooLarge);
        messages_[id] += delta;
        if (phases_[id] != "commentary" && request_.mode == CodexMode::Text) {
            CodexEvent event;
            event.text = messages_[id];
            receive_(std::move(event));
        }
    } else if (method == "error") {
        if (!params.value("willRetry", false))
            fatal_ = true;
    } else if (method == "turn/completed") {
        const auto& turn = params.at("turn");
        if ((turn.contains("id") && !turn_.empty() && turn.at("id") != turn_) ||
            turn.value("status", "") != "completed" || fatal_)
            return Fail();
        if (request_.mode == CodexMode::Image) {
            if (!image_started_ || image_.empty())
                return Fail();
        } else {
            if (final_.empty())
                final_ = fallback_;
            if (final_.empty())
                return Fail();
        }
        stage_ = Stage::Done;
        return CLIAction::Complete;
    }
    return CLIAction::Continue;
}
std::string CodexProtocol::Interrupt() const {
    if (!login_.empty() && stage_ != Stage::Done)
        return RPC(12, "account/login/cancel", {{"loginId", login_}});
    if (!thread_.empty() && !turn_.empty())
        return RPC(5, "turn/interrupt", {{"threadId", thread_}, {"turnId", turn_}});
    return {};
}
} // namespace rimes::runtime
