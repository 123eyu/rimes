#pragma once
#include "ai_api.hpp"
#include "cli_session.hpp"
#include <optional>

namespace rimes::runtime {
enum class CodexMode { Probe, Login, Text, Image };
enum class CodexError {
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
const char* CodexErrorMessage(CodexError error);
struct CodexEvent {
    std::string text, authorization_url, image_path;
    bool finished = false, image_started = false;
    CodexError error = CodexError::None;
    std::vector<std::string> blocks;
};
using CodexReceive = std::function<void(CodexEvent)>;
struct CodexRequest {
    CodexMode mode = CodexMode::Probe;
    std::string text, model, effort, skill, prepared_prompt;
    std::filesystem::path root = UserRoot();
    // An injected executable is used by the test harness. Production callers
    // leave it empty and resolve an installed, owned executable.
    std::filesystem::path executable;
    std::chrono::milliseconds timeout{120000};
};
bool SafeCodexAuthorizationURL(const std::string& url);
std::filesystem::path LocateCodex();
bool HasCodexCredential(const std::filesystem::path& root);
Json CodexContract();
std::vector<std::string> CodexArguments(const std::filesystem::path& workspace, bool image);
std::map<std::string, std::string> CodexEnvironment(const std::filesystem::path& executable,
                                                    const std::filesystem::path& home,
                                                    const std::filesystem::path& temporary);

// The pure parser is also exercised against replays exported by the actual
// macOS parser. Cancellation and transport ownership live outside it.
class CodexProtocol {
  public:
    CodexProtocol(CodexRequest request, std::filesystem::path workspace, CodexReceive receive,
                  bool verify_account = true);
    std::string Start() const;
    CLIAction Feed(std::string_view bytes, std::string& outbound);
    std::string Interrupt() const;
    CodexError Error() const { return error_; }
    bool Done() const { return stage_ == Stage::Done; }
    const std::string& FinalText() const { return final_; }
    const std::string& ImagePath() const { return image_; }
    bool ImageStarted() const { return image_started_; }

  private:
    enum class Stage {
        Initialize,
        MCP,
        Account,
        Login,
        WaitingLogin,
        VerifyLogin,
        Thread,
        Turn,
        Active,
        Done,
        Failed
    };
    CLIAction Consume(const Json& value, std::string& outbound);
    void Begin(std::string& outbound);
    CLIAction Fail(CodexError error = CodexError::Protocol);
    CodexRequest request_;
    std::filesystem::path workspace_;
    CodexReceive receive_;
    Stage stage_ = Stage::Initialize;
    bool verify_account_, image_started_ = false, fatal_ = false;
    CodexError error_ = CodexError::None;
    std::string buffer_, thread_, turn_, login_, final_, fallback_, image_;
    std::map<std::string, std::string> messages_, phases_;
};
void RunCodex(CodexRequest request, std::atomic<bool>& cancelled, CodexReceive receive);
class CodexWorker {
  public:
    ~CodexWorker() { Shutdown(); }
    void Run(CodexRequest request, std::shared_ptr<std::atomic<bool>> cancelled,
             CodexReceive receive);
    void CancelAll() { jobs_.CancelAll(); }
    void Shutdown() { jobs_.Shutdown(); }

  private:
    BackgroundJobs jobs_;
};
} // namespace rimes::runtime
