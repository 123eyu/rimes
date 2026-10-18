#include "configuration.hpp"
#include "mailbox_workspace.hpp"
#include "official_catalog.hpp"
#include <iostream>
#include <thread>
using namespace rimes::mailbox;
using namespace rimes::runtime;
using namespace std::chrono_literals;
namespace {
std::string stage;
void Check(bool ok, const char* why) {
    if (!ok)
        throw std::runtime_error(stage + ": " + why);
}
void Wait(const std::function<bool()>& condition) {
    const auto end = std::chrono::steady_clock::now() + 8s;
    do {
        while (gtk_events_pending())
            gtk_main_iteration();
        try {
            if (condition())
                return;
        } catch (const StorageBusy&) {
        }
        std::this_thread::sleep_for(5ms);
    } while (std::chrono::steady_clock::now() < end);
    throw std::runtime_error(stage + ": UI deadline");
}
GtkWidget* Find(GtkWidget* root, const std::string& name) {
    if (name == gtk_widget_get_name(root))
        return root;
    if (!GTK_IS_CONTAINER(root))
        return nullptr;
    auto* children = gtk_container_get_children(GTK_CONTAINER(root));
    GtkWidget* result = nullptr;
    for (auto* child = children; child && !result; child = child->next)
        result = Find(GTK_WIDGET(child->data), name);
    g_list_free(children);
    return result;
}
void Click(GtkWidget* widget) {
    Check(widget && GTK_IS_BUTTON(widget) && gtk_widget_is_sensitive(widget), "button unavailable");
    gtk_button_clicked(GTK_BUTTON(widget));
}
void Write(GtkWidget* widget, const std::string& text) {
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget)), text.c_str(), -1);
}
std::string Text(GtkWidget* widget) {
    auto* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));
    GtkTextIter start{}, end{};
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    auto* raw = gtk_text_buffer_get_text(buffer, &start, &end, false);
    std::string value = raw;
    g_free(raw);
    return value;
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    const auto root = std::filesystem::temp_directory_path() / ("xsime-chat-ui-" + RandomID());
    PrivateDirectory home(root / "home");
    setenv("RIMES_USER_DIR", root.c_str(), 1);
    setenv("HOME", home.Path().c_str(), 1);
    setenv("RIMES_CLAUDE_PATH", std::filesystem::absolute(argv[1]).c_str(), 1);
    home.Write("fixture-mode", "chat-text-hold");
    Store store(root / "mailbox");
    try {
        gtk_init(&argc, &argv);
        {
            Workspace host;
            host.Protect(false);
            stage = "new conversation";
            Wait([&] { return host.Inspect().at("ready") == true; });
            Check(host.Open("chat"), "chat did not open");
            auto* input = Find(host.Content(), "mailbox-composer-input");
            auto* picker = Find(host.Content(), "mailbox-model-picker");
            Check(gtk_widget_get_visible(picker), "new chat has no model picker");
            Check(gtk_combo_box_set_active_id(GTK_COMBO_BOX(picker), "claudeCodeCLI"),
                  "Claude option absent");
            Write(input, "公开界面对话");
            Click(Find(host.Content(), "mailbox-send"));
            Wait([&] {
                return !host.Inspect().at("thread").get<std::string>().empty() &&
                       Find(host.Content(), "mailbox-chat-preview");
            });
            const std::string id = host.Inspect().at("thread");
            Wait([&] {
                return std::string(gtk_label_get_text(
                           GTK_LABEL(Find(host.Content(), "mailbox-chat-preview")))) == "公开🙂。";
            });
            Check(Text(input).empty() && !gtk_widget_get_visible(picker),
                  "admitted draft or model picker retained");
            Check(!gtk_widget_is_sensitive(Find(host.Content(), "mailbox-send")),
                  "concurrent turn allowed");
            Check(store.Get(id).at("messages").size() == 1, "UI stream persisted early");
            Write(input, "下一条独立草稿");
            stage = "navigate and hide during streaming";
            Click(Find(host.Content(), "mailbox-nav-inbox"));
            host.Hide();
            home.Write("release-text", "release");
            Wait([&] { return store.Get(id).at("generation").at("phase") == "succeeded"; });
            Wait([&] {
                return host.Inspect().at("busy") == false &&
                       host.Inspect().at("revision") == store.Read().revision;
            });
            Check(host.Open("chat", id), "completed conversation did not reopen");
            Check(Text(input) == "下一条独立草稿", "navigation lost next-message draft");
            Check(store.Get(id).at("messages").back().at("body") == "公开🙂。\n\n第二句。",
                  "final reply changed");
            Wait([&] {
                return host.Inspect().at("busy") == false &&
                       host.Inspect().at("revision") == store.Read().revision &&
                       store.Get(id).at("unread") == false;
            });
            stage = "cancel and retry";
            home.Remove("release-text");
            home.Remove("text-started");
            Click(Find(host.Content(), "mailbox-send"));
            Wait([&] {
                const auto* error =
                    gtk_label_get_text(GTK_LABEL(Find(host.Content(), "mailbox-feedback")));
                if (*error && std::string(error) != "已保存")
                    throw std::runtime_error(stage + ": " + error);
                auto* preview = Find(host.Content(), "mailbox-chat-preview");
                return preview && std::string(gtk_label_get_text(GTK_LABEL(preview))) == "公开🙂。";
            });
            stage = "cancel button";
            Click(Find(host.Content(), "mailbox-chat-cancel"));
            Wait([&] {
                auto* retry = Find(host.Content(), "mailbox-chat-retry");
                return retry && gtk_widget_is_sensitive(retry);
            });
            Check(store.Get(id).at("messages").size() == 3, "cancel persisted response");
            stage = "retry button";
            home.Write("fixture-mode", "chat-text");
            Click(Find(host.Content(), "mailbox-chat-retry"));
            stage = "retry completion";
            Wait([&] { return store.Get(id).at("messages").size() == 4; });
            Wait([&] {
                return host.Inspect().at("busy") == false &&
                       host.Inspect().at("revision") == store.Read().revision;
            });
            Check(Text(input).empty(), "retry restored consumed draft");
            stage = "protected session cancels";
            home.Write("fixture-mode", "chat-text-hold");
            Write(input, "公开保护状态测试");
            Click(Find(host.Content(), "mailbox-send"));
            Wait([&] {
                const auto* error =
                    gtk_label_get_text(GTK_LABEL(Find(host.Content(), "mailbox-feedback")));
                if (*error && std::string(error) != "已保存")
                    throw std::runtime_error(stage + ": " + error);
                auto* preview = Find(host.Content(), "mailbox-chat-preview");
                return preview && std::string(gtk_label_get_text(GTK_LABEL(preview))) == "公开🙂。";
            });
            host.Protect(true);
            Wait([&] { return store.Get(id).at("generation").at("phase") == "failed"; });
            Check(!gtk_widget_get_visible(host.Window()) &&
                      store.Get(id).at("messages").size() == 5,
                  "protected reply exposed");
            host.Protect(false);
            host.Refresh();
            Wait([&] {
                return host.Inspect().at("busy") == false &&
                       host.Inspect().at("revision") == store.Read().revision;
            });
            Check(host.Open("chat", id), "unlock did not reopen");
            stage = "unavailable channel preserves draft";
            Click(Find(host.Content(), "mailbox-new-chat"));
            home.Write("fixture-mode", "chat-logged-out");
            Write(input, "公开未连接草稿");
            const auto count = store.Read().document.at("threads").size();
            Click(Find(host.Content(), "mailbox-send"));
            Wait([&] {
                return *gtk_label_get_text(GTK_LABEL(Find(host.Content(), "mailbox-feedback"))) !=
                       '\0';
            });
            Check(Text(input) == "公开未连接草稿" &&
                      store.Read().document.at("threads").size() == count,
                  "failed admission consumed draft");
        }
        std::filesystem::remove_all(root);
        std::cout << "Mailbox chat widgets: model selection, live preview, draft preservation, "
                     "hide/navigation, final reply, cancel/retry, protection and unavailable "
                     "channel passed\n";
    } catch (...) {
        const auto failed = store.Read();
        for (const auto& row : failed.document.at("threads"))
            std::cerr << "public fixture: " << row.at("generation").dump()
                      << " messages=" << row.at("messages").size() << "\n";
        std::filesystem::remove_all(root);
        throw;
    }
}
