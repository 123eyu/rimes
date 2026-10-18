#include "practice_pane.hpp"
#include <fstream>
#include <iostream>
using namespace rimes::desktop;
GtkWidget *Find(GtkWidget *root, const char *title) {
    const auto *name = atk_object_get_name(gtk_widget_get_accessible(root));
    if (name && std::string(name) == title)
        return root;
    if (!GTK_IS_CONTAINER(root))
        return nullptr;
    auto *children = gtk_container_get_children(GTK_CONTAINER(root));
    GtkWidget *result = nullptr;
    for (auto *c = children; c && !result; c = c->next)
        result = Find(GTK_WIDGET(c->data), title);
    g_list_free(children);
    return result;
}
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    g_set_prgname("rimes-settings");
    ConfigurePracticeInput();
    gtk_init(&argc, &argv);
    Theme("night");
    auto *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "XSIME Practice Desktop Probe");
    gtk_window_set_default_size(GTK_WINDOW(window), 698, 831);
    const std::filesystem::path root(argv[1]);
    PracticePane pane(GTK_WINDOW(window), "overview", "night", false);
    gtk_container_add(GTK_CONTAINER(window), pane.Widget());
    gtk_widget_show_all(window);
    struct Probe {
        PracticePane &pane;
        std::filesystem::path root;
    } probe{pane, root};
    g_timeout_add(
        50,
        +[](gpointer p) -> gboolean {
            auto &probe = *static_cast<Probe *>(p);
            const auto command_path = probe.root / "command";
            if (std::filesystem::exists(command_path)) {
                std::ifstream input(command_path);
                std::string command;
                std::getline(input, command);
                std::filesystem::remove(command_path);
                if (command == "quit") {
                    gtk_main_quit();
                    return G_SOURCE_REMOVE;
                }
                auto *button =
                    Find(probe.pane.Widget(), command == "start" ? "开始或重新跟打" : "结束跟打");
                if (!button)
                    throw std::runtime_error("desktop probe button missing");
                gtk_button_clicked(GTK_BUTTON(button));
            }
            auto snapshot = probe.pane.Snapshot();
            snapshot["text"] = probe.pane.Editor().Text();
            std::ofstream(probe.root / "state.tmp") << snapshot.dump();
            std::filesystem::rename(probe.root / "state.tmp", probe.root / "state.json");
            return G_SOURCE_CONTINUE;
        },
        &probe);
    gtk_main();
    pane.CanLeave();
    gtk_widget_destroy(window);
    return 0;
}
