#include "metrics_store.hpp"
#include "metrics_views.hpp"
#include "official_catalog.hpp"
#include "statistics_pane.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

using namespace rimes::desktop;
namespace mt = rimes::metrics;
namespace rt = rimes::runtime;
void Check(bool ok, const std::string& message) {
    if (!ok)
        throw std::runtime_error(message);
}
GtkWidget* Find(GtkWidget* widget, const std::string& name) {
    const char* title = atk_object_get_name(gtk_widget_get_accessible(widget));
    if (title && name == title)
        return widget;
    if (GTK_IS_CONTAINER(widget)) {
        auto* list = gtk_container_get_children(GTK_CONTAINER(widget));
        GtkWidget* found = nullptr;
        for (auto* child = list; child && !found; child = child->next)
            found = Find(GTK_WIDGET(child->data), name);
        g_list_free(list);
        if (found)
            return found;
    }
    return nullptr;
}
void Settle() {
    while (gtk_events_pending())
        gtk_main_iteration();
}
void Wait(const std::function<bool()>& predicate) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    do {
        Settle();
        if (predicate())
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while (std::chrono::steady_clock::now() < until);
    throw std::runtime_error("GTK asynchronous operation timed out");
}
Json Read(const std::filesystem::path& path) {
    std::ifstream in(path);
    Json value;
    in >> value;
    return value;
}
Json Call(Json command) {
    bool done = false;
    Json response;
    auto* cancel = g_cancellable_new();
    StatisticsCall(command, cancel, [&](Json value) {
        response = std::move(value);
        done = true;
    });
    Wait([&] { return done; });
    g_object_unref(cancel);
    Check(response.value("ok", false), response.value("message", "statistics failed"));
    return response;
}
void Reply(int response) {
    g_idle_add(
        +[](gpointer data) -> gboolean {
            auto* list = gtk_window_list_toplevels();
            bool answered = false;
            for (auto* item = list; item; item = item->next)
                if (GTK_IS_MESSAGE_DIALOG(item->data)) {
                    gtk_dialog_response(GTK_DIALOG(item->data), GPOINTER_TO_INT(data));
                    answered = true;
                }
            g_list_free(list);
            return answered ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
        },
        GINT_TO_POINTER(response));
}
void Key(GtkWidget* widget, guint keyval) {
    GdkEventKey event{};
    event.type = GDK_KEY_PRESS;
    event.keyval = keyval;
    gboolean handled = FALSE;
    g_signal_emit_by_name(widget, "key-press-event", &event, &handled);
    Check(handled, "graph keyboard navigation failed");
    Settle();
}
int main(int argc, char** argv) {
    char temporary[] = "/tmp/xsime-metrics-ui-XXXXXX";
    if (!mkdtemp(temporary))
        return 1;
    const std::filesystem::path root(temporary);
    setenv("RIMES_USER_DIR", temporary, 1);
    setenv("NO_AT_BRIDGE", "1", 1);
    try {
        gtk_init(&argc, &argv);
        g_object_set(gtk_settings_get_default(), "gtk-enable-animations", FALSE, nullptr);
        const auto fixture = Read(NATIVE_DASHBOARD_FIXTURE);
        const std::string day = fixture.at("day");
        const auto output = argc == 2 ? std::filesystem::path(argv[1]) : std::filesystem::path();
        if (!output.empty())
            std::filesystem::create_directories(output);
        for (const auto* theme : {"night", "day", "quiet", "rasta"})
            for (const auto* page : {"daily", "history"}) {
                Theme(theme);
                auto* window = Class(gtk_offscreen_window_new(), "statistics-surface");
                gtk_window_set_default_size(GTK_WINDOW(window), 698, 1);
                {
                    StatisticsPane pane(GTK_WINDOW(window), page, theme, true, fixture);
                    gtk_widget_set_margin_start(pane.Widget(), 24);
                    gtk_widget_set_margin_end(pane.Widget(), 24);
                    gtk_widget_set_margin_bottom(pane.Widget(), 22);
                    gtk_container_add(GTK_CONTAINER(window), pane.Widget());
                    gtk_widget_show_all(window);
                    Settle();
                    const auto geometry = pane.Geometry();
                Check(gtk_widget_get_allocated_width(window) == 698 &&
                          gtk_widget_get_allocated_height(window) == (std::string(page) == "daily" ? 738 : 1162),
                      "native statistics page bounds differ");
                    Check(geometry.at("trend").at("height") == 190 &&
                              geometry.at("keyboard").at("height") == 260,
                          "native chart heights differ");
                    for (int i = 0; i < 4; ++i)
                        Check(geometry.at("card-" + std::to_string(i)).at("width") == 155,
                              "native card width differs");
                    if (!output.empty()) {
                        const auto name = std::string(page) + "-" + theme + ".png";
                        Check(Render(window, (output / name).string()), "statistics render failed");
                        auto frame = geometry;
                        frame["page"] = {{"width", gtk_widget_get_allocated_width(window)},
                                         {"height", gtk_widget_get_allocated_height(window)}};
                        std::ofstream(output / (name + ".geometry.json")) << frame.dump(2) << '\n';
                    }
                    auto* chart = Find(pane.Widget(), "统计趋势 · 字符");
                    Check(chart, "trend not accessible");
                    const auto before = std::string(
                        gtk_entry_get_text(GTK_ENTRY(Find(pane.Widget(), "趋势结束日期"))));
                    Key(chart, GDK_KEY_Left);
                    Check(std::string(gtk_entry_get_text(
                              GTK_ENTRY(Find(pane.Widget(), "趋势结束日期")))) == before,
                          "chart selection moved trend range");
                    auto* date = Find(pane.Widget(), "趋势结束日期");
                    const auto first_day = mt::DayFromSerial(*mt::DaySerial(day) - 3);
                    gtk_entry_set_text(GTK_ENTRY(date), first_day.c_str());
                    g_signal_emit_by_name(date, "activate");
                    Settle();
                    Check(Find(pane.Widget(), std::string(page) == "daily"
                                                  ? first_day + " · 日常输入"
                                                  : "日期详情 · " + first_day),
                          "date drilldown not updated");
                    if (std::string(page) == "history") {
                        auto* calendar = Find(pane.Widget(), "按键日历 · 全部历史");
                        // The section label and graphic share a title; locate the focusable
                        // drawing.
                        std::function<GtkWidget*(GtkWidget*)> graphic =
                            [&](GtkWidget* widget) -> GtkWidget* {
                            if (GTK_IS_DRAWING_AREA(widget)) {
                                const char* name =
                                    atk_object_get_name(gtk_widget_get_accessible(widget));
                                if (name && std::string(name) == "按键日历 · 全部历史")
                                    return widget;
                            }
                            if (!GTK_IS_CONTAINER(widget))
                                return nullptr;
                            auto* children = gtk_container_get_children(GTK_CONTAINER(widget));
                            GtkWidget* found = nullptr;
                            for (auto* item = children; item && !found; item = item->next)
                                found = graphic(GTK_WIDGET(item->data));
                            g_list_free(children);
                            return found;
                        };
                        calendar = graphic(pane.Widget());
                        Check(calendar, "calendar graphic missing");
                        Key(calendar, GDK_KEY_Home);
                        Check(std::string(gtk_entry_get_text(GTK_ENTRY(date))) ==
                                  fixture.at("keys").at("days").begin().key(),
                              "calendar did not select first record");
                    }
                }
                gtk_widget_destroy(window);
            }
        // Offline browsing and consent changes must not grant execution.
        rt::ConfigurationStore configuration(root / "configuration");
        rimes::plugins::PackageStore packages(
            root / "plugins", rimes::plugins::OfficialCatalog(), rimes::plugins::BundledPackages(),
            rimes::plugins::CompiledAdapters(), RIMES_PRODUCT_VERSION);
        Check(!packages.Inspect("builtin.statistics").enabled,
              "new statistics plugin enabled itself");
        rt::PrivateDirectory files(root / "stats");
        files.Write("key_frequency.json", fixture.at("keys").dump());
        files.Write("typing_speed.json", fixture.at("activity").dump());
        auto* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        Theme("night");
        {
            StatisticsPane pane(GTK_WINDOW(window), "daily", "night", false);
            gtk_container_add(GTK_CONTAINER(window), pane.Widget());
            gtk_widget_show_all(window);
            Wait([&] { return Find(pane.Widget(), "输入法未运行，显示本机历史记录。"); });
            auto* toggle = Find(pane.Widget(), "记录键频");
            Check(toggle, "consent control missing");
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle), FALSE);
            Settle();
            Check(
                !configuration.Plugin("builtin.statistics").at("recordsKeyFrequency").get<bool>() &&
                    configuration.Plugin("builtin.statistics")
                        .at("recordsTypingActivity")
                        .get<bool>(),
                "independent consent not persisted");
            Check(!packages.Inspect("builtin.statistics").enabled &&
                      Read(root / "stats/key_frequency.json") == fixture.at("keys"),
                  "opening page changed grant/history");
            const auto key_bytes = files.Read("key_frequency.json", mt::kMaximumFileBytes);
            auto* clear = Find(pane.Widget(), "清空输入趋势…");
            Check(clear, "clear missing");
            Reply(GTK_RESPONSE_CANCEL);
            gtk_button_clicked(GTK_BUTTON(clear));
            Check(!mt::Store(root).Snapshot().at("activity").at("days").empty(),
                  "cancel cleared history");
            Reply(GTK_RESPONSE_ACCEPT);
            gtk_button_clicked(GTK_BUTTON(clear));
            Wait([&] { return mt::Store(root).Snapshot().at("activity").at("days").empty(); });
            Check(files.Read("key_frequency.json", mt::kMaximumFileBytes) == key_bytes,
                  "clearing activity changed key file");
            const auto exported = (root / "XSIME-statistics.json").string();
            g_timeout_add(
                30,
                +[](gpointer data) -> gboolean {
                    auto* list = gtk_window_list_toplevels();
                    bool found = false;
                    for (auto* item = list; item; item = item->next)
                        if (GTK_IS_FILE_CHOOSER(item->data)) {
                            auto* chooser = GTK_FILE_CHOOSER(item->data);
                            gtk_file_chooser_set_filename(chooser, static_cast<const char*>(data));
                            gtk_dialog_response(GTK_DIALOG(item->data), GTK_RESPONSE_ACCEPT);
                            found = true;
                        }
                    g_list_free(list);
                    return found ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
                },
                const_cast<char*>(exported.c_str()));
            gtk_button_clicked(GTK_BUTTON(Find(pane.Widget(), "导出统计聚合")));
            const auto export_value = Read(exported);
            struct stat st {};
            stat(exported.c_str(), &st);
            Check(export_value.size() == 3 && mt::ValidKeysFile(export_value.at("keys")) &&
                      mt::ValidActivityFile(export_value.at("activity")) &&
                      (st.st_mode & 0777) == 0600,
                  "export leaked metadata or unsafe permissions");
        }
        gtk_widget_destroy(window);
        const auto reply = Call({{"op", "status"}});
        Check(reply.value("offline", false) && !reply.at("recordsKeyFrequency").get<bool>(),
              "restart lost independent consent");
        // Only aggregate snapshots are exported; malformed files stay read-only until explicit
        // repair.
        files.Write("typing_speed.json", "not-json");
        const auto before = Call({{"op", "status"}});
        Check(!before.at("snapshot").at("issues").at("activity").get<std::string>().empty(),
              "corruption was hidden");
        const auto repaired = Call({{"op", "repair"}, {"collector", "activity"}});
        Check(files.Read(repaired.at("backup").get<std::string>(), 100) ==
                  std::optional<std::string>("not-json"),
              "repair did not retain exact bytes");
        std::cout << "PASS: eight native-data renders, chart/calendar/date navigation, offline "
                     "history, independent consent without activation, cancel/clear isolation, "
                     "private aggregate export, restart and explicit repair\n";
        std::filesystem::remove_all(root);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << " (artifacts " << root << ")\n";
        return 1;
    }
}
