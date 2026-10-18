#include "desktop.hpp"
#include "mailbox_workspace.hpp"
#include "terminal_files.hpp"
#include "terminal_fixtures.hpp"
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <thread>
namespace rimes::mailbox {
namespace {
void Pump(int milliseconds) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    do {
        while (gtk_events_pending())
            gtk_main_iteration();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < end);
}
void Wait(const std::function<bool()>& condition) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        Pump(50);
        if (condition())
            return;
    }
    throw std::runtime_error("Terminal fixture timeout");
}
GtkWidget* Terminal(GtkWidget* view) {
    if (VTE_IS_TERMINAL(view))
        return view;
    if (!GTK_IS_CONTAINER(view))
        return nullptr;
    auto* children = gtk_container_get_children(GTK_CONTAINER(view));
    GtkWidget* result = nullptr;
    for (auto* child = children; child && !result; child = child->next)
        result = Terminal(GTK_WIDGET(child->data));
    g_list_free(children);
    return result;
}
} // namespace
int RenderTerminalFixture(const std::filesystem::path& output) {
    const auto& contract = TerminalContract();
    const std::filesystem::path directory = contract.at("directory").get<std::string>();
    // The compiled, native-exported command contains only public fixture text.
    // Never execute a command supplied through an arbitrary input JSON file.
    if (!std::filesystem::create_directory(directory))
        throw std::runtime_error("Public terminal fixture directory already exists");
    chmod(directory.c_str(), 0700);
    char profile[] = "/tmp/xsime-terminal-render-XXXXXX";
    if (!mkdtemp(profile)) {
        std::filesystem::remove(directory);
        throw std::runtime_error("Cannot create fixture profile");
    }
    try {
        setenv("RIMES_USER_DIR", profile, 1);
        runtime::PrivateDirectory(directory).Write(contract.at("fileName").get<std::string>(),
                                                   contract.at("fileBody").get<std::string>());
        gtk_init(nullptr, nullptr);
        g_object_set(gtk_settings_get_default(), "gtk-enable-animations", FALSE, nullptr);
        std::filesystem::create_directories(output);
        Json records = Json::array();
        for (const auto* theme : {"night", "day"}) {
            const auto user = std::filesystem::path(profile) / theme;
            setenv("RIMES_USER_DIR", user.c_str(), 1);
            Store store;
            Workspace host(nullptr, true);
            host.Fixture(store.Read(), theme, "terminal", "");
            gtk_widget_show(host.Window());
            auto& terminal = host.Terminal();
            auto render = [&](const std::string& scene) {
                for (bool compact : {false, true}) {
                    const int width = compact ? 680 : 1000, height = compact ? 500 : 700;
                    gtk_widget_set_size_request(host.Window(), width, height);
                    gtk_window_set_default_size(GTK_WINDOW(host.Window()), width, height);
                    gtk_window_resize(GTK_WINDOW(host.Window()), width, height);
                    Pump(180);
                    const auto filename = std::string(theme) + "-terminal-" + scene +
                                          (compact ? "-compact.png" : "-wide.png");
                    if (!desktop::Render(host.Content(), (output / filename).string(), 2))
                        throw std::runtime_error("Cannot capture terminal GTK widget");
                    auto state = terminal.Inspect();
                    state["image"] = filename;
                    state["logicalWidth"] = gtk_widget_get_allocated_width(host.Content());
                    state["logicalHeight"] = gtk_widget_get_allocated_height(host.Content());
                    records.push_back(state);
                }
            };
            render("empty");
            gtk_widget_set_size_request(host.Window(), 1000, 700);
            gtk_window_resize(GTK_WINDOW(host.Window()), 1000, 700);
            Pump(150);
            terminal.Launch("Shell", directory, "/bin/sh", {"-c", contract.at("command")});
            auto* view = Terminal(terminal.Widget());
            Wait([&] { return terminal.Inspect().at("sessions").at(0).at("title") == "公开终端"; });
            render("running");
            terminal.Preview(directory / contract.at("fileName").get<std::string>());
            Wait([&] { return terminal.Inspect().at("loading") == false; });
            if (terminal.Inspect().at("preview") != contract.at("fileName"))
                throw std::runtime_error("Terminal file preview failed");
            render("file");
            terminal.Collect();
            Wait([&] { return terminal.Inspect().at("collecting") == false; });
            if (store.Read().document.at("threads").size() != 1)
                throw std::runtime_error("Terminal receipt failed");
            host.Fixture(store.Read(), theme, "terminal", "");
            render("receipt");
            const std::string input = "完成\n";
            vte_terminal_feed_child(VTE_TERMINAL(view), input.c_str(), input.size());
            Wait([&] { return terminal.Inspect().at("sessions").at(0).at("exitCode") == 7; });
            render("exited");
        }
        std::ofstream(output / "renders.json") << records.dump(2) << "\n";
        std::ofstream(output / "native-terminal.json") << contract.dump(2) << "\n";
    } catch (const std::exception& error) {
        std::cerr << "Public terminal fixture: " << error.what() << "\n";
        std::filesystem::remove_all(profile);
        std::filesystem::remove_all(directory);
        throw;
    }
    std::filesystem::remove_all(profile);
    std::filesystem::remove_all(directory);
    return 0;
}
} // namespace rimes::mailbox
