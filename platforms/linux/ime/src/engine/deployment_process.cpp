#include "deployment_process.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace rimes::linuxime {

DeploymentProcess::~DeploymentProcess() { Stop(); }

bool DeploymentProcess::Start(const std::filesystem::path& executable,
    const std::string& shared, const std::string& user, const std::string& log,
    bool full, int lock_fd, std::string* error) {
    if (pid_ > 0 || lock_fd < 0 || !executable.is_absolute()) {
        if (error) *error = "invalid or already running deployment worker";
        return false;
    }
    std::vector<std::string> values{executable.string(), shared, user, log, full ? "1" : "0"};
    std::vector<char*> args;
    for (auto& value : values) args.push_back(value.data());
    args.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    int status = posix_spawn_file_actions_init(&actions);
    if (status != 0) { if (error) *error = "could not initialize deployment spawn"; return false; }
    // The same open file description holds the user-directory lock even if
    // Fcitx unexpectedly dies. A replacement cannot write alongside this child.
    status = posix_spawn_file_actions_adddup2(&actions, lock_fd, 3);
#if defined(__GLIBC__)
#if __GLIBC_PREREQ(2, 34)
    if (status == 0) status = posix_spawn_file_actions_addclosefrom_np(&actions, 4);
#endif
#endif
    if (status == 0) status = posix_spawn(&pid_, executable.c_str(), &actions,
                                         nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (status != 0) {
        pid_ = -1;
        if (error) *error = std::string("could not start deployment worker: ") + std::strerror(status);
        return false;
    }
    exit_code_ = -1;
    return true;
}

bool DeploymentProcess::Poll() noexcept {
    if (pid_ <= 0) return true;
    int status = 0;
    const auto result = waitpid(pid_, &status, WNOHANG);
    if (result == 0 || (result < 0 && errno == EINTR)) return false;
    exit_code_ = result == pid_ && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    pid_ = -1;
    return true;
}

void DeploymentProcess::Stop() noexcept {
    if (Poll()) return;
    kill(pid_, SIGTERM);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    while (!Poll() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (pid_ <= 0) return;
    kill(pid_, SIGKILL);
    int status = 0;
    while (waitpid(pid_, &status, 0) < 0 && errno == EINTR) {}
    pid_ = -1;
    exit_code_ = -1;
}

}  // namespace rimes::linuxime
