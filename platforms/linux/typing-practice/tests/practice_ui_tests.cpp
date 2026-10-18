#include "practice_pane.hpp"
#include "practice_store.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>

using namespace rimes::desktop;
namespace pt = rimes::practice;
void Check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
Json Read(const std::filesystem::path &path) {
    std::ifstream in(path);
    Json value;
    in >> value;
    return value;
}
void Settle() {
    while (gtk_events_pending())
        gtk_main_iteration();
}
void Wait(const std::function<bool()> &ready) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    do {
        Settle();
        if (ready())
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < until);
    throw std::runtime_error("practice UI asynchronous operation timed out");
}
GtkWidget *Find(GtkWidget *widget, const std::string &title) {
    const auto *name = atk_object_get_name(gtk_widget_get_accessible(widget));
    if (name && name == title)
        return widget;
    if (!GTK_IS_CONTAINER(widget))
        return nullptr;
    auto *children = gtk_container_get_children(GTK_CONTAINER(widget));
    GtkWidget *found = nullptr;
    for (auto *child = children; child && !found; child = child->next)
        found = Find(GTK_WIDGET(child->data), title);
    g_list_free(children);
    return found;
}
void Click(PracticePane &pane, const char *title) {
    auto *button = Find(pane.Widget(), title);
    Check(button, std::string("missing control: ") + title);
    gtk_button_clicked(GTK_BUTTON(button));
    Settle();
}
void Key(PracticeEditor &editor, guint time, guint key, guint16 code, bool release = false,
         guint state = 0) {
    GdkEventKey event{};
    event.type = release ? GDK_KEY_RELEASE : GDK_KEY_PRESS;
    event.time = time;
    event.keyval = key;
    event.hardware_keycode = code;
    event.state = state;
    gboolean handled = FALSE;
    g_signal_emit_by_name(editor.Widget(), release ? "key-release-event" : "key-press-event",
                          &event, &handled);
}
void Insert(PracticeEditor &editor, const std::string &text) {
    auto *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor.Widget()));
    gtk_text_buffer_begin_user_action(buffer);
    gtk_text_buffer_insert_at_cursor(buffer, text.c_str(), text.size());
    gtk_text_buffer_end_user_action(buffer);
}
void Reply(int response) {
    g_idle_add(
        +[](gpointer p) -> gboolean {
            auto *windows = gtk_window_list_toplevels();
            bool found = false;
            for (auto *item = windows; item; item = item->next)
                if (GTK_IS_MESSAGE_DIALOG(item->data)) {
                    gtk_dialog_response(GTK_DIALOG(item->data), GPOINTER_TO_INT(p));
                    found = true;
                }
            g_list_free(windows);
            return found ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
        },
        GINT_TO_POINTER(response));
}
int main(int argc, char **argv) {
    char temporary[] = "/tmp/xsime-practice-ui-XXXXXX";
    if (!mkdtemp(temporary))
        return 1;
    setenv("RIMES_USER_DIR", temporary, 1);
    setenv("NO_AT_BRIDGE", "1", 1);
    setenv("TZ", "Asia/Tokyo", 1);
    tzset();
    const std::filesystem::path root = temporary;
    try {
        gtk_init(&argc, &argv);
        g_object_set(gtk_settings_get_default(), "gtk-enable-animations", FALSE, nullptr);
        const auto output = argc == 2 ? std::filesystem::path(argv[1]) : std::filesystem::path();
        if (!output.empty())
            std::filesystem::create_directories(output);
        for (const auto *theme : {"night", "day", "quiet", "rasta"})
            for (const auto *language : {"chinese", "english"})
                for (const auto *phase : {"ready", "live", "complete", "history"}) {
                    Theme(theme);
                    const auto fixture = Read(std::filesystem::path(PRACTICE_DASHBOARD) /
                                              (std::string(language) + "-" + phase + ".json"));
                    auto *window = Class(gtk_offscreen_window_new(), "practice-surface");
                    gtk_window_set_default_size(GTK_WINDOW(window), 698, 1);
                    {
                        PracticePane pane(GTK_WINDOW(window),
                                          std::string(phase) == "history" ? "history" : "overview",
                                          theme, true, fixture);
                        gtk_container_add(GTK_CONTAINER(window), pane.Widget());
                        gtk_widget_show_all(window);
                        Settle();
                        const auto frame = pane.Geometry();
                        const auto name =
                            std::string(language) + "-" + phase + "-" + theme + ".png";
                        if (!output.empty()) {
                            Check(Render(window, (output / name).string()), "render failed");
                            std::ofstream(output / (name + ".geometry.json"))
                                << frame.dump(2) << '\n';
                        }
                        Check(frame.at("page").at("width") == 698 &&
                                  frame.at("page").at("height") == fixture.at("height"),
                              "native practice page differs: " + name + " " +
                                  frame.at("page").dump());
                        Check(frame.at("card-0").at("width") == 154 &&
                                  frame.at("card-0").at("height") == 92 &&
                                  frame.at("trend").at("height") == 165,
                              "native practice metric bounds differ");
                        if (std::string(phase) != "history")
                            Check(frame.at("target").at("height") == 189 &&
                                      frame.at("editor").at("height") == 139,
                                  "native editor bounds differ");
                    }
                    gtk_widget_destroy(window);
                }
        std::cout << "PASS: 32 native article-practice/history renders and measured bounds\n";
        Theme("night");
        auto *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        double clock = 10;
        const Json context = {{"schemaID", "rime_ice"},
                              {"inputSourceID", "public.source"},
                              {"chordCountingAvailable", false}};
        const auto article = Read(std::filesystem::path(PRACTICE_DASHBOARD) / "chinese-ready.json");
        const auto contract = Read(PRACTICE_CONTRACT);
        const std::string target = contract.at("articles").front().at("text");
        {
            PracticePane pane(
                GTK_WINDOW(window), "overview", "night", false, {}, [&] { return clock; },
                [&] { return context; });
            gtk_container_add(GTK_CONTAINER(window), pane.Widget());
            gtk_widget_show_all(window);
            Settle();
            auto &editor = pane.Editor();
            // Exercise the actual GTK signal handlers, without posting keys to any other app.
            for (const auto *event : {"key-press-event", "key-release-event"})
                g_signal_connect(editor.Widget(), event,
                                 G_CALLBACK(+[](GtkWidget *, GdkEventKey *, gpointer) -> gboolean {
                                     return TRUE;
                                 }),
                                 nullptr);
            Click(pane, "开始或重新跟打");
            Check(pane.Snapshot().at("armed"), "start did not arm");
            g_signal_emit_by_name(editor.Widget(), "preedit-changed", "tian");
            clock = 20;
            Key(editor, 100, GDK_KEY_t, 28);
            Key(editor, 100, GDK_KEY_t, 28);
            Key(editor, 101, GDK_KEY_t, 28);
            Key(editor, 102, GDK_KEY_t, 28, true);
            Key(editor, 103, GDK_KEY_BackSpace, 22);
            auto metrics = pane.Snapshot().at("snapshot").at("metrics");
            Check(metrics.at("physicalKeyCount") == 2 && metrics.at("repeatKeyCount") == 1 &&
                      metrics.at("compositionBackspaceCount") == 1 &&
                      metrics.at("attemptedCharacterCount") == 0 &&
                      metrics.at("elapsedSeconds") == 10,
                  "preedit/key/repeat dedup differs");
            g_signal_emit_by_name(editor.Widget(), "preedit-changed", "");
            Insert(editor, "天");
            Check(pane.Snapshot().at("snapshot").at("metrics").at("correctCharacterCount") == 1,
                  "committed character not scored");
            auto *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(editor.Widget()));
            gtk_text_buffer_begin_user_action(buffer);
            GtkTextIter a, b;
            gtk_text_buffer_get_bounds(buffer, &a, &b);
            gtk_text_buffer_delete(buffer, &a, &b);
            gtk_text_buffer_insert_at_cursor(buffer, "天", -1);
            gtk_text_buffer_end_user_action(buffer);
            Check(pane.Snapshot().at("snapshot").at("metrics").at("attemptedCharacterCount") == 2,
                  "identical replacement lost its new attempt");
            gtk_text_buffer_begin_user_action(buffer);
            gtk_text_buffer_get_bounds(buffer, &a, &b);
            gtk_text_buffer_delete(buffer, &a, &b);
            const std::string oversized(pt::kMaximumCharacters + 1, 'a');
            gtk_text_buffer_insert_at_cursor(buffer, oversized.c_str(), oversized.size());
            gtk_text_buffer_end_user_action(buffer);
            Check(editor.Text() == "天" &&
                      pane.Snapshot().at("snapshot").at("metrics").at("attemptedCharacterCount") ==
                          2,
                  "rejected replacement changed editor or score");
            g_signal_emit_by_name(editor.Widget(), "paste-clipboard");
            Check(editor.Text() == "天", "paste changed editor");
            Key(editor, 104, GDK_KEY_v, 55, false, GDK_CONTROL_MASK);
            Check(editor.Text() == "天", "shortcut paste changed editor");
            g_signal_emit_by_name(editor.Widget(), "preedit-changed", "di");
            Click(pane, "结束跟打");
            Check(pane.Snapshot().at("armed"), "finish accepted preedit");
            g_signal_emit_by_name(editor.Widget(), "preedit-changed", "");
            clock = 30;
            Click(pane, "结束跟打");
            Wait([&] { return pane.Snapshot().at("results").size() == 1; });
            auto first = pane.Snapshot().at("result");
            Check(!first.at("isComplete").get<bool>() && first.at("practiceReasons").size() == 2 &&
                      !first.at("metrics").contains("chordCount"),
                  "interrupted practice fabricated a formal/chord score");
            Click(pane, "结束跟打");
            Check(pane.Snapshot().at("results").size() == 1, "finish duplicated result");
            clock = 40;
            Click(pane, "开始或重新跟打");
            Key(editor, 200, GDK_KEY_t, 28);
            clock = 100;
            Insert(editor, target);
            Wait([&] { return pane.Snapshot().at("results").size() == 2; });
            Check(pane.Snapshot().at("result").at("isComplete") &&
                      pane.Snapshot().at("result").at("practiceReasons").empty(),
                  "complete result incorrectly classified");
            clock = 110;
            Click(pane, "开始或重新跟打");
            Key(editor, 300, GDK_KEY_t, 28);
            clock = 120;
            Insert(editor, "天");
            Check(pane.CanLeave(), "leaving practice blocked");
        }
        gtk_widget_destroy(window);
        Settle();
        Check(pt::HistoryStore(root).Snapshot().at("file").at("results").size() == 3,
              "closing page lost queued result");
        std::ofstream(root / "stats/typing_speed.json") << "passive-data-must-stay";
        window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        {
            PracticePane pane(GTK_WINDOW(window), "history", "night", false);
            gtk_container_add(GTK_CONTAINER(window), pane.Widget());
            gtk_widget_show_all(window);
            Wait([&] { return pane.Snapshot().at("results").size() == 3; });
            Reply(GTK_RESPONSE_CANCEL);
            Click(pane, "清空文章跟打成绩");
            Check(pt::HistoryStore(root).Snapshot().at("file").at("results").size() == 3,
                  "cancel cleared scores");
            Reply(GTK_RESPONSE_ACCEPT);
            Click(pane, "清空文章跟打成绩");
            Wait([&] { return pane.Snapshot().at("results").empty(); });
        }
        gtk_widget_destroy(window);
        Settle();
        std::ifstream passive(root / "stats/typing_speed.json");
        std::string bytes;
        std::getline(passive, bytes);
        Check(bytes == "passive-data-must-stay", "practice clear touched passive history");
        Check(pt::InsertionRange("é", 1, 1) == std::make_pair(0, 1) &&
                  pt::InsertionRange("👩‍💻", 1, 2) == std::make_pair(0, 1),
              "combining/ZWJ insertion ranges differ");
        std::cout << "PASS: real GTK preedit/commits, physical/repeat dedup, identical "
                     "replacements, paste rejection, finish barrier, partial/complete results, "
                     "restart, queue drain and clear isolation\n";
        std::filesystem::remove_all(root);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << " (" << root << ")\n";
        return 1;
    }
}
