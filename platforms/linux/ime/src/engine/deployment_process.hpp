#pragma once

#include <filesystem>
#include <string>
#include <sys/types.h>

namespace rimes::linuxime {

// Only the deploy worker runs cancellable maintenance. The Fcitx process never
// abandons a librime thread or unloads code while that thread is still running.
class DeploymentProcess final {
public:
    ~DeploymentProcess();
    bool Start(const std::filesystem::path& executable,
               const std::string& shared, const std::string& user,
               const std::string& log, bool full, int lock_fd,
               std::string* error);
    bool Poll() noexcept;  // true after the child has been reaped
    int ExitCode() const noexcept { return exit_code_; }
    void Stop() noexcept;
private:
    pid_t pid_ = -1;
    int exit_code_ = -1;
};

}  // namespace rimes::linuxime
