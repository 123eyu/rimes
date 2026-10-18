#include "capsule_hash.hpp"
#include "capsule_resources.hpp"
#include "capsule_text.hpp"
#include "cli_session.hpp"
#include "terminal_session.hpp"
#include <csignal>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
namespace rimes::mailbox {
namespace {
struct Identity {
    pid_t session = 0;
    unsigned long long start = 0;
};
Identity Process(pid_t pid) {
    std::ifstream input("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    std::getline(input, line);
    const auto end = line.rfind(')');
    if (end == line.npos)
        return {};
    std::istringstream fields(line.substr(end + 2));
    std::string field;
    Identity result;
    for (int index = 3; index <= 22 && fields >> field; ++index) {
        try {
            if (index == 6)
                result.session = std::stoi(field);
            if (index == 22)
                result.start = std::stoull(field);
        } catch (...) {
            return {};
        }
    }
    return result;
}
// Interactive shells create foreground process groups. Stop every group in the
// owned PTY session, using pidfds so a PID reused between inspection and signal
// cannot target an unrelated process. Detached new sessions are not ours.
void SignalSession(pid_t leader, unsigned long long start, int signal) {
    if (leader <= 1 || !start)
        return;
    const auto current = Process(leader);
    if (current.start && current.start != start)
        return;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator("/proc", error)) {
        const auto name = entry.path().filename().string();
        if (name.empty() || name.find_first_not_of("0123456789") != name.npos)
            continue;
        pid_t pid = 0;
        try {
            pid = std::stoi(name);
        } catch (...) {
            continue;
        }
        const int fd = syscall(SYS_pidfd_open, pid, 0);
        if (fd < 0)
            continue;
        const auto identity = Process(pid);
        struct stat info {};
        if (identity.session == leader && identity.start >= start &&
            !stat(entry.path().c_str(), &info) && info.st_uid == getuid())
            syscall(SYS_pidfd_send_signal, fd, signal, nullptr, 0);
        close(fd);
    }
}
std::vector<std::string> Environment(const std::filesystem::path& executable,
                                     const std::filesystem::path& directory) {
    std::vector<std::string> result;
    // Match the native clean terminal environment, adding Linux desktop socket
    // locations so interactive CLI login and user-started GUI tools work.
    for (const char* key : {"HOME", "USER", "LOGNAME", "SHELL", "DISPLAY", "WAYLAND_DISPLAY",
                            "XAUTHORITY", "XDG_RUNTIME_DIR", "DBUS_SESSION_BUS_ADDRESS",
                            "SSH_AUTH_SOCK", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"})
        if (const auto* value = getenv(key))
            result.push_back(std::string(key) + "=" + value);
    std::string path = executable.parent_path().string();
    if (const auto* inherited = getenv("PATH"))
        path += ":" + std::string(inherited);
    if (const auto* home = getenv("HOME"))
        for (const auto* part : {"/.local/bin", "/.bun/bin", "/.cargo/bin"})
            path += ":" + std::string(home) + part;
    path += ":/usr/local/bin:/usr/bin:/bin";
    result.insert(result.end(), {"PATH=" + path, "PWD=" + directory.string(), "TERM=xterm-256color",
                                 "COLORTERM=truecolor", "LANG=C.UTF-8"});
    return result;
}
} // namespace
struct TerminalSession::State {
    std::string id = capsule::NewUuidV4(), kind, title, error;
    std::filesystem::path directory;
    GtkWidget* widget = nullptr;
    GCancellable* cancellable = g_cancellable_new();
    std::function<void()> changed;
    pid_t pid = 0;
    unsigned long long process_start = 0;
    double started = Now();
    bool starting = false, running = false, stopping = false, disposed = false;
    std::optional<int> exit_code;
    guint kill_timer = 0;
    ~State() {
        g_signal_handlers_disconnect_by_data(widget, this);
        g_object_unref(cancellable);
        gtk_widget_destroy(widget);
        g_object_unref(widget);
    }
    void Changed() {
        if (!disposed && changed)
            changed();
    }
    void Terminate(bool immediately) {
        stopping = true;
        g_cancellable_cancel(cancellable);
        SignalSession(pid, process_start, immediately ? SIGKILL : SIGTERM);
    }
};
TerminalSession::TerminalSession(std::string kind, std::filesystem::path directory,
                                 std::function<void()> changed)
    : state_(std::make_shared<State>()) {
    auto& s = *state_;
    s.kind = std::move(kind);
    s.directory = std::move(directory);
    s.title = s.directory.filename().string();
    s.changed = std::move(changed);
    s.widget = GTK_WIDGET(g_object_ref_sink(vte_terminal_new()));
    auto* terminal = VTE_TERMINAL(s.widget);
    auto* font = pango_font_description_from_string("Monospace 13px");
    vte_terminal_set_font(terminal, font);
    pango_font_description_free(font);
    GdkRGBA background{0.09, 0.11, 0.13, 1}, foreground{0.87, 0.90, 0.90, 1};
    vte_terminal_set_colors(terminal, &foreground, &background, nullptr, 0);
    vte_terminal_set_scrollback_lines(terminal, 10000);
    vte_terminal_set_allow_hyperlink(terminal, false);
    vte_terminal_set_audible_bell(terminal, false);
    vte_terminal_set_scroll_on_keystroke(terminal, true);
    vte_terminal_set_scroll_on_output(terminal, false);
    gtk_widget_set_hexpand(s.widget, TRUE);
    gtk_widget_set_vexpand(s.widget, TRUE);
    g_signal_connect(terminal, "key-press-event",
                     G_CALLBACK(+[](GtkWidget* widget, GdkEventKey* event, gpointer) -> gboolean {
                         if ((event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_MOD1_MASK)) !=
                                 (GDK_CONTROL_MASK | GDK_SHIFT_MASK) ||
                             !vte_terminal_get_input_enabled(VTE_TERMINAL(widget)))
                             return FALSE;
                         if (event->keyval == GDK_KEY_c || event->keyval == GDK_KEY_C) {
                             vte_terminal_copy_clipboard_format(VTE_TERMINAL(widget),
                                                                VTE_FORMAT_TEXT);
                             return TRUE;
                         }
                         if (event->keyval == GDK_KEY_v || event->keyval == GDK_KEY_V) {
                             vte_terminal_paste_clipboard(VTE_TERMINAL(widget));
                             return TRUE;
                         }
                         return FALSE;
                     }),
                     &s);
    g_signal_connect(terminal, "window-title-changed",
                     G_CALLBACK(+[](VteTerminal* view, gpointer p) {
                         auto& state = *static_cast<State*>(p);
                         const char* title = vte_terminal_get_window_title(view);
                         if (title && *title)
                             state.title = capsule::OneLine(capsule::CharacterPrefix(title, 100));
                         state.Changed();
                     }),
                     &s);
    g_signal_connect(
        terminal, "child-exited", G_CALLBACK(+[](VteTerminal*, gint status, gpointer p) {
            auto& state = *static_cast<State*>(p);
            state.running = state.starting = false;
            state.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            // A finished shell must not strand background children in its PTY.
            SignalSession(state.pid, state.process_start, SIGKILL);
            state.Changed();
        }),
        &s);
}
TerminalSession::~TerminalSession() {
    state_->disposed = true;
    state_->changed = {};
    if (state_->kill_timer) {
        g_source_remove(state_->kill_timer);
        state_->kill_timer = 0;
    }
    state_->Terminate(true);
}
void TerminalSession::Start(const std::filesystem::path& executable,
                            const std::vector<std::string>& arguments) {
    auto& s = *state_;
    if (s.starting || s.pid || s.disposed)
        throw std::runtime_error("终端会话不能重复启动");
    runtime::VerifiedCLIExecutable verified(executable);
    const int directory_fd = capsule::OpenResource(s.directory);
    struct stat info {};
    const bool valid = directory_fd >= 0 && !fstat(directory_fd, &info) && S_ISDIR(info.st_mode);
    if (directory_fd >= 0)
        close(directory_fd);
    if (!valid)
        throw std::runtime_error("工作目录不可用，请重新选择本机文件夹");
    std::vector<std::string> args{verified.path.string()};
    args.insert(args.end(), arguments.begin(), arguments.end());
    auto environment = Environment(verified.path, s.directory);
    std::vector<char*> argv, envv;
    for (auto& value : args)
        argv.push_back(value.data());
    for (auto& value : environment)
        envv.push_back(value.data());
    argv.push_back(nullptr);
    envv.push_back(nullptr);
    s.starting = true;
    vte_terminal_spawn_async(
        VTE_TERMINAL(s.widget), VTE_PTY_DEFAULT, s.directory.c_str(), argv.data(), envv.data(),
        static_cast<GSpawnFlags>(VTE_SPAWN_NO_PARENT_ENVV), nullptr, nullptr, nullptr, 5000,
        s.cancellable,
        +[](VteTerminal*, GPid pid, GError* error, gpointer raw) {
            std::unique_ptr<std::shared_ptr<State>> holder(
                static_cast<std::shared_ptr<State>*>(raw));
            auto state = *holder;
            state->starting = false;
            if (error)
                state->error = "终端启动失败，请检查 CLI 与工作目录";
            else {
                state->pid = pid;
                state->process_start = Process(pid).start;
                state->running = !state->exit_code.has_value();
                if (state->stopping || state->disposed)
                    state->Terminate(true);
            }
            state->Changed();
            // VTE still uses its terminal after invoking the spawn callback. A
            // cancelled pending launch must release the final owner on the next
            // main-loop turn, not re-enter VTE disposal inside this callback.
            if (state->disposed)
                g_idle_add_full(
                    G_PRIORITY_DEFAULT_IDLE, +[](gpointer) -> gboolean { return G_SOURCE_REMOVE; },
                    new std::shared_ptr<State>(state),
                    +[](gpointer raw) { delete static_cast<std::shared_ptr<State>*>(raw); });
        },
        new std::shared_ptr<State>(state_));
    s.Changed();
}
void TerminalSession::Stop() {
    auto& s = *state_;
    if ((!s.running && !s.starting) || s.stopping)
        return;
    s.Terminate(false);
    s.kill_timer = g_timeout_add_full(
        G_PRIORITY_DEFAULT, 750,
        +[](gpointer raw) -> gboolean {
            const auto state = *static_cast<std::shared_ptr<State>*>(raw);
            state->kill_timer = 0;
            state->Terminate(true);
            state->Changed();
            return G_SOURCE_REMOVE;
        },
        new std::shared_ptr<State>(state_),
        +[](gpointer raw) { delete static_cast<std::shared_ptr<State>*>(raw); });
    s.Changed();
}
GtkWidget* TerminalSession::Widget() const { return state_->widget; }
const std::string& TerminalSession::ID() const { return state_->id; }
const std::filesystem::path& TerminalSession::Directory() const { return state_->directory; }
void TerminalSession::SetInputEnabled(bool enabled) {
    vte_terminal_set_input_enabled(VTE_TERMINAL(state_->widget), enabled);
}
Json TerminalSession::Inspect() const {
    const auto& s = *state_;
    return {{"id", s.id},
            {"kind", s.kind},
            {"title", s.title},
            {"directory", s.directory.string()},
            {"startedAt", s.started},
            {"running", s.running},
            {"starting", s.starting},
            {"stopping", s.stopping},
            {"pid", s.pid},
            {"error", s.error},
            {"exitCode", s.exit_code ? Json(*s.exit_code) : Json(nullptr)}};
}
std::filesystem::path TerminalExecutable(const std::string& kind) {
    if (kind == "Shell") {
        if (const auto* shell = getenv("SHELL")) {
            if (std::filesystem::path(shell).is_absolute() && !access(shell, X_OK))
                return shell;
        }
        return "/bin/bash";
    }
    const std::string binary = kind == "Codex" ? "codex" : kind == "Claude Code" ? "claude" : "";
    if (binary.empty())
        throw std::runtime_error("未知终端类型");
    if (const auto* home = getenv("HOME")) {
        const auto local = std::filesystem::path(home) / ".local/bin" / binary;
        if (!access(local.c_str(), X_OK))
            return local;
    }
    auto* located = g_find_program_in_path(binary.c_str());
    std::filesystem::path result = located ? located : "";
    g_free(located);
    if (result.empty())
        throw std::runtime_error("未找到 " + kind + "，请先安装相应 CLI。");
    return result;
}
} // namespace rimes::mailbox
