#pragma once
#include "claude_channel.hpp"
#include "codex_channel.hpp"
#include "mailbox_store.hpp"
#include <mutex>

namespace rimes::mailbox {
const Json& ChatContract();
std::string InitialChatPrompt(const std::string& source);
std::string ContinuationChatPrompt(const Json& turns, const std::string& format = "plain");
Json ChatTurns(const Json& thread, const std::string& next = "");
struct ChatRequest {
    Json source;
    std::string thread, expected, body;
    bool retry = false;
    std::filesystem::path root = runtime::UserRoot(), executable;
    std::chrono::milliseconds timeout{120000};
};
struct ChatEvent {
    std::string job, thread, generation, preview, message;
    bool admitted = false, finished = false, success = false;
};
using ChatReceive = std::function<void(ChatEvent)>;
// Mailbox-owned; hiding or navigating the window does not own a request.
// Partial output exists only here and never reaches the durable store.
class ChatJobs {
  public:
    ~ChatJobs();
    bool Start(ChatRequest request, Commit authorize, ChatReceive receive);
    void Cancel(const std::string& thread);
    void CancelAll();
    void Shutdown();
    std::optional<ChatEvent> State(const std::string& thread) const;

  private:
    void Publish(ChatEvent event, const ChatReceive& receive);
    mutable std::mutex mutex_;
    std::map<std::string, std::pair<ChatEvent, std::shared_ptr<std::atomic<bool>>>> active_;
    runtime::BackgroundJobs workers_;
};
} // namespace rimes::mailbox
