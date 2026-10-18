#pragma once
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>
#include <sys/stat.h>

namespace rimes::runtime {
struct ExecutableUnavailable : std::runtime_error { using std::runtime_error::runtime_error; };
struct VerifiedCLIExecutable {
    std::filesystem::path path;
    struct stat identity {};
    explicit VerifiedCLIExecutable(const std::filesystem::path& candidate);
    bool Matches() const;
};
std::map<std::string, std::string> CLIEnvironment(const std::filesystem::path& executable,
                                                const std::filesystem::path& temporary);
struct CLISessionOptions {
    std::vector<std::string> argv;
    std::map<std::string, std::string> environment;
    std::filesystem::path directory;
    std::chrono::milliseconds timeout{120000};
    std::size_t maximum_output = 1048576, maximum_pending_input = 2097152;
    // Print-mode CLIs consume stdin to EOF and must exit successfully after a valid result.
    bool close_stdin = false, finish_at_eof = false;
};
enum class CLIAction { Continue, Complete, Fail };
enum class CLIExit { Completed, Cancelled, Timeout, Unavailable, Protocol, TooLarge };
// Fixed argv, explicit environment and cwd. The callback consumes bytes and
// queues bounded stdin; it cannot cause a shell invocation. Always off the UI
// thread. No output is logged, retained globally, or returned after cancellation.
CLIExit RunCLISession(const CLISessionOptions& options, const std::atomic<bool>& cancelled,
                      std::string input,
                      const std::function<CLIAction(std::string_view, std::string&)>& receive,
                      const std::function<std::string()>& interrupt = {});
} // namespace rimes::runtime
