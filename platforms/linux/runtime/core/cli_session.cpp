#include "cli_session.hpp"
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace rimes::runtime {
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
                if (!count)
                    return CLIExit::Protocol; // EOF without protocol completion.
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
                if (action == CLIAction::Complete)
                    return CLIExit::Completed;
                if (action == CLIAction::Fail)
                    return CLIExit::Protocol;
            }
        }
    }
}
} // namespace rimes::runtime
