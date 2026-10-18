#include "cli_session.hpp"
#include <array>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace rimes::runtime {
VerifiedCLIExecutable::VerifiedCLIExecutable(const std::filesystem::path& candidate) {
    std::error_code error;
    path = std::filesystem::canonical(candidate, error);
    if (error || !path.is_absolute() || lstat(path.c_str(), &identity) || !S_ISREG(identity.st_mode) ||
        (identity.st_uid != geteuid() && identity.st_uid != 0) || (identity.st_mode & 0022) || access(path.c_str(), X_OK))
        throw ExecutableUnavailable("CLI executable unavailable");
}
bool VerifiedCLIExecutable::Matches() const {
    struct stat now {};
    return !lstat(path.c_str(), &now) && now.st_dev == identity.st_dev && now.st_ino == identity.st_ino &&
        now.st_size == identity.st_size && now.st_mtim.tv_sec == identity.st_mtim.tv_sec &&
        now.st_mtim.tv_nsec == identity.st_mtim.tv_nsec && now.st_ctim.tv_sec == identity.st_ctim.tv_sec &&
        now.st_ctim.tv_nsec == identity.st_ctim.tv_nsec && now.st_mode == identity.st_mode && now.st_uid == identity.st_uid;
}
std::map<std::string, std::string> CLIEnvironment(const std::filesystem::path& executable,
                                                const std::filesystem::path& temporary) {
    std::map<std::string, std::string> environment;
    for (const auto* name :
         {"HOME", "USER", "LOGNAME", "LANG", "LC_ALL", "SSL_CERT_FILE", "SSL_CERT_DIR"})
        if (const auto* value = std::getenv(name))
            environment[name] = value;
    std::vector<std::string> paths{executable.parent_path().string()};
    const auto path = executable.string();
    for (const auto* marker : {"/.nvm/versions/node/", "/.local/share/fnm/node-versions/"}) {
        const auto at = path.find(marker);
        if (at != path.npos) {
            const auto end = path.find('/', at + std::string(marker).size());
            if (end != path.npos)
                paths.push_back(path.substr(0, end) +
                                (std::string(marker).find("fnm") == std::string::npos
                                     ? "/bin"
                                     : "/installation/bin"));
        }
    }
    if (const auto* user = std::getenv("HOME"))
        for (const auto* marker : {".volta", ".bun"})
            if (path.find(std::string("/") + marker + "/") != path.npos)
                paths.push_back(std::string(user) + "/" + marker + "/bin");
    paths.insert(paths.end(), {"/usr/local/bin", "/usr/bin", "/bin", "/usr/sbin", "/sbin"});
    std::string search;
    for (const auto& item : paths) {
        if (!search.empty())
            search += ':';
        search += item;
    }
    environment["PATH"] = search;
    environment["TMPDIR"] = temporary.string();
    return environment;
}
namespace {
struct FD {
    int value = -1;
    ~FD() { Reset(); }
    void Reset() {
        if (value >= 0)
            close(value);
        value = -1;
    }
};
struct PipeMask {
    sigset_t previous{}, blocked{};
    PipeMask() {
        sigemptyset(&blocked);
        sigaddset(&blocked, SIGPIPE);
        pthread_sigmask(SIG_BLOCK, &blocked, &previous);
    }
    ~PipeMask() {
        if (!sigismember(&previous, SIGPIPE)) {
            timespec zero{};
            while (sigtimedwait(&blocked, nullptr, &zero) >= 0) {
            }
        }
        pthread_sigmask(SIG_SETMASK, &previous, nullptr);
    }
};
struct Child {
    pid_t pid = -1;
    ~Child() {
        if (pid <= 0)
            return;
        // Keep the leader unreaped until group cleanup completes so its process
        // group identity cannot be recycled between TERM and KILL.
        kill(-pid, SIGTERM);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        kill(-pid, SIGKILL);
        while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {
        }
    }
};
bool Plain(const std::string& text) { return text.find('\0') == text.npos; }
} // namespace
CLIExit RunCLISession(const CLISessionOptions& options, const std::atomic<bool>& cancelled,
                      std::string input,
                      const std::function<CLIAction(std::string_view, std::string&)>& receive,
                      const std::function<std::string()>& interrupt) {
    if (cancelled.load())
        return CLIExit::Cancelled;
    if (options.argv.empty() || options.argv.front().empty() ||
        options.argv.front().front() != '/' || !options.directory.is_absolute() ||
        !Plain(options.directory.string()) || options.timeout.count() <= 0 ||
        !options.maximum_output || input.size() > options.maximum_pending_input || !receive)
        return CLIExit::Unavailable;
    std::vector<char*> argv, environment;
    std::vector<std::string> values;
    for (const auto& arg : options.argv) {
        if (!Plain(arg))
            return CLIExit::Unavailable;
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    for (const auto& [key, value] : options.environment) {
        if (key.empty() || key.find('=') != key.npos || !Plain(key) || !Plain(value))
            return CLIExit::Unavailable;
        values.push_back(key + "=" + value);
    }
    for (auto& value : values)
        environment.push_back(value.data());
    environment.push_back(nullptr);
    int in[2], out[2];
    if (pipe2(in, O_CLOEXEC) < 0)
        return CLIExit::Unavailable;
    FD child_in{in[0]}, writer{in[1]};
    if (pipe2(out, O_CLOEXEC) < 0)
        return CLIExit::Unavailable;
    FD reader{out[0]}, child_out{out[1]}, null{open("/dev/null", O_WRONLY | O_CLOEXEC)};
    if (null.value < 0)
        return CLIExit::Unavailable;
    PipeMask signals;
    posix_spawn_file_actions_t files;
    posix_spawnattr_t attributes;
    if (posix_spawn_file_actions_init(&files))
        return CLIExit::Unavailable;
    if (posix_spawnattr_init(&attributes)) {
        posix_spawn_file_actions_destroy(&files);
        return CLIExit::Unavailable;
    }
    int setup = posix_spawn_file_actions_adddup2(&files, child_in.value, STDIN_FILENO);
    setup |= posix_spawn_file_actions_adddup2(&files, child_out.value, STDOUT_FILENO);
    setup |= posix_spawn_file_actions_adddup2(&files, null.value, STDERR_FILENO);
    setup |= posix_spawn_file_actions_addchdir_np(&files, options.directory.c_str());
    for (const auto fd : {child_in.value, writer.value, reader.value, child_out.value, null.value})
        if (fd > STDERR_FILENO)
            setup |= posix_spawn_file_actions_addclose(&files, fd);
    setup |= posix_spawnattr_setpgroup(&attributes, 0);
    setup |= posix_spawnattr_setsigmask(&attributes, &signals.previous);
    setup |= posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK);
    Child child;
    const int spawned = setup ? setup
                              : posix_spawn(&child.pid, options.argv.front().c_str(), &files,
                                            &attributes, argv.data(), environment.data());
    posix_spawn_file_actions_destroy(&files);
    posix_spawnattr_destroy(&attributes);
    if (spawned) {
        child.pid = -1;
        return CLIExit::Unavailable;
    }
    child_in.Reset();
    child_out.Reset();
    null.Reset();
    for (const auto fd : {reader.value, writer.value})
        if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) < 0)
            return CLIExit::Unavailable;
    const auto deadline = std::chrono::steady_clock::now() + options.timeout;
    std::size_t received = 0;
    bool output_closed = false;
    for (;;) {
        if (cancelled.load() || std::chrono::steady_clock::now() >= deadline) {
            // Best effort protocol cancellation. Never wait for an unresponsive
            // app-server before terminating its owned process group.
            if (interrupt) {
                try {
                    const auto message = interrupt();
                    if (message.size() <= 8192 && !message.empty()) {
                        const auto ignored = write(writer.value, message.data(), message.size());
                        (void)ignored;
                    }
                } catch (...) {
                }
            }
            return cancelled.load() ? CLIExit::Cancelled : CLIExit::Timeout;
        }
        if (input.size() > options.maximum_pending_input)
            return CLIExit::TooLarge;
        if (options.close_stdin && input.empty()) writer.Reset();
        if (output_closed) {
            siginfo_t status{};
            if (waitid(P_PID, child.pid, &status, WEXITED | WNOHANG | WNOWAIT) < 0 && errno != EINTR)
                return CLIExit::Protocol;
            if (status.si_pid == child.pid)
                return status.si_code == CLD_EXITED && status.si_status == 0 ? CLIExit::Completed : CLIExit::Protocol;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        pollfd fds[] = {{reader.value, POLLIN, 0},
                        {writer.value, static_cast<short>(input.empty() ? 0 : POLLOUT), 0}};
        if (poll(fds, 2, 20) < 0 && errno != EINTR)
            return CLIExit::Protocol;
        if (!input.empty() && (fds[1].revents & (POLLOUT | POLLHUP | POLLERR))) {
            const auto sent = write(writer.value, input.data(), input.size());
            if (sent > 0)
                input.erase(0, static_cast<std::size_t>(sent));
            else if (sent < 0 && errno != EINTR && errno != EAGAIN)
                return CLIExit::Protocol;
        }
        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            std::array<char, 16384> buffer{};
            // Bound each drain so a flooding producer cannot starve cancellation.
            for (int batch = 0; batch < 16; ++batch) {
                const auto count = read(reader.value, buffer.data(), buffer.size());
                if (!count) {
                    if (!options.finish_at_eof) return CLIExit::Protocol;
                    try {
                        if (receive({}, input) != CLIAction::Complete) return CLIExit::Protocol;
                    } catch (...) { return CLIExit::Protocol; }
                    output_closed = true;
                    reader.Reset();
                    break;
                }
                if (count < 0) {
                    if (errno == EINTR)
                        continue;
                    if (errno == EAGAIN)
                        break;
                    return CLIExit::Protocol;
                }
                const auto size = static_cast<std::size_t>(count);
                if (size > options.maximum_output - received)
                    return CLIExit::TooLarge;
                received += size;
                if (cancelled.load())
                    break;
                CLIAction action;
                try {
                    action = receive(std::string_view(buffer.data(), size), input);
                } catch (...) {
                    return CLIExit::Protocol;
                }
                if (cancelled.load())
                    return CLIExit::Cancelled;
                if (action == CLIAction::Complete && !options.finish_at_eof)
                    return CLIExit::Completed;
                if (action == CLIAction::Fail)
                    return CLIExit::Protocol;
            }
        }
    }
}
} // namespace rimes::runtime
