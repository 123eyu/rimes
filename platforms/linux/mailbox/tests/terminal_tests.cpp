#include "capsule_hash.hpp"
#include "mailbox_workspace.hpp"
#include "terminal_files.hpp"
#include "terminal_session.hpp"
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <thread>
using namespace rimes::mailbox;
using namespace rimes::runtime;
namespace {
std::string stage;
void Check(bool value, const char* why) {
    if (!value)
        throw std::runtime_error(stage + ": " + why);
}
void Wait(const std::function<bool()>& predicate, int milliseconds = 8000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    do {
        while (gtk_events_pending())
            gtk_main_iteration();
        try {
            if (predicate())
                return;
        } catch (const StorageBusy&) {
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (std::chrono::steady_clock::now() < end);
    throw std::runtime_error(stage + ": deadline");
}
void Pump(int milliseconds) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    Wait([&] { return std::chrono::steady_clock::now() >= until; }, milliseconds + 1000);
}
GtkWidget* Find(GtkWidget* root, const std::string& name = "") {
    if (name.empty() ? VTE_IS_TERMINAL(root) : name == gtk_widget_get_name(root))
        return root;
    if (!GTK_IS_CONTAINER(root))
        return nullptr;
    auto* children = gtk_container_get_children(GTK_CONTAINER(root));
    GtkWidget* result = nullptr;
    for (auto* c = children; c && !result; c = c->next)
        result = Find(GTK_WIDGET(c->data), name);
    g_list_free(children);
    return result;
}
std::string Text(GtkWidget* terminal) {
    auto* raw = vte_terminal_get_text_format(VTE_TERMINAL(terminal), VTE_FORMAT_TEXT);
    std::string text = raw ? raw : "";
    g_free(raw);
    return text;
}
void Input(GtkWidget* terminal, const std::string& text) {
    vte_terminal_feed_child(VTE_TERMINAL(terminal), text.c_str(), text.size());
}
bool Alive(int pid) {
    std::ifstream file("/proc/" + std::to_string(pid) + "/stat");
    std::string data;
    std::getline(file, data);
    return !data.empty() && data.substr(data.rfind(')') + 2, 1) != "Z";
}
void Click(GtkWidget* button) {
    Check(button && GTK_IS_BUTTON(button) && gtk_widget_is_sensitive(button), "button disabled");
    gtk_button_clicked(GTK_BUTTON(button));
}
template <class F> void Reject(F action) {
    bool failed = false;
    try {
        action();
    } catch (...) {
        failed = true;
    }
    Check(failed, "expected rejection");
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 3)
        return 2;
    const auto root = std::filesystem::temp_directory_path() / ("xsime-terminal-" + RandomID());
    PrivateDirectory profile(root / "profile"), a(root / "a"), b(root / "b"), home(root / "home");
    setenv("RIMES_USER_DIR", profile.Path().c_str(), 1);
    setenv("HOME", home.Path().c_str(), 1);
    setenv("RIMES_PRIVATE_TEST_SECRET", "public-sentinel-never-inherit", 1);
    const auto fixture = std::filesystem::absolute(argv[1]).lexically_normal();
    const auto picture = std::filesystem::absolute(argv[2]).lexically_normal();
    try {
        gtk_init(&argc, &argv);
        std::atomic<bool> cancelled{false};
        stage = "bounded file imports";
        a.Write("public.txt", "公开文本🙂\n第二行\n");
        auto text = ReadTerminalFile(a.Path() / "public.txt", cancelled);
        Check(text.body == "公开文本🙂\n第二行\n" && text.png.empty(), "UTF-8 file changed");
        auto png = ReadTerminalFile(picture, cancelled);
        Check(!png.png.empty() && png.body.find(" × ") != std::string::npos,
              "image was not normalized");
        a.Write("nul", std::string("x\0y", 3));
        Reject([&] { ReadTerminalFile(a.Path() / "nul", cancelled); });
        a.Write("bad-utf8", std::string("\xff", 1));
        Reject([&] { ReadTerminalFile(a.Path() / "bad-utf8", cancelled); });
        a.Write("large-text", std::string(1048577, 'x'));
        Reject([&] { ReadTerminalFile(a.Path() / "large-text", cancelled); });
        a.Write("oversized", "");
        std::filesystem::resize_file(a.Path() / "oversized", 24 * 1048576 + 1);
        Reject([&] { ReadTerminalFile(a.Path() / "oversized", cancelled); });
        a.Write("blank", " \n\t");
        Reject([&] { ReadTerminalFile(a.Path() / "blank", cancelled); });
        std::filesystem::create_symlink(a.Path() / "public.txt", a.Path() / "link");
        Reject([&] { ReadTerminalFile(a.Path() / "link", cancelled); });
        mkfifo((a.Path() / "fifo").c_str(), 0600);
        Reject([&] { ReadTerminalFile(a.Path() / "fifo", cancelled); });
        for (int i = 0; i < 80; ++i)
            a.Write("file-" + std::to_string(i), "public");
        auto files = TerminalFiles(a.Path(), cancelled);
        Check(files.size() == 60, "file index unbounded");
        Check(std::find(files.begin(), files.end(), a.Path() / "file-2") <
                  std::find(files.begin(), files.end(), a.Path() / "file-10"),
              "numeric filenames sorted lexically");
        for (const auto& file : files)
            Check(file.filename() != "link" && file.filename() != "fifo", "unsafe index entry");
        cancelled = true;
        Reject([&] { ReadTerminalFile(picture, cancelled); });
        cancelled = false;
        stage = "atomic receipt";
        const auto session = rimes::capsule::NewUuidV4();
        Store rejected(root / "rejected", {}, [](const auto&) {});
        Reject([&] {
            rejected.ImportTerminalResult(session, "Shell · public", "public.png", png.body,
                                          png.png);
        });
        Check(std::filesystem::is_empty(root / "rejected/images") &&
                  rejected.Read().document.at("threads").empty(),
              "cancel left an attachment or row");
        Store store(profile.Path() / "mailbox");
        auto receipt =
            store.ImportTerminalResult(session, "Shell · public", "public.png", png.body, png.png);
        Check(store.Get(receipt.at("id")).at("terminalSessionID") == session,
              "session identity not persisted");
        Check(store.Image(receipt.at("messages").at(0).at("imageFileName")) == png.png,
              "attachment differs");
        int leader = 0, child = 0;
        {
            Workspace workspace;
            workspace.Protect(false);
            Wait([&] { return workspace.Inspect().at("ready") == true; });
            Check(workspace.Open("terminal"), "terminal did not open");
            auto& terminal = workspace.Terminal();
            stage = "adaptive empty terminal";
            gtk_window_resize(GTK_WINDOW(workspace.Window()), 680, 500);
            Wait([&] {
                return gtk_widget_get_allocated_width(workspace.Content()) == 680 &&
                       gtk_widget_get_allocated_height(workspace.Content()) == 500;
            });
            stage = "actual PTY input and independent directories";
            const auto first =
                terminal.Launch("Shell", a.Path(), "/usr/bin/python3", {fixture.string()});
            auto* view = Find(terminal.Widget());
            Check(view, "PTY widget absent");
            stage = "PTY ready in compact window";
            Wait(
                [&] { return Text(view).find("READY:" + a.Path().string()) != std::string::npos; });
            Check(gtk_widget_get_mapped(view), "terminal hidden");
            stage = "PTY UTF-8 and cwd";
            Input(view, "公开🙂\rwhere\r");
            Wait([&] {
                return Text(view).find("ECHO:公开🙂") != std::string::npos &&
                       Text(view).find("CWD:" + a.Path().string()) != std::string::npos;
            });
            const auto original_columns = vte_terminal_get_column_count(VTE_TERMINAL(view));
            stage = "PTY grow window";
            gtk_window_resize(GTK_WINDOW(workspace.Window()), 1200, 800);
            Pump(150);
            Input(view, "size\r");
            Wait([&] {
                return vte_terminal_get_column_count(VTE_TERMINAL(view)) != original_columns;
            });
            const auto cols = vte_terminal_get_column_count(VTE_TERMINAL(view)),
                       rows = vte_terminal_get_row_count(VTE_TERMINAL(view));
            stage = "PTY SIGWINCH size";
            Wait([&] {
                return Text(view).find("SIZE:" + std::to_string(cols) + ":" +
                                       std::to_string(rows)) != std::string::npos;
            });
            stage = "adaptive compact terminal";
            gtk_window_resize(GTK_WINDOW(workspace.Window()), 680, 500);
            Wait([&] {
                return gtk_widget_get_allocated_width(workspace.Content()) == 680 &&
                       gtk_widget_get_allocated_width(view) >= 580 &&
                       !terminal.Inspect().at("listVisible").get<bool>() &&
                       !terminal.Inspect().at("inspectorVisible").get<bool>();
            });
            stage = "PTY keyboard focus after resize";
            gtk_widget_grab_focus(view);
            // Allocation can settle before X11 has delivered the focus events.
            // Do not send real keys until the terminal owns the active window.
            Wait([&] {
                return gtk_window_is_active(GTK_WINDOW(workspace.Window())) &&
                       gtk_window_get_focus(GTK_WINDOW(workspace.Window())) == view &&
                       gtk_widget_has_focus(view);
            });
            stage = "PTY keyboard Escape/Ctrl+W/Ctrl+C";
            // Actual GTK key events must reach the terminal, including window-shortcut keys.
            const char* keys[] = {"xdotool", "key", "--clearmodifiers", "Escape", "ctrl+w",
                                  "ctrl+c",  "a",   "Return",           nullptr};
            Check(g_spawn_sync(nullptr, const_cast<char**>(keys), nullptr, G_SPAWN_SEARCH_PATH,
                               nullptr, nullptr, nullptr, nullptr, nullptr, nullptr),
                  "key injection unavailable");
            std::string observed_keys;
            try {
                Wait([&] {
                    const auto text = Text(view);
                    observed_keys.clear();
                    for (std::size_t offset = 0; (offset = text.find("KEY:", offset)) != text.npos;) {
                        offset += 4;
                        const auto end = text.find_first_not_of("0123456789abcdef", offset);
                        observed_keys += text.substr(offset, end - offset);
                        offset = end;
                        if (offset == text.npos)
                            break;
                    }
                    return observed_keys.find("1b1703610d") != observed_keys.npos;
                });
            } catch (...) {
                // This process uses only the isolated public fixture, never user sessions.
                std::cerr << "PTY keyboard diagnostics: "
                          << Json{{"observedKeys", observed_keys},
                                  {"windowActive", bool(gtk_window_is_active(GTK_WINDOW(workspace.Window())))},
                                  {"terminalFocused", bool(gtk_widget_has_focus(view))},
                                  {"windowVisible", bool(gtk_widget_get_visible(workspace.Window()))},
                                  {"fixtureScreen", Text(view)}}.dump()
                          << '\n';
                throw;
            }
            Check(gtk_widget_get_visible(workspace.Window()), "PTY Escape/Ctrl+W closed window");
            stage = "explicit terminal copy and paste";
            auto* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
            vte_terminal_select_all(VTE_TERMINAL(view));
            const char* copy_keys[] = {"xdotool", "key", "--clearmodifiers", "ctrl+shift+c",
                                       nullptr};
            Check(g_spawn_sync(nullptr, const_cast<char**>(copy_keys), nullptr, G_SPAWN_SEARCH_PATH,
                               nullptr, nullptr, nullptr, nullptr, nullptr, nullptr),
                  "copy key injection unavailable");
            Pump(50);
            auto* copied = gtk_clipboard_wait_for_text(clipboard);
            const std::string selection = copied ? copied : "";
            g_free(copied);
            Check(selection.find("ECHO:公开🙂") != selection.npos, "terminal selection not copied");
            vte_terminal_unselect_all(VTE_TERMINAL(view));
            gtk_clipboard_set_text(clipboard, "公开粘贴\n", -1);
            const char* paste_keys[] = {"xdotool", "key", "--clearmodifiers", "ctrl+shift+v",
                                        nullptr};
            Check(g_spawn_sync(nullptr, const_cast<char**>(paste_keys), nullptr,
                               G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, nullptr, nullptr,
                               nullptr),
                  "paste key injection unavailable");
            Wait([&] { return Text(view).find("ECHO:公开粘贴") != std::string::npos; });
            const auto second =
                terminal.Launch("Codex", b.Path(), "/usr/bin/python3", {fixture.string()});
            auto* second_view = Find(terminal.Widget());
            Wait([&] {
                return Text(second_view).find("READY:" + b.Path().string()) != std::string::npos;
            });
            Check(second != first && terminal.Inspect().at("sessions").size() == 2,
                  "session identity shared");
            stage = "navigation, close and protection preserve processes";
            workspace.Open("inbox");
            workspace.Hide();
            Pump(100);
            const auto hidden = terminal.Inspect();
            for (const auto& state : hidden.at("sessions"))
                Check(state.at("running"), "hidden process stopped");
            workspace.Protect(true);
            Check(!vte_terminal_get_input_enabled(VTE_TERMINAL(second_view)),
                  "protected terminal accepts input");
            Check(!terminal.Select(first), "protected session changed");
            workspace.Protect(false);
            workspace.Open("terminal");
            Check(terminal.Select(first), "original session lost");
            view = Find(terminal.Widget());
            Input(view, "where\r");
            Wait([&] { return Text(view).find("CWD:" + a.Path().string()) != std::string::npos; });
            stage = "UUID-scoped preview and manual collection";
            terminal.Preview(a.Path() / "public.txt");
            terminal.Select(second);
            Pump(100);
            Check(terminal.Inspect().at("preview") == "", "stale preview crossed session");
            terminal.Preview(a.Path() / "public.txt");
            Wait([&] { return !terminal.Inspect().at("loading").get<bool>(); });
            Check(terminal.Inspect().at("previewSession") == second, "preview association wrong");
            // Explicit collection persists the exact bytes that the user previewed.
            a.Write("public.txt", "changed after preview");
            terminal.Collect();
            terminal.Collect();
            Wait([&] { return !terminal.Inspect().at("collecting").get<bool>(); });
            const auto document = store.Read().document;
            Check(document.at("threads").size() == 2, "double click duplicated collection");
            const auto collected = document.at("threads").back();
            Check(collected.at("terminalSessionID") == second &&
                      collected.at("messages").at(0).at("body") == text.body,
                  "preview bytes or UUID lost");
            terminal.Preview(picture);
            terminal.Suspend();
            Pump(100);
            Check(terminal.Inspect().at("preview") == "", "cancelled preview published");
            stage = "exit status and process group termination";
            Input(second_view, "quit7\r");
            Wait([&] { return terminal.Inspect().at("sessions").at(0).at("exitCode") == 7; });
            terminal.Select(first);
            view = Find(terminal.Widget());
            Input(view, "fork\r");
            Wait([&] { return std::filesystem::exists(a.Path() / "child.pid"); });
            std::ifstream(a.Path() / "child.pid") >> child;
            leader = terminal.Inspect().at("sessions").at(1).at("pid");
            Check(Alive(child) && Alive(leader), "test process not running");
            Click(Find(terminal.Widget(), "terminal-stop"));
            Wait([&] { return !Alive(child) && !Alive(leader); });
            stage = "host shutdown owns live descendants";
            terminal.Launch("Claude Code", b.Path(), "/usr/bin/python3", {fixture.string()});
            auto* last = Find(terminal.Widget());
            Wait([&] { return Text(last).find("READY:") != std::string::npos; });
            Input(last, "fork\r");
            Wait([&] { return std::filesystem::exists(b.Path() / "child.pid"); });
            std::ifstream(b.Path() / "child.pid") >> child;
            leader = terminal.Inspect().at("sessions").at(0).at("pid");
        }
        Wait([&] { return !Alive(leader) && !Alive(child); });
        // Destruction before spawn completion must also cancel the owned child.
        auto* pending_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        {
            TerminalSession pending("Shell", b.Path(), [] {});
            gtk_container_add(GTK_CONTAINER(pending_window), pending.Widget());
            gtk_widget_show_all(pending_window);
            pending.Start("/usr/bin/python3", {fixture.string()});
        }
        Pump(150);
        gtk_widget_destroy(pending_window);
        Pump(150);
        stage = "restart is empty, receipts survive";
        {
            Workspace workspace;
            workspace.Protect(false);
            Wait([&] { return workspace.Inspect().at("ready") == true; });
            Check(workspace.Terminal().Inspect().at("sessions").empty(), "restored fake live PTY");
            Check(store.Read().document.at("threads").size() == 2, "restart lost receipts");
        }
        if (const auto* destination = getenv("RIMES_TERMINAL_EXPORT")) {
            PrivateDirectory output(std::filesystem::path(destination) / "mailbox");
            const auto document = store.Read().document;
            output.Write("mailbox.json", document.dump());
            Json images = Json::object();
            for (const auto& row : document.at("threads"))
                for (const auto& message : row.at("messages")) {
                    if (!message.contains("imageFileName"))
                        continue;
                    const std::string name = message.at("imageFileName");
                    const auto bytes = *store.Image(name);
                    PrivateDirectory(output.Path() / "images").Write(name, bytes);
                    images[name] = rimes::runtime::Sha256(bytes);
                }
            PrivateDirectory(destination)
                .Write("linux-public-fixture.json", Json{{"purpose", "public-parity-fixture"},
                                                         {"threads", document.at("threads")},
                                                         {"images", images}}
                                                        .dump(2));
        }
        std::filesystem::remove_all(root);
        std::cout << "Terminal PTY, UTF-8/keyboard/resize, lifecycle, process groups, UUID file "
                     "collection, cancellation and persistence passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << stage << ": " << error.what() << "\n";
        std::filesystem::remove_all(root);
        return 1;
    }
}
