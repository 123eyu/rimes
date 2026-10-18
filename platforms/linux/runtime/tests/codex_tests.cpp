#include "codex_channel.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
using namespace rimes::runtime;
using namespace std::chrono_literals;
namespace {
void Check(bool ok, const std::string& message) {
    if (!ok)
        throw std::runtime_error(message);
}
std::vector<Json> Lines(const std::string& bytes) {
    std::vector<Json> result;
    std::istringstream input(bytes);
    std::string line;
    while (std::getline(input, line))
        if (!line.empty())
            result.push_back(Json::parse(line));
    return result;
}
void Replays() {
    const auto contract = CodexContract();
    for (const auto& replay : contract.at("protocol").at("replays")) {
        CodexRequest request;
        request.mode = CodexMode::Text;
        request.model = "gpt-6-sol";
        request.effort = "high";
        request.text = "公开请求";
        std::vector<std::string> actual;
        CodexProtocol protocol(
            request, "/RIMES_WORKSPACE",
            [&](CodexEvent event) {
                if (!event.text.empty())
                    actual.push_back("text:" + event.text);
            },
            false);
        for (std::size_t i = 0; i < replay.at("events").size(); ++i) {
            actual.clear();
            std::string outbound;
            const auto bytes = replay.at("events").at(i).dump() + "\n";
            CLIAction action = CLIAction::Continue;
            // Every single byte boundary is exercised, including UTF-8.
            for (const auto ch : bytes)
                action = protocol.Feed(std::string_view(&ch, 1), outbound);
            for (const auto& command : Lines(outbound)) {
                const auto method = command.at("method").get<std::string>();
                if (method == "mcpServerStatus/list")
                    actual.push_back("mcp");
                if (method == "thread/start") {
                    Check(command.at("params") == CodexContract().at("protocol").at("thread"),
                          "native thread request drift");
                    actual.push_back("thread");
                }
                if (method == "turn/start")
                    actual.push_back("turn:" +
                                     command.at("params").at("threadId").get<std::string>());
            }
            if (action == CLIAction::Complete)
                actual.push_back("done:" + protocol.FinalText());
            if (action == CLIAction::Fail)
                actual.push_back("failed");
            Check(Json(actual) == replay.at("outputs").at(i),
                  "native parser drift: " + replay.at("name").get<std::string>());
        }
    }
}
} // namespace
int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--probe-official") {
        char directory[] = "/tmp/xsime-official-codex-probe-XXXXXX";
        if (!mkdtemp(directory))
            return 1;
        CodexRequest request;
        request.root = directory;
        request.executable = argv[2];
        request.mode = CodexMode::Probe;
        request.timeout = 4s;
        std::atomic<bool> cancel{false};
        CodexEvent final;
        RunCodex(request, cancel, [&](CodexEvent event) {
            if (event.finished)
                final = event;
        });
        std::filesystem::remove_all(directory);
        Check(final.finished && final.error == CodexError::LoginRequired,
              "Official CLI could not complete initialize, empty MCP and isolated account/read: " +
                  std::string(CodexErrorMessage(final.error)));
        std::cout << "Official Linux CLI: isolated prompt-free initialize, empty MCP, account/read "
                     "and login-required detection passed\n";
        return 0;
    }

    if (argc != 2)
        return 2;
    const auto root = std::filesystem::temp_directory_path() / ("rimes-codex-" + RandomID());
    (void)PrivateDirectory(root);
    setenv("RIMES_USER_DIR", root.c_str(), 1);
    try {
        Replays();
        for (const auto& url : {"https://auth.openai.com/x", "https://chatgpt.com:443/x"})
            Check(SafeCodexAuthorizationURL(url), "safe login URL rejected");
        for (const auto& url : {"http://auth.openai.com/", "https://auth.openai.com.evil.invalid/",
                                "https://auth.openai.com@evil.invalid/",
                                "https://auth.openai.com:444/", "https://auth.openai.com/\n"})
            Check(!SafeCodexAuthorizationURL(url), "unsafe login URL accepted");
        const auto environment =
            CodexEnvironment("/usr/bin/codex", root / "ai/codex-home", root / "request");
        Check(environment.at("CODEX_HOME") != std::string(std::getenv("HOME")) + "/.codex",
              "ambient Codex home used");
        for (const auto* poison :
             {"OPENAI_API_KEY", "CODEX_API_KEY", "CODEX_BASE_URL", "NODE_OPTIONS", "PYTHONPATH"})
            setenv(poison, "fixture-poison", 1);
        CodexRequest request;
        request.root = root;
        request.executable = std::filesystem::absolute(argv[1]);
        request.mode = CodexMode::Text;
        request.model = "gpt-6-sol";
        request.effort = "high";
        request.text = "公开原文 $(touch /tmp/never-executed) `never-executed`";
        request.timeout = 3s;
        {
            auto missing = request;
            missing.executable = root / "missing-codex";
            std::atomic<bool> cancelled{false};
            CodexEvent final;
            RunCodex(missing, cancelled, [&](CodexEvent event) {
                if (event.finished)
                    final = event;
            });
            Check(final.finished && final.error == CodexError::Missing && final.text.empty(),
                  "missing CLI not reported");
        }
        const PrivateDirectory home(root / "ai/codex-home");
        Check(!HasCodexCredential(root), "missing credential considered ready");
        home.Write("auth.json",
                   Json{{"auth_mode", "chatgpt"},
                        {"tokens",
                         {{"access_token", "public-fixture"}, {"refresh_token", "public-fixture"}}}}
                       .dump());
        Check(HasCodexCredential(root), "private local readiness failed");
        for (const auto& [mode, error] : std::vector<std::pair<std::string, CodexError>>{
                 {"fragmented", CodexError::None},
                 {"logged-out", CodexError::LoginRequired},
                 {"unsupported", CodexError::Incompatible},
                 {"mcp", CodexError::Incompatible},
                 {"malformed", CodexError::Protocol},
                 {"model-mismatch", CodexError::Protocol},
                 {"server-request", CodexError::Protocol},
                 {"interrupted", CodexError::Protocol},
                 {"oversized", CodexError::TooLarge},
                 {"timeout", CodexError::Timeout}}) {
            home.Write("fixture-mode", mode);
            CodexEvent final;
            unsigned completions = 0, previews = 0;
            std::atomic<bool> cancelled{false};
            auto run = request;
            if (mode == "timeout")
                run.timeout = 200ms;
            RunCodex(run, cancelled, [&](CodexEvent event) {
                if (event.finished) {
                    ++completions;
                    final = event;
                } else if (!event.text.empty()) {
                    ++previews;
                    Check(event.text.find("NEVER DISPLAY") == event.text.npos &&
                              event.text.find("blocks") == event.text.npos,
                          "non-answer leaked");
                }
            });
            Check(completions == 1 && final.error == error,
                  "unexpected result: " + mode + ": " + CodexErrorMessage(final.error));
            if (error == CodexError::None)
                Check(previews >= 2 && final.text == "公开🙂。第二句。" && final.blocks.size() == 2,
                      "semantic streaming failed");
            else
                Check(final.text.empty() && final.blocks.empty(), "failed text became deliverable");
        }
        {
            home.Write("fixture-mode", "tree-cancel");
            std::atomic<bool> cancelled{false};
            std::thread stop([&] {
                for (int i = 0; i < 200 && !std::filesystem::exists(home.Path() / "child-pid"); ++i)
                    std::this_thread::sleep_for(10ms);
                cancelled.store(true);
            });
            CodexEvent final;
            const auto started = std::chrono::steady_clock::now();
            RunCodex(request, cancelled, [&](CodexEvent event) {
                if (event.finished)
                    final = event;
            });
            stop.join();
            Check(final.error == CodexError::Cancelled && final.text.empty(),
                  "cancellation completed text");
            Check(std::chrono::steady_clock::now() - started < 3s,
                  "process group cancellation stalled");
            const auto child = home.Read("child-pid", 32);
            Check(child.has_value(), "fixture descendant never started");
            bool stopped = false;
            for (int attempt = 0; attempt < 50; ++attempt) {
                std::ifstream proc("/proc/" + *child + "/stat");
                std::string stat;
                std::getline(proc, stat);
                stopped =
                    stat.empty() || stat.find(") Z") != stat.npos || stat.find(") X") != stat.npos;
                if (stopped)
                    break;
                std::this_thread::sleep_for(10ms);
            }
            Check(stopped, "descendant survived group cancellation");
        }
        for (const auto& mode : {"login", "bad-login-url", "login-cancel"}) {
            home.Write("fixture-mode", mode);
            std::atomic<bool> cancelled{false};
            auto run = request;
            run.mode = CodexMode::Login;
            run.timeout = 2s;
            CodexEvent final;
            unsigned urls = 0;
            RunCodex(run, cancelled, [&](CodexEvent event) {
                if (!event.authorization_url.empty()) {
                    ++urls;
                    if (std::string(mode) == "login-cancel")
                        cancelled.store(true);
                }
                if (event.finished)
                    final = event;
            });
            const auto expected = std::string(mode) == "login"          ? CodexError::None
                                  : std::string(mode) == "login-cancel" ? CodexError::Cancelled
                                                                        : CodexError::Protocol;
            Check(final.error == expected &&
                      urls == (std::string(mode) == "bad-login-url" ? 0u : 1u),
                  "login binding or URL validation failed");
        }
        for (const auto& mode : {"image", "image-missing"}) {
            home.Write("fixture-mode", mode);
            std::atomic<bool> cancelled{false};
            auto run = request;
            run.mode = CodexMode::Image;
            run.skill = "imagegen";
            CodexEvent final;
            RunCodex(run, cancelled, [&](CodexEvent event) {
                Check(event.text.empty(), "image task leaked text into Buffer");
                if (event.finished)
                    final = event;
            });
            Check(final.error ==
                      (std::string(mode) == "image" ? CodexError::None : CodexError::Protocol),
                  "explicit image skill failed");
            if (final.error == CodexError::None)
                Check(final.image_started && !final.image_path.empty(), "image event missing");
        }
        Check(std::filesystem::is_empty(root / "ai/requests"), "private workspaces leaked");
        std::filesystem::remove_all(root);
        std::cout << "Codex native parser/request parity, fixed argv, private home, "
                     "authentication, semantic stream, login, explicit image skill and "
                     "process-tree cancellation passed\n";
    } catch (...) {
        std::filesystem::remove_all(root);
        throw;
    }
}
