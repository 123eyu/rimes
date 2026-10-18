#include "chat_codec.hpp"
#include "claude_channel.hpp"
#include <cstdlib>
#include <set>
#include <sstream>
namespace rimes::runtime {
namespace {
struct Workspace {
    std::filesystem::path path;
    explicit Workspace(const std::filesystem::path& root)
        : path(root / "ai/requests" / RandomID()) {
        (void)PrivateDirectory(path);
    }
    ~Workspace() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
ClaudeError ExitError(CLIExit code) {
    switch (code) {
    case CLIExit::Completed:
        return ClaudeError::None;
    case CLIExit::Cancelled:
        return ClaudeError::Cancelled;
    case CLIExit::Timeout:
        return ClaudeError::Timeout;
    case CLIExit::Unavailable:
        return ClaudeError::Missing;
    case CLIExit::TooLarge:
        return ClaudeError::TooLarge;
    case CLIExit::Protocol:
        return ClaudeError::Protocol;
    }
    return ClaudeError::Protocol;
}
std::string Preview(const std::string& raw) {
    openai::Decoder decoder;
    decoder.Feed(
        "data: " +
        Json{{"choices", Json::array({{{"index", 0}, {"delta", {{"content", raw}}}}})}}.dump() +
        "\n\n");
    return decoder.Preview();
}
CLISessionOptions Options(const VerifiedCLIExecutable& executable, const Workspace& workspace,
                          const std::vector<std::string>& args, std::chrono::milliseconds timeout,
                          std::size_t maximum) {
    CLISessionOptions options;
    options.argv = {executable.path.string()};
    options.argv.insert(options.argv.end(), args.begin(), args.end());
    options.directory = workspace.path;
    options.environment = CLIEnvironment(executable.path, workspace.path);
    options.timeout = timeout;
    options.maximum_output = maximum;
    options.close_stdin = options.finish_at_eof = true;
    return options;
}
CLIExit Capture(const CLISessionOptions& options, const VerifiedCLIExecutable& executable,
                const std::atomic<bool>& cancelled, std::string& text) {
    if (!executable.Matches())
        return CLIExit::Unavailable;
    return RunCLISession(options, cancelled, "", [&](auto bytes, auto&) {
        text.append(bytes);
        return bytes.empty() ? CLIAction::Complete : CLIAction::Continue;
    });
}
} // namespace
const char* ClaudeErrorMessage(ClaudeError error) {
    switch (error) {
    case ClaudeError::None:
        return "";
    case ClaudeError::Missing:
        return "未找到具备所需流式生成能力的 Claude Code CLI";
    case ClaudeError::Incompatible:
        return "Claude Code CLI 未提供所需安全参数与流式能力";
    case ClaudeError::LoginRequired:
        return "Claude 尚未登录，请点击“登录 Claude”完成 CLI 授权";
    case ClaudeError::Cancelled:
        return "已取消 Claude 请求。";
    case ClaudeError::Timeout:
        return "Claude 请求超时，请重试。";
    case ClaudeError::Protocol:
        return "Claude 回复格式无效，原文已保留";
    case ClaudeError::TooLarge:
        return "Claude 回复过长，请缩短请求后重试";
    case ClaudeError::Busy:
        return "前一个请求正在停止，请稍后重试";
    }
    return "Claude 请求失败";
}
bool ClaudeHelpOption(const std::string& help, const std::string& option) {
    std::istringstream lines(help);
    std::string line;
    while (std::getline(lines, line)) {
        const auto start = line.find_first_not_of(" \t\r");
        if (start == line.npos || line[start] != '-')
            continue;
        line.erase(0, start);
        const auto tab = line.find('\t'), spacing = line.find("  ");
        line.resize(std::min(line.size(), std::min(tab, spacing)));
        for (auto& c : line)
            if (c == ',')
                c = ' ';
        std::istringstream fields(line);
        std::string token;
        while (fields >> token)
            if (token.substr(0, token.find('=')) == option)
                return true;
    }
    return false;
}
static std::vector<std::filesystem::path> ClaudeCandidates() {
    // A configured executable is a pin; never silently select another account's CLI.
    if (const auto* pin = std::getenv("RIMES_CLAUDE_PATH"); pin && *pin)
        return {pin};
    std::vector<std::filesystem::path> candidates;
    if (const auto* home = std::getenv("HOME"); home && std::filesystem::path(home).is_absolute())
        for (const auto* directory : {".local/bin", ".claude/local", ".npm-global/bin",
                                      ".volta/bin", ".bun/bin", ".local/share/pnpm"})
            candidates.push_back(std::filesystem::path(home) / directory / "claude");
    for (const auto* directory : {"/usr/local/bin", "/usr/bin", "/bin"})
        candidates.push_back(std::filesystem::path(directory) / "claude");
    std::istringstream paths(std::getenv("PATH") ? std::getenv("PATH") : "");
    std::string path;
    while (std::getline(paths, path, ':'))
        if (std::filesystem::path(path).is_absolute())
            candidates.push_back(std::filesystem::path(path) / "claude");
    std::set<std::filesystem::path> seen;
    std::vector<std::filesystem::path> verified;
    for (const auto& candidate : candidates) {
        try {
            const auto path = VerifiedCLIExecutable(candidate).path;
            if (seen.insert(path).second)
                verified.push_back(path);
            if (verified.size() == 16)
                break;
        } catch (...) {
        }
    }
    return verified;
}
void RunClaude(ClaudeRequest request, const std::atomic<bool>& cancelled, ClaudeReceive receive) {
    ClaudeEvent completion;
    completion.finished = true;
    try {
        const auto candidates = request.executable.empty()
                                    ? ClaudeCandidates()
                                    : std::vector<std::filesystem::path>{request.executable};
        if (candidates.empty())
            throw ExecutableUnavailable("Claude unavailable");
        Workspace workspace(request.root);
        const auto& contract = ClaudeContract();
        std::optional<VerifiedCLIExecutable> selected;
        completion.error = ClaudeError::Missing;
        const auto discovery_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        for (const auto& path : candidates) {
            if (std::chrono::steady_clock::now() >= discovery_deadline)
                break;
            try {
                const VerifiedCLIExecutable candidate(path);
                auto options =
                    Options(candidate, workspace, {"--help"}, std::chrono::seconds(2), 128 * 1024);
                std::string help;
                completion.error = ExitError(Capture(options, candidate, cancelled, help));
                if (completion.error == ClaudeError::Cancelled)
                    break;
                if (completion.error == ClaudeError::None) {
                    for (const auto& required : contract.at("requiredOptions"))
                        if (!ClaudeHelpOption(help, required)) {
                            completion.error = ClaudeError::Incompatible;
                            break;
                        }
                    if (completion.error == ClaudeError::None && candidate.Matches()) {
                        selected = candidate;
                        break;
                    }
                }
            } catch (const ExecutableUnavailable&) {
                completion.error = ClaudeError::Missing;
            }
        }
        if (!selected) {
            if (completion.error == ClaudeError::None)
                completion.error = ClaudeError::Missing;
            receive(std::move(completion));
            return;
        }
        const auto& executable = *selected;
        auto authenticated = [&] {
            auto status = Options(executable, workspace, contract.at("statusArguments"),
                                  std::chrono::seconds(2), 4096);
            std::string body;
            const auto exit = Capture(status, executable, cancelled, body);
            if (exit == CLIExit::Cancelled || exit == CLIExit::Timeout ||
                exit == CLIExit::TooLarge || exit == CLIExit::Unavailable)
                return ExitError(exit);
            const auto value = Json::parse(body);
            // Account metadata remains within this call and never becomes a UI event.
            return exit == CLIExit::Completed && value.value("loggedIn", false)
                       ? ClaudeError::None
                       : ClaudeError::LoginRequired;
        };
        if (completion.error == ClaudeError::None && request.mode == ClaudeMode::Login) {
            auto login = Options(executable, workspace, contract.at("loginArguments"),
                                 request.timeout, 256 * 1024);
            // Only an explicit login may ask the official CLI to open the desktop browser.
            for (const auto* name : {"DISPLAY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR",
                                     "DBUS_SESSION_BUS_ADDRESS", "XAUTHORITY"})
                if (const auto* value = std::getenv(name))
                    login.environment[name] = value;
            ClaudeEvent waiting;
            waiting.activity = "等待浏览器完成 Claude 授权…";
            receive(std::move(waiting));
            if (!executable.Matches())
                completion.error = ClaudeError::Missing;
            else
                completion.error =
                    ExitError(RunCLISession(login, cancelled, "", [](auto bytes, auto&) {
                        // Drain but never retain or display browser links, codes or account output.
                        return bytes.empty() ? CLIAction::Complete : CLIAction::Continue;
                    }));
        }
        if (completion.error == ClaudeError::None)
            completion.error = authenticated();
        if (completion.error == ClaudeError::None && request.mode == ClaudeMode::Text) {
            if (request.text.empty() || request.text.find('\0') != request.text.npos ||
                request.text.size() > contract.at("maximumSourceBytes").get<std::size_t>())
                throw std::runtime_error("invalid request");
            (void)ValidateFields(PluginFields("builtin.claude-code-cli"),
                                 {{"model", request.model}, {"effort", request.effort}});
            auto arguments = contract.at("arguments").get<std::vector<std::string>>();
            arguments.insert(arguments.end(),
                             {"--model", request.model, "--effort", request.effort});
            auto inference = Options(executable, workspace, arguments, request.timeout,
                                     contract.at("maximumWireBytes"));
            auto prompt = contract.at("prompt").get<std::string>();
            const auto marker = prompt.find("RIMES_SOURCE_TEXT");
            if (marker == prompt.npos)
                throw std::runtime_error("missing source marker");
            prompt.replace(marker, std::string("RIMES_SOURCE_TEXT").size(), request.text);
            if (prompt.size() > inference.maximum_pending_input)
                throw std::runtime_error("request too large");
            std::string previous;
            ClaudeProtocol protocol(
                [&](ClaudeEvent event) {
                    if (cancelled.load())
                        return;
                    if (!event.text.empty()) {
                        event.text = Preview(event.text);
                        if (event.text.empty() || event.text == previous)
                            return;
                        previous = event.text;
                    }
                    receive(std::move(event));
                },
                request.model);
            if (!executable.Matches())
                completion.error = ClaudeError::Missing;
            else {
                const auto result =
                    RunCLISession(inference, cancelled, prompt, [&](auto bytes, auto&) {
                        return bytes.empty() ? protocol.Finish() : protocol.Feed(bytes);
                    });
                completion.error = ExitError(result);
                if (protocol.Error() != ClaudeError::None)
                    completion.error = protocol.Error();
                if (completion.error == ClaudeError::None) {
                    completion.blocks = openai::LogicalBlocks(protocol.FinalText());
                    completion.text = openai::Joined(completion.blocks);
                }
            }
        }
    } catch (const ExecutableUnavailable&) {
        completion.error = ClaudeError::Missing;
    } catch (const StorageBusy&) {
        completion.error = ClaudeError::Busy;
    } catch (const openai::Failure& failure) {
        completion.error =
            failure.code == openai::Error::TooLarge ? ClaudeError::TooLarge : ClaudeError::Protocol;
    } catch (...) {
        completion.error = ClaudeError::Protocol;
    }
    if (cancelled.load())
        completion.error = ClaudeError::Cancelled;
    if (completion.error != ClaudeError::None) {
        completion.text.clear();
        completion.blocks.clear();
    }
    receive(std::move(completion));
}
void ClaudeWorker::Run(ClaudeRequest request, std::shared_ptr<std::atomic<bool>> cancelled,
                       ClaudeReceive receive) {
    const auto discarded = [receive] {
        ClaudeEvent event;
        event.finished = true;
        event.error = ClaudeError::Cancelled;
        receive(std::move(event));
    };
    if (!jobs_.Run(
            cancelled,
            [request = std::move(request), cancelled, receive] {
                RunClaude(request, *cancelled, receive);
            },
            discarded)) {
        ClaudeEvent event;
        event.finished = true;
        event.error = ClaudeError::Busy;
        receive(std::move(event));
    }
}
} // namespace rimes::runtime
