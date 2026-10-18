#include "capsule_hash.hpp"
#include "capsule_text.hpp"
#include "chat_codec.hpp"
#include "chat_jobs.hpp"
#include "native_chat_contract.hpp"
#include <thread>

namespace rimes::mailbox {
namespace {
struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};
void Need(bool ok, const char* message) {
    if (!ok)
        throw Failure(message);
}
std::string NativeJSON(const Json& value) {
    auto text = value.dump();
    for (std::size_t at = 0; (at = text.find('/', at)) != text.npos; at += 2)
        text.insert(at, "\\");
    return text;
}
std::string Prompt(const std::string& example, const std::string& marker, const Json& payload) {
    const auto offset = example.find(marker);
    Need(offset != example.npos, "对话请求模板不可用");
    auto result = example.substr(0, offset + marker.size()) + NativeJSON(payload);
    Need(result.size() <= ChatContract().at("maximumWireBytes").get<std::size_t>(),
         "此会话上下文过长，请新建会话");
    return result;
}
template <class F> auto Stable(F action) -> decltype(action()) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    for (;;) {
        try {
            return action();
        } catch (const runtime::StorageBusy&) {
            if (std::chrono::steady_clock::now() >= deadline)
                throw;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}
std::string ResponseFormat(const Json& thread) {
    for (auto it = thread.at("messages").rbegin(); it != thread.at("messages").rend(); ++it)
        if (it->at("role") == "inbound" && it->at("kind") == "content")
            return it->value("format", "plain");
    return thread.contains("generation") && thread.at("generation").is_object()
               ? thread.at("generation").value("expectedFormat", "plain")
               : "plain";
}
} // namespace
const Json& ChatContract() {
    static const auto value = Json::parse(native::kChatContract);
    return value;
}
std::string InitialChatPrompt(const std::string& source) {
    Need(!capsule::TrimText(source).empty() &&
             source.size() <= ChatContract().at("maximumSourceBytes").get<std::size_t>(),
         "请输入有效的对话内容");
    return Prompt(ChatContract().at("initial"), "USER_PAYLOAD_JSON:\n",
                  {{"mode", "ask"}, {"format", "plain"}, {"source", source}});
}
std::string ContinuationChatPrompt(const Json& turns, const std::string& format) {
    Need(turns.is_array() && !turns.empty() && turns.back().at("role") == "user",
         "此会话不能继续向 AI 提问");
    for (const auto& turn : turns)
        Need((turn.at("role") == "user" || turn.at("role") == "assistant") &&
                 !capsule::TrimText(turn.at("content").get<std::string>()).empty(),
             "对话上下文格式无效");
    return Prompt(ChatContract().at("continuations").at(format), "CONVERSATION_JSON:\n", turns);
}
Json ChatTurns(const Json& thread, const std::string& next) {
    auto turns = Json::array();
    for (const auto& message : thread.at("messages")) {
        if (message.at("kind") != "content" || message.at("role") == "system")
            continue;
        turns.push_back({{"role", message.at("role") == "user" ? "user" : "assistant"},
                         {"content", message.at("body")}});
    }
    if (!next.empty())
        turns.push_back({{"role", "user"}, {"content", next}});
    return turns;
}
ChatJobs::~ChatJobs() { Shutdown(); }
void ChatJobs::Cancel(const std::string& thread) {
    std::lock_guard lock(mutex_);
    for (const auto& [id, item] : active_)
        if (item.first.thread == thread || id == thread)
            item.second->store(true);
}
void ChatJobs::CancelAll() {
    std::lock_guard lock(mutex_);
    for (const auto& [id, item] : active_)
        item.second->store(true);
}
void ChatJobs::Shutdown() {
    CancelAll();
    workers_.Shutdown();
}
std::optional<ChatEvent> ChatJobs::State(const std::string& thread) const {
    std::lock_guard lock(mutex_);
    for (const auto& [id, item] : active_)
        if (item.first.thread == thread)
            return item.first;
    return {};
}
void ChatJobs::Publish(ChatEvent event, const ChatReceive& receive) {
    {
        std::lock_guard lock(mutex_);
        const auto found = active_.find(event.job);
        if (found == active_.end())
            return;
        if (event.finished)
            active_.erase(found);
        else
            found->second.first = event;
    }
    if (receive)
        receive(std::move(event));
}
bool ChatJobs::Start(ChatRequest request, Commit authorize, ChatReceive receive) {
    if (!authorize || !receive)
        return false;
    ChatEvent initial;
    initial.job = capsule::NewUuidV4();
    initial.thread = request.thread;
    const auto cancelled = std::make_shared<std::atomic<bool>>(false);
    {
        std::lock_guard lock(mutex_);
        if (active_.size() >= 4)
            return false;
        for (const auto& [id, item] : active_)
            if (!request.thread.empty() && item.first.thread == request.thread)
                return false;
        active_[initial.job] = {initial, cancelled};
    }
    const auto discarded = [this, initial, receive]() mutable {
        auto done = initial;
        done.finished = true;
        done.message = "生成已取消";
        Publish(done, receive);
    };
    const bool accepted = workers_.Run(
        cancelled,
        [this, request = std::move(request), initial, authorize, receive, cancelled]() mutable {
            auto event = initial;
            event.generation = capsule::NewUuidV4();
            try {
                auto source = request.source;
                auto body = capsule::TrimText(request.body);
                std::string prompt, format = "plain";
                Json existing;
                Store plain(request.root / "mailbox");
                if (!request.thread.empty()) {
                    existing = Stable([&] { return plain.Get(request.thread); });
                    Need(ConversationRevision(existing) == request.expected &&
                             Module(existing) == "chat",
                         "此会话已变化，请刷新后重试");
                    source = existing.at("source");
                    format = ResponseFormat(existing);
                    if (request.retry) {
                        Need(!existing.at("messages").empty(), "此请求无法重试");
                        body = existing.at("messages").back().at("body");
                    }
                    prompt = ContinuationChatPrompt(ChatTurns(existing, request.retry ? "" : body),
                                                    format);
                } else
                    prompt = InitialChatPrompt(body);
                Need(!body.empty() &&
                         body.size() <=
                             ChatContract().at("maximumSourceBytes").get<std::size_t>() &&
                         body.find('\0') == body.npos,
                     "请输入有效的对话内容");
                const std::string kind = source.at("kind");
                std::string model;
                if (source.contains("model") && source.at("model").is_string())
                    model = source.at("model");
                if (model.empty() && (kind == "codexCLI" || kind == "claudeCodeCLI")) {
                    // Native schema-v1 CLI rows used the adapter's verified default.
                    // Pin that model on the first reply, independent of global settings.
                    const auto plugin =
                        kind == "codexCLI" ? "builtin.codex-cli" : "builtin.claude-code-cli";
                    model = runtime::FieldDefaults(runtime::PluginFields(plugin)).at("model");
                    source["model"] = model;
                }
                runtime::RequestConfiguration api;
                if (kind == "openAICompatible") {
                    Need(source.contains("providerRoute") && source.at("providerRoute").is_object(),
                         "此会话缺少已保存的 Provider 路由，请新建会话");
                    try {
                        api = runtime::ConfigurationStore(request.root / "configuration")
                                  .FreezeProvider(source.at("providerRoute"));
                    } catch (...) {
                        throw Failure("已保存的 Provider 模型路由不可用，请新建会话后重新选择模型");
                    }
                    Need(api.provider.at("model") == model, "此会话的模型已变化，请新建会话");
                } else {
                    Need(kind == "codexCLI" || kind == "claudeCodeCLI", "此来源只能添加本地备注");
                    const auto plugin =
                        kind == "codexCLI" ? "builtin.codex-cli" : "builtin.claude-code-cli";
                    (void)runtime::ValidateFields(runtime::PluginFields(plugin),
                                                  {{"model", model}, {"effort", "medium"}});
                }
                auto authorized = [&] {
                    bool allowed = false;
                    authorize([&] { allowed = !cancelled->load(); });
                    if (!allowed)
                        cancelled->store(true);
                    return allowed;
                };
                Need(authorized(), "对话任务已取消或插件授权已变化");
                if (kind == "codexCLI") {
                    runtime::CodexRequest probe;
                    probe.root = request.root;
                    probe.executable = request.executable;
                    probe.timeout = std::min(request.timeout, std::chrono::milliseconds(5000));
                    runtime::CodexError error = runtime::CodexError::Missing;
                    runtime::RunCodex(probe, *cancelled, [&](auto value) {
                        if (value.finished)
                            error = value.error;
                    });
                    Need(error == runtime::CodexError::None, runtime::CodexErrorMessage(error));
                } else if (kind == "claudeCodeCLI") {
                    runtime::ClaudeRequest probe;
                    probe.root = request.root;
                    probe.executable = request.executable;
                    probe.timeout = std::min(request.timeout, std::chrono::milliseconds(5000));
                    runtime::ClaudeError error = runtime::ClaudeError::Missing;
                    runtime::RunClaude(probe, *cancelled, [&](auto value) {
                        if (value.finished)
                            error = value.error;
                    });
                    Need(error == runtime::ClaudeError::None, runtime::ClaudeErrorMessage(error));
                }
                Need(authorized(), "对话任务已取消或插件授权已变化");
                runtime::PrivateDirectory owner(request.root / "mailbox/generations" /
                                                event.generation);
                const auto held = owner.Lock(false);
                Store store(request.root / "mailbox", {},
                            [authorize, cancelled](const auto& write) {
                                authorize([&] {
                                    if (!cancelled->load())
                                        write();
                                });
                            });
                const auto row = Stable([&] {
                    return request.thread.empty()
                               ? store.BeginGeneration(source, "", body, event.generation, "chat")
                               : store.BeginReply(request.thread, request.expected, body,
                                                  event.generation, format, request.retry, model);
                });
                event.thread = row.at("id");
                event.admitted = true;
                Publish(event, receive);
                std::vector<std::string> blocks;
                bool finished = false, succeeded = false;
                std::string error;
                auto last = std::chrono::steady_clock::time_point{};
                const auto preview = [&](const std::string& text) {
                    if (text.empty() || text.size() > 256 * 1024 || !authorized())
                        return;
                    event.preview = text;
                    const auto now = std::chrono::steady_clock::now();
                    if (now - last >= std::chrono::milliseconds(50)) {
                        last = now;
                        Publish(event, receive);
                    }
                };
                if (kind == "codexCLI") {
                    runtime::CodexRequest cli;
                    cli.mode = runtime::CodexMode::Text;
                    cli.root = request.root;
                    cli.executable = request.executable;
                    cli.text = body;
                    cli.prepared_prompt = prompt;
                    cli.model = model;
                    cli.effort = "medium";
                    cli.timeout = request.timeout;
                    runtime::RunCodex(cli, *cancelled, [&](runtime::CodexEvent value) {
                        if (!value.finished)
                            preview(value.text);
                        else {
                            finished = true;
                            succeeded = value.error == runtime::CodexError::None;
                            blocks = std::move(value.blocks);
                            error = runtime::CodexErrorMessage(value.error);
                        }
                    });
                } else if (kind == "claudeCodeCLI") {
                    runtime::ClaudeRequest cli;
                    cli.mode = runtime::ClaudeMode::Text;
                    cli.root = request.root;
                    cli.executable = request.executable;
                    cli.text = body;
                    cli.prepared_prompt = prompt;
                    cli.model = model;
                    cli.effort = "medium";
                    cli.timeout = request.timeout;
                    runtime::RunClaude(cli, *cancelled, [&](runtime::ClaudeEvent value) {
                        if (!value.finished)
                            preview(value.text);
                        else {
                            finished = true;
                            succeeded = value.error == runtime::ClaudeError::None;
                            blocks = std::move(value.blocks);
                            error = runtime::ClaudeErrorMessage(value.error);
                        }
                    });
                } else
                    runtime::RunAIRequest(
                        body, api, *cancelled,
                        [&](runtime::AIUpdate value) {
                            if (!value.finished)
                                preview(value.text);
                            else {
                                finished = true;
                                succeeded = value.error == runtime::AIError::None;
                                blocks = std::move(value.blocks);
                                error = runtime::AIErrorMessage(value.error);
                            }
                        },
                        request.timeout, prompt);
                Need(finished && succeeded && !blocks.empty(),
                     error.empty() ? "生成未完成，请重试" : error.c_str());
                Need(authorized(), "生成已取消");
                const auto response = openai::Joined(blocks, "\n\n");
                Need(!capsule::TrimText(response).empty(), "生成结果为空，请重试");
                if (format == "json") {
                    const auto checked = Json::parse(response);
                    (void)checked;
                }
                Stable([&] {
                    return store.CompleteGeneration(event.thread, event.generation, response);
                });
                event.success = true;
            } catch (const Failure& failure) {
                event.message = failure.what();
            } catch (...) {
                event.message = "对话未完成，请检查连接器或存储后重试";
            }
            if (!event.success && event.admitted) {
                if (cancelled->load())
                    event.message = "生成已取消";
                // Only the task's own status is written after revocation; no output.
                try {
                    Stable([&] {
                        Store(request.root / "mailbox")
                            .FailGeneration(event.thread, event.generation, event.message);
                    });
                } catch (...) {
                }
            }
            event.finished = true;
            event.preview.clear();
            Publish(event, receive);
        },
        discarded);
    if (!accepted) {
        std::lock_guard lock(mutex_);
        active_.erase(initial.job);
    }
    return accepted;
}
} // namespace rimes::mailbox
