#pragma once
#include "ai_api.hpp"
#include "cli_session.hpp"
namespace rimes::runtime {
enum class ClaudeMode { Probe, Login, Text };
enum class ClaudeError {
    None,
    Missing,
    Incompatible,
    LoginRequired,
    Cancelled,
    Timeout,
    Protocol,
    TooLarge,
    Busy
};
struct ClaudeEvent {
    bool finished = false;
    ClaudeError error = ClaudeError::None;
    std::string text, activity;
    std::vector<std::string> blocks;
};
using ClaudeReceive = std::function<void(ClaudeEvent)>;
struct ClaudeRequest {
    ClaudeMode mode = ClaudeMode::Probe;
    std::string text, prepared_prompt, model = "claude-opus-5-5", effort = "medium";
    std::filesystem::path root = UserRoot(), executable;
    std::chrono::milliseconds timeout{120000};
};
const Json& ClaudeContract();
const char* ClaudeErrorMessage(ClaudeError error);
bool ClaudeHelpOption(const std::string& help, const std::string& option);
class ClaudeProtocol {
  public:
    explicit ClaudeProtocol(ClaudeReceive receive, std::string model = "");
    CLIAction Feed(std::string_view bytes);
    CLIAction Finish();
    const std::string& FinalText() const { return final_; }
    ClaudeError Error() const { return error_; }

  private:
    CLIAction Record(const std::string& line);
    CLIAction Fail(ClaudeError error = ClaudeError::Protocol);
    ClaudeReceive receive_;
    std::string model_, session_, buffer_, latest_, final_;
    bool initialized_ = false, completed_ = false;
    std::size_t total_ = 0;
    ClaudeError error_ = ClaudeError::None;
};
void RunClaude(ClaudeRequest request, const std::atomic<bool>& cancelled, ClaudeReceive receive);
class ClaudeWorker {
  public:
    ~ClaudeWorker() { Shutdown(); }
    void Run(ClaudeRequest request, std::shared_ptr<std::atomic<bool>> cancelled,
             ClaudeReceive receive);
    void CancelAll() { jobs_.CancelAll(); }
    void Shutdown() { jobs_.Shutdown(); }

  private:
    BackgroundJobs jobs_;
};
} // namespace rimes::runtime
