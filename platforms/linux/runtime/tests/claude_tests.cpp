#include "claude_channel.hpp"
#include <fstream>
#include <iostream>
#include <signal.h>
#include <thread>
#include <unistd.h>
using namespace rimes::runtime;
using namespace std::chrono_literals;
namespace {
void Check(bool ok, const std::string& reason) {
    if (!ok)
        throw std::runtime_error(reason);
}
template <class F> void Wait(F condition) {
    const auto end = std::chrono::steady_clock::now() + 4s;
    while (!condition()) {
        Check(std::chrono::steady_clock::now() < end, "Claude worker deadline");
        std::this_thread::sleep_for(10ms);
    }
}
void Replays() {
    for (const auto& fixture : ClaudeContract().at("fixtures")) {
        std::vector<std::string> snapshots;
        ClaudeProtocol parser([&](ClaudeEvent event) {
            if (!event.text.empty())
                snapshots.push_back(event.text);
        });
        const auto bytes = fixture.at("wire").get<std::string>();
        bool accepted = true;
        for (const auto& byte : bytes)
            if (parser.Feed(std::string_view(&byte, 1)) == CLIAction::Fail) {
                accepted = false;
                break;
            }
        if (accepted)
            accepted = parser.Finish() == CLIAction::Complete;
        Check(accepted == fixture.at("accepted"),
              "native parser acceptance differs: " + fixture.at("name").get<std::string>());
        if (accepted) {
            Check(Json::parse(parser.FinalText()) ==
                      Json::parse(fixture.at("finalText").get<std::string>()),
                  "native final result differs");
            Check(Json(snapshots) == fixture.at("snapshots"), "native streaming result differs");
        }
    }
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 2 && argc != 3)
        return 2;
    const auto root = std::filesystem::temp_directory_path() / ("xsime-claude-tests-" + RandomID());
    PrivateDirectory home(root / "home");
    setenv("HOME", home.Path().c_str(), 1);
    setenv("RIMES_USER_DIR", root.c_str(), 1);
    try {
        ClaudeRequest request;
        request.executable = std::filesystem::absolute(argv[argc - 1]);
        request.root = root;
        request.effort = "high";
        request.text = "公开请求，请分成两句。";
        request.timeout = 2s;
        std::atomic<bool> cancelled{false};
        if (argc == 3 && std::string(argv[1]) == "--probe-official") {
            ClaudeEvent final;
            RunClaude(request, cancelled, [&](auto event) {
                if (event.finished)
                    final = std::move(event);
            });
            Check(final.finished && final.error == ClaudeError::LoginRequired,
                  "official isolated capability/auth probe failed: " +
                      std::string(ClaudeErrorMessage(final.error)));
            std::filesystem::remove_all(root);
            std::cout << "Official Claude CLI: declared safe/stream parameters and isolated "
                         "prompt-free authentication probe passed\n";
            return 0;
        }
        Replays();
        Check(ClaudeHelpOption("  -p, --print  Output", "--print"), "flag alias not accepted");
        Check(!ClaudeHelpOption("  --print  Use --safe-mode for safety", "--safe-mode"),
              "help prose became a capability");
        for (const auto* key :
             {"ANTHROPIC_API_KEY", "ANTHROPIC_BASE_URL", "CLAUDE_CODE_OAUTH_TOKEN",
              "CLAUDE_CONFIG_DIR", "NODE_OPTIONS", "PYTHONPATH", "CODEX_HOME"})
            setenv(key, "PRIVATE-ENV-FIXTURE", 1);
        auto run = [&](const std::string& mode, ClaudeError expected,
                       ClaudeMode operation = ClaudeMode::Text) {
            home.Write("fixture-mode", mode);
            cancelled.store(false);
            request.mode = operation;
            int finished = 0;
            ClaudeEvent final;
            RunClaude(request, cancelled, [&](auto event) {
                Check(event.text.find("PRIVATE-") == event.text.npos &&
                          event.activity.find("PRIVATE-") == event.activity.npos,
                      "private process output reached UI");
                if (!event.text.empty())
                    Check(event.text.front() != '{', "raw JSON reached Buffer");
                if (event.finished) {
                    ++finished;
                    final = std::move(event);
                }
            });
            Check(finished == 1 && final.error == expected,
                  mode + ": wrong completion: " + ClaudeErrorMessage(final.error));
            if (expected != ClaudeError::None)
                Check(final.text.empty() && final.blocks.empty(),
                      "failed CLI exposed deliverable result");
            return final;
        };
        const auto valid = run("fragmented", ClaudeError::None);
        Check(valid.text == "公开🙂。第二句。" && valid.blocks.size() == 2,
              "semantic blocks differ");
        run("unsupported", ClaudeError::Incompatible);
        run("logged-out", ClaudeError::LoginRequired);
        run("auth-exit-fail", ClaudeError::LoginRequired);
        for (const auto* bad : {"tools", "mcp", "model", "session", "tool-block", "failed",
                                "no-result", "duplicate-key", "exit-fail"})
            run(bad, ClaudeError::Protocol);
        run("oversized", ClaudeError::TooLarge);
        request.timeout = 150ms;
        run("timeout", ClaudeError::Timeout);
        request.timeout = 2s;
        run("login", ClaudeError::None, ClaudeMode::Login);
        Check(home.Read("logged-in", 128).has_value(), "official login did not own credentials");
        for (const auto* mode : {"login-hold", "child"}) {
            home.Write("fixture-mode", mode);
            home.Remove("login-started");
            cancelled.store(false);
            request.mode = std::string(mode) == "child" ? ClaudeMode::Text : ClaudeMode::Login;
            ClaudeEvent final;
            std::atomic<bool> done{false};
            std::thread worker([&] {
                RunClaude(request, cancelled, [&](auto event) {
                    if (event.finished)
                        final = std::move(event);
                });
                done.store(true);
            });
            try {
                Wait([&] {
                    return home
                        .Read(std::string(mode) == "child" ? "child-pid" : "login-started", 128)
                        .has_value();
                });
            } catch (...) {
                cancelled.store(true);
                worker.join();
                throw;
            }
            cancelled.store(true);
            worker.join();
            Check(done.load() && final.error == ClaudeError::Cancelled,
                  "cancel did not complete exactly once");
            if (std::string(mode) == "child") {
                const auto pid = std::stoi(*home.Read("child-pid", 128));
                Wait([&] {
                    if (kill(pid, 0) < 0)
                        return true;
                    std::ifstream input("/proc/" + std::to_string(pid) + "/stat");
                    std::string id, name;
                    char state;
                    input >> id >> name >> state;
                    return state == 'Z';
                });
            }
        }
        // An old user install must not hide a compatible executable later in PATH.
        PrivateDirectory old_bin(home.Path() / ".local/bin"), fallback(root / "fallback");
        old_bin.Write("claude", "#!/bin/sh\nprintf '  --print  old CLI without safe mode\\n'\n");
        std::filesystem::permissions(old_bin.Path() / "claude", std::filesystem::perms::owner_all);
        std::filesystem::create_symlink(request.executable, fallback.Path() / "claude");
        setenv("PATH", fallback.Path().c_str(), 1);
        request.executable.clear();
        run("fragmented", ClaudeError::None);
        setenv("RIMES_CLAUDE_PATH", (old_bin.Path() / "claude").c_str(), 1);
        run("fragmented", ClaudeError::Incompatible);
        unsetenv("RIMES_CLAUDE_PATH");
        request.executable = root / "missing";
        run("text", ClaudeError::Missing);
        Check(std::filesystem::is_empty(root / "ai/requests"),
              "temporary requests survived completion");
        std::filesystem::remove_all(root);
        std::cout << "Claude channel: native fragmented replays, explicit print EOF, exit status, "
                     "login, isolation, reasoning concealment, tool refusal, bounded output, "
                     "timeout and process-tree cancellation passed\n";
    } catch (...) {
        std::filesystem::remove_all(root);
        throw;
    }
}
