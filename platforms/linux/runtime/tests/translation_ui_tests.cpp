#include "translation_resources.hpp"
#include "desktop.hpp"
#include <filesystem>
#include <iostream>
#include <string>

namespace {
bool HasLabel(GtkWidget* widget, const std::string& text) {
    if (GTK_IS_LABEL(widget) && gtk_label_get_text(GTK_LABEL(widget)) == text) return true;
    if (!GTK_IS_CONTAINER(widget)) return false;
    auto* children = gtk_container_get_children(GTK_CONTAINER(widget));
    bool found = false;
    for (auto* item = children; item; item = item->next) found |= HasLabel(GTK_WIDGET(item->data), text);
    g_list_free(children);
    return found;
}
struct Probe { bool passed = false; unsigned ticks = 0; };
}
int main(int argc, char** argv) {
    if (!gtk_init_check(&argc, &argv)) return 1;
    char pattern[] = "/tmp/xsime-translation-ui-XXXXXX";
    const auto* root = mkdtemp(pattern);
    if (!root) return 1;
    setenv("RIMES_USER_DIR", root, 1);
    rimes::desktop::Theme("night");
    Probe probe;
    const auto source = g_timeout_add(20, +[](gpointer data) -> gboolean {
        auto& probe = *static_cast<Probe*>(data);
        ++probe.ticks;
        auto* windows = gtk_window_list_toplevels();
        for (auto* item = windows; item; item = item->next) {
            auto* widget = GTK_WIDGET(item->data);
            if (!GTK_IS_DIALOG(widget)) continue;
            const bool ready = HasLabel(widget, "尚未安装语言资源。安装后可离线翻译。");
            if (ready || probe.ticks >= 250) {
                probe.passed = ready && HasLabel(widget, "安装 / 修复") && HasLabel(widget, "重新检查");
                gtk_dialog_response(GTK_DIALOG(widget), GTK_RESPONSE_CLOSE);
            }
        }
        g_list_free(windows);
        return G_SOURCE_CONTINUE;
    }, &probe);
    rimes::desktop::TranslationResources(nullptr);
    g_source_remove(source);
    std::filesystem::remove_all(root);
    std::cout << (probe.passed ? "Translation dialog reads actual resource status and exposes repair controls\n" : "Translation dialog status failed\n");
    return probe.passed ? 0 : 1;
}
