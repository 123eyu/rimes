#include "metrics_protocol.hpp"
#include "metrics_store.hpp"
#include "metrics_views.hpp"
#include "statistics_pane.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <ctime>
#include <deque>
#include <iomanip>
#include <sstream>

namespace rimes::desktop {
namespace mt = rimes::metrics;
namespace {
struct Request {
    Json command;
    std::filesystem::path root;
    std::function<void(Json)> done;
};
std::string Number(double number, int precision = 0) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << number;
    return out.str();
}
std::string Duration(double seconds) {
    auto value = static_cast<std::int64_t>(std::round(std::max(0., seconds)));
    if (value >= 3600)
        return std::to_string(value / 3600) + "时" + std::to_string(value % 3600 / 60) + "分";
    if (value >= 60)
        return std::to_string(value / 60) + "分" + std::to_string(value % 60) + "秒";
    return std::to_string(value) + "秒";
}
std::string SessionLabel(double stamp) {
    if (!std::isfinite(stamp) || stamp < 0 || stamp > 253402300799.0)
        return "时间未知";
    const auto seconds = static_cast<time_t>(stamp);
    tm date{};
    if (!localtime_r(&seconds, &date)) return "时间未知";
    char text[32];
    strftime(text, sizeof(text), "%m.%d %H:%M", &date);
    return text;
}
GtkWidget* Named(GtkWidget* widget, const char* name) {
    atk_object_set_name(gtk_widget_get_accessible(widget), name);
    return widget;
}
GtkWidget* TextLabel(const std::string& text, const char* style = nullptr) {
    auto* label = Class(Label(text, style), "statistics-label");
    auto* attributes = pango_attr_list_new();
    pango_attr_list_insert(attributes,
                           pango_attr_line_height_new_absolute(
                               (style && std::string(style) == "section" ? 15 : 14) * PANGO_SCALE));
    gtk_label_set_attributes(GTK_LABEL(label), attributes);
    pango_attr_list_unref(attributes);
    return label;
}
Json EmptySnapshot() {
    return {{"formatVersion", 1},
            {"keys", mt::KeyFrequency().File()},
            {"activity", mt::TypingActivity().File()},
            {"issues", {{"keys", ""}, {"activity", ""}}}};
}
void ValidateResponse(Json& value) {
    if (!value.is_object() || !value.contains("ok") || !value.at("ok").is_boolean())
        throw std::runtime_error("无法读取统计响应");
    for (const auto* key : {"enabled", "offline", "unchanged", "recordsKeyFrequency", "recordsTypingActivity"})
        if (value.contains(key) && !value.at(key).is_boolean()) throw std::runtime_error("统计状态无效");
    for (const auto* key : {"message", "backup"})
        if (value.contains(key) && (!value.at(key).is_string() || value.at(key).get_ref<const std::string&>().size() > 4096))
            throw std::runtime_error("统计状态无效");
    if (value.contains("revision") && !value.at("revision").is_number_unsigned())
        throw std::runtime_error("统计版本无效");
    if (!value.contains("snapshot")) return;
    auto& snapshot = value.at("snapshot");
    snapshot["keys"] = mt::KeyFrequency(snapshot.at("keys")).File();
    snapshot["activity"] = mt::TypingActivity(snapshot.at("activity")).File();
    for (const auto* key : {"keys", "activity"}) {
        const auto& issue = snapshot.at("issues").at(key);
        if (!issue.is_string() || issue.get_ref<const std::string&>().size() > 4096)
            throw std::runtime_error("统计存储状态无效");
    }
}
} // namespace
void StatisticsCall(const Json& command, GCancellable* cancel, std::function<void(Json)> done) {
    // Blocking D-Bus and offline file reads run in GLib's bounded worker pool.
    // The completion is delivered to the caller's GTK main context.
    auto* task = g_task_new(
        nullptr, cancel,
        +[](GObject*, GAsyncResult* result, gpointer) {
            auto* task = G_TASK(result);
            auto* request = static_cast<Request*>(g_task_get_task_data(task));
            GError* error = nullptr;
            std::unique_ptr<Json> value(static_cast<Json*>(g_task_propagate_pointer(task, &error)));
            g_clear_error(&error);
            if (value)
                request->done(std::move(*value));
        },
        nullptr);
    g_task_set_task_data(
        task, new Request{command, runtime::UserRoot(), std::move(done)},
        +[](gpointer data) { delete static_cast<Request*>(data); });
    g_task_run_in_thread(
        task, +[](GTask* task, gpointer, gpointer data, GCancellable* cancel) {
            auto* request = static_cast<Request*>(data);
            Json value;
            try {
                const auto bytes = request->command.dump();
                if (bytes.size() > 4096)
                    throw std::runtime_error("统计请求过大");
                GError* error = nullptr;
                auto* bus = g_bus_get_sync(G_BUS_TYPE_SESSION, cancel, &error);
                bool offline = !bus && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
                g_clear_error(&error);
                GVariant* reply = nullptr;
                if (bus) {
                    reply = g_dbus_connection_call_sync(
                        bus, mt::kBusName, mt::kBusPath, mt::kBusInterface, "Command",
                        g_variant_new("(s)", bytes.c_str()), G_VARIANT_TYPE("(s)"),
                        G_DBUS_CALL_FLAGS_NO_AUTO_START, 3000, cancel, &error);
                    offline = g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN) ||
                              g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_NAME_HAS_NO_OWNER);
                    g_object_unref(bus);
                }
                if (reply) {
                    const char* text = nullptr;
                    g_variant_get(reply, "(&s)", &text);
                    if (text && std::strlen(text) <= 2 * mt::kMaximumFileBytes + 16384)
                        value = Json::parse(text, nullptr, false);
                    g_variant_unref(reply);
                    if (!value.is_object())
                        throw std::runtime_error("无法读取统计响应");
                } else if (offline && !g_cancellable_is_cancelled(cancel)) {
                    // A second live writer cannot acquire this lock. Never fall
                    // back after a timeout, which could repeat a destructive op.
                    const std::string op = request->command.at("op");
                    mt::Store store(request->root, op != "status");
                    std::string backup;
                    if (op == "clear")
                        store.Clear(request->command.at("collector"),
                                    request->command.contains("day")
                                        ? std::optional<std::string>(request->command.at("day"))
                                        : std::nullopt);
                    else if (op == "repair")
                        backup = store.Repair(request->command.at("collector"));
                    else if (op == "flush")
                        store.Flush();
                    else if (op != "status")
                        throw std::runtime_error("未知统计操作");
                    const auto options =
                        runtime::ConfigurationStore(request->root / "configuration")
                            .Plugin(mt::kPluginID);
                    value = {{"ok", true},
                             {"offline", true},
                             {"enabled", false},
                             {"snapshot", store.Snapshot()},
                             {"recordsKeyFrequency", options.at("recordsKeyFrequency")},
                             {"recordsTypingActivity", options.at("recordsTypingActivity")}};
                    if (!backup.empty())
                        value["backup"] = backup;
                } else {
                    const std::string message = error ? error->message : "统计服务不可用";
                    g_clear_error(&error);
                    throw std::runtime_error(message);
                }
                g_clear_error(&error);
                ValidateResponse(value);
            } catch (const std::exception& error) {
                value = {{"ok", false}, {"message", error.what()}};
            }
            g_task_return_pointer(
                task, new Json(std::move(value)),
                +[](gpointer value) { delete static_cast<Json*>(value); });
        });
    g_object_unref(task);
}
struct StatisticsPane::Impl {
    GtkWindow* parent;
    bool history, rendering, pending = false, updating = false;
    std::string theme, today = mt::LocalDay(mt::Store::Now()), end = today, selected = today;
    int range = 7;
    bool speed = false;
    Json snapshot = EmptySnapshot();
    std::uint64_t revision = 0;
    std::uint64_t consent_generation = 0;
    std::deque<Json> commands;
    GtkWidget *root = Class(Column(0), "statistics"), *warnings = Column(8),
              *canvas = gtk_fixed_new();
    GtkWidget *message = nullptr, *date = nullptr, *summary = nullptr, *day_label = nullptr,
              *keyboard_summary = nullptr, *best = nullptr, *key_toggle = nullptr,
              *activity_toggle = nullptr, *clear_keys = nullptr, *clear_activity = nullptr;
    std::vector<GtkWidget*> metric_buttons, range_buttons;
    std::vector<std::unique_ptr<MetricsGraphic>> cards;
    std::unique_ptr<MetricsGraphic> trend, keyboard, calendar, sessions;
    std::map<std::string, GtkWidget*> geometry;
    std::function<void()> changed;
    GCancellable* cancel = g_cancellable_new();
    guint poll = 0;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    Impl(GtkWindow* host, std::string page, std::string style, bool render, const Json& fixture,
         std::function<void()> callback)
        : parent(host), history(page == "history"), rendering(render), theme(std::move(style)),
          changed(std::move(callback)) {
        g_object_ref_sink(root);
        range = history ? 30 : 7;
        if (!fixture.empty()) {
            if (fixture.value("purpose", "") != "public-statistics-dashboard")
                throw std::runtime_error("无效的统计展示样例");
            snapshot["keys"] = mt::KeyFrequency(fixture.at("keys")).File();
            snapshot["activity"] = mt::TypingActivity(fixture.at("activity")).File();
            today = end = selected = fixture.at("day");
        }
        Build();
        Refresh();
        if (!rendering) {
            Request({{"op", "status"}});
            poll = g_timeout_add(
                1000,
                +[](gpointer data) -> gboolean {
                    auto* self = static_cast<Impl*>(data);
                    if (!self->pending)
                        self->Request({{"op", "status"}, {"knownRevision", self->revision}});
                    return G_SOURCE_CONTINUE;
                },
                this);
        }
    }
    ~Impl() {
        alive->store(false);
        if (poll)
            g_source_remove(poll);
        g_cancellable_cancel(cancel);
        g_object_unref(cancel);
        cards.clear();
        trend.reset();
        keyboard.reset();
        calendar.reset();
        sessions.reset();
        gtk_widget_destroy(root);
        g_object_unref(root);
    }
    void Put(GtkWidget* widget, int x, int y, int width, int height, const std::string& name = {}) {
        gtk_widget_set_size_request(widget, width, height);
        gtk_fixed_put(GTK_FIXED(canvas), widget, x, y);
        if (!name.empty())
            geometry[name] = widget;
    }
    GtkWidget* Segment(const std::vector<std::string>& titles, std::vector<GtkWidget*>& buttons,
                       std::function<void(int)> choose) {
        auto* row = Class(Row(0), "metrics-segments");
        for (std::size_t i = 0; i < titles.size(); ++i) {
            auto* button = Button(titles[i], [choose, i] { choose(int(i)); }, "metrics-segment");
            buttons.push_back(button);
            Add(row, button, true);
        }
        return row;
    }
    void Build() {
        Add(root, warnings);
        Add(root, canvas);
        static GtkCssProvider* css = nullptr;
        if (!css) {
            css = gtk_css_provider_new();
            const char* rules =
                ".statistics button { min-height: 16px; min-width: 0; padding: 2px 8px; font-size: "
                "11px; }"
                ".statistics entry { min-height: 18px; padding: 2px 4px; min-width: 0; font-size: "
                "11px; }"
                ".statistics-label { min-height: 0; padding: 0; } .statistics-label.section { "
                "font-size: 12px; }"
                ".statistics .metrics-segment { min-height: 16px; padding: 0 7px; border-radius: "
                "0; }"
                ".statistics .metrics-segment:first-child { border-radius: 5px 0 0 5px; }"
                ".statistics .metrics-segment:last-child { border-radius: 0 5px 5px 0; }"
                ".statistics checkbutton { min-height: 16px; font-size: 11px; padding: 0; margin: "
                "0; }"
                ".statistics check { min-width: 12px; min-height: 12px; margin: 0 4px 0 0; }"
                ".statistics button.metrics-clear-small { min-height: 16px; padding: 0 6px; }";
            gtk_css_provider_load_from_data(css, rules, -1, nullptr);
            gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                                      GTK_STYLE_PROVIDER(css),
                                                      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
            g_object_unref(css);
        }
        date = Named(gtk_entry_new(), "趋势结束日期");
        gtk_entry_set_width_chars(GTK_ENTRY(date), 1);
        gtk_entry_set_icon_from_icon_name(GTK_ENTRY(date), GTK_ENTRY_ICON_SECONDARY,
                                          "x-office-calendar-symbolic");
        gtk_widget_set_tooltip_text(
            date, "输入 YYYY-MM-DD 或选择日期；点击趋势只切换详情，不移动趋势范围。");
        g_signal_connect(date, "activate", G_CALLBACK(+[](GtkEntry* entry, gpointer data) {
                             auto* self = static_cast<Impl*>(data);
                             const std::string value = gtk_entry_get_text(entry);
                             if (mt::DaySerial(value) && value <= self->today) {
                                 self->end = self->selected = value;
                                 self->Refresh();
                             } else
                                 self->Notice("请选择今天或之前的有效日期（YYYY-MM-DD）。");
                         }),
                         this);
        g_signal_connect(
            date, "icon-press",
            G_CALLBACK(+[](GtkEntry* entry, GtkEntryIconPosition, GdkEvent*, gpointer data) {
                auto* self = static_cast<Impl*>(data);
                auto* popover = gtk_popover_new(GTK_WIDGET(entry));
                auto* picker = gtk_calendar_new();
                gtk_calendar_select_month(GTK_CALENDAR(picker),
                                          std::stoi(self->end.substr(5, 2)) - 1,
                                          std::stoi(self->end.substr(0, 4)));
                gtk_calendar_select_day(GTK_CALENDAR(picker), std::stoi(self->end.substr(8, 2)));
                g_object_set_data(G_OBJECT(picker), "popover", popover);
                g_signal_connect(picker, "day-selected-double-click",
                                 G_CALLBACK(+[](GtkCalendar* calendar, gpointer data) {
                                     auto* self = static_cast<Impl*>(data);
                                     guint y = 0;
                                     guint m = 0;
                                     guint d = 0;
                                     gtk_calendar_get_date(calendar, &y, &m, &d);
                                     char text[20];
                                     g_snprintf(text, sizeof(text), "%04u-%02u-%02u", y, m + 1, d);
                                     if (mt::DaySerial(text) && text <= self->today) {
                                         self->end = self->selected = text;
                                         self->Refresh();
                                     }
                                     gtk_widget_destroy(GTK_WIDGET(
                                         g_object_get_data(G_OBJECT(calendar), "popover")));
                                 }),
                                 self);
                gtk_container_add(GTK_CONTAINER(popover), picker);
                gtk_widget_show_all(popover);
                gtk_popover_popup(GTK_POPOVER(popover));
            }),
            this);
        auto* metric = Segment({"成文字数", "日常速度"}, metric_buttons, [this](int i) {
            speed = i == 1;
            Refresh();
        });
        auto* ranges = Segment({"7 天", "30 天"}, range_buttons, [this](int i) {
            range = i ? 30 : 7;
            Refresh();
        });
        Put(metric, 0, history ? 2 : 159, 129, 21);
        Put(ranges, 570, history ? 2 : 159, 80, 21);
        Put(date, history ? 453 : 0, 0, 107, 24);
        clear_keys = Button(history ? "清空键频历史…" : "清空当天键频…", [this] { ClearKeys(); });
        Put(clear_keys, 541, history ? 257 : 0, 109, 24);
        auto* export_button = Named(Button("导出…", [this] { Export(); }), "导出统计聚合");
        Put(export_button, 471, history ? 257 : 0, 60, 24);
        // History has a date in its first row; export belongs to the calendar header.
        summary = TextLabel("");
        Put(summary, 0, history ? 262 : 38, history ? 450 : 650, 14);
        for (int i = 0; i < 4; ++i) {
            auto card = std::make_unique<MetricsGraphic>(MetricsGraphic::Kind::Card, theme, 92);
            Put(card->Widget(), i * 165, history ? 515 : 58, 155, 92, "card-" + std::to_string(i));
            cards.push_back(std::move(card));
        }
        trend = std::make_unique<MetricsGraphic>(MetricsGraphic::Kind::Trend, theme, 190);
        trend->on_select = [this](const std::string& day) {
            selected = day;
            Refresh();
        };
        Put(trend->Widget(), 0, history ? 34 : 188, 650, 190, "trend");
        keyboard = std::make_unique<MetricsGraphic>(MetricsGraphic::Kind::Keyboard, theme, 260);
        Put(keyboard->Widget(), 0, history ? 637 : 413, 650, 260, "keyboard");
        if (history) {
            Put(TextLabel("按键日历 · 全部历史", "section"), 0, 234, 650, 15);
            calendar = std::make_unique<MetricsGraphic>(MetricsGraphic::Kind::Calendar, theme, 164);
            calendar->on_select = [this](const std::string& day) {
                if (day <= today) {
                    end = selected = day;
                    Refresh();
                }
            };
            auto* scroll = gtk_scrolled_window_new(nullptr, nullptr);
            gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC,
                                           GTK_POLICY_NEVER);
            gtk_container_add(GTK_CONTAINER(scroll), calendar->Widget());
            Put(scroll, 0, 293, 650, 164, "calendar");
            Put(gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, 481, 650, 1);
            day_label = TextLabel("", "section");
            Put(day_label, 0, 492, 650, 15);
            keyboard_summary = TextLabel("");
            Put(keyboard_summary, 0, 615, 650, 14);
            Put(TextLabel("最近输入会话", "section"), 0, 907, 250, 15);
            best = TextLabel("", "tiny");
            gtk_label_set_xalign(GTK_LABEL(best), 1);
            Put(best, 350, 908, 300, 13);
            sessions = std::make_unique<MetricsGraphic>(MetricsGraphic::Kind::Trend, theme, 165);
            Put(sessions->Widget(), 0, 932, 650, 165, "sessions");
        } else
            Put(TextLabel("键盘分布", "section"), 0, 388, 650, 15);
        const int y = history ? 1107 : 683;
        Put(gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, y, 650, 1);
        Put(TextLabel("本机记录", "section"), 0, y + 17, 52, 15);
        key_toggle = Named(gtk_check_button_new_with_label("按键分布"), "记录键频");
        activity_toggle = Named(gtk_check_button_new_with_label("输入趋势"), "记录日常输入");
        for (auto* toggle : {key_toggle, activity_toggle}) {
            auto* label = gtk_bin_get_child(GTK_BIN(toggle));
            auto* attributes = pango_attr_list_new();
            pango_attr_list_insert(attributes, pango_attr_line_height_new_absolute(14 * PANGO_SCALE));
            gtk_label_set_attributes(GTK_LABEL(label), attributes);
            pango_attr_list_unref(attributes);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle), TRUE);
            g_signal_connect(toggle, "toggled", G_CALLBACK(+[](GtkToggleButton*, gpointer data) {
                                 auto* self = static_cast<Impl*>(data);
                                 if (!self->updating)
                                     self->SaveConsent();
                             }),
                             this);
        }
        Put(key_toggle, 62, y + 17, 82, 16);
        Put(activity_toggle, 154, y + 17, 82, 16);
        clear_activity = Button("清空输入趋势…", [this] {
            if (rendering ||
                !Confirm(
                    parent, "清空全部日常输入趋势？",
                    "永久删除每日成文字数、活跃时长和最近会话。键盘分布和文章测速成绩不受影响。"))
                return;
            Request({{"op", "clear"}, {"collector", "activity"}});
        });
        Class(clear_activity, "metrics-clear-small");
        Put(clear_activity, 553, y + 16, 97, 16);
        gtk_widget_set_size_request(canvas, 650, history ? 1140 : 716);
    }
    void Notice(const std::string& text) {
        if (!message) {
            message = TextLabel(text, "caption");
            gtk_label_set_line_wrap(GTK_LABEL(message), TRUE);
            Add(warnings, message);
        } else
            gtk_label_set_text(GTK_LABEL(message), text.c_str());
        gtk_widget_set_visible(message, !text.empty());
    }
    void Request(Json command) {
        if (rendering)
            return;
        if (pending) {
            if (command.value("op", "status") != "status")
                commands.push_back(std::move(command));
            return;
        }
        pending = true;
        StatisticsCall(
            command, cancel,
            [this, flag = alive, consent_at_request = consent_generation](Json value) {
                if (!flag->load())
                    return;
                pending = false;
                if (!value.value("ok", false)) {
                    Notice(value.value("message", "统计操作未完成，原文件已保留。"));
                    NextRequest();
                    return;
                }
                revision = value.value("revision", std::uint64_t(0));
                if (value.contains("snapshot"))
                    snapshot = value.at("snapshot");
                if (consent_at_request == consent_generation) {
                    updating = true;
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(key_toggle),
                                                 value.value("recordsKeyFrequency", true));
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(activity_toggle),
                                                 value.value("recordsTypingActivity", true));
                    updating = false;
                }
                Clear(warnings);
                message = nullptr;
                if (value.value("offline", false))
                    Notice("输入法未运行，显示本机历史记录。");
                else if (!value.value("enabled", false))
                    Notice("统计扩展未启用；可在扩展管理中开启，现有历史记录仍保留。");
                if (value.contains("backup"))
                    Notice("原文件已备份为 " + value.at("backup").get<std::string>());
                for (const auto* collector : {"keys", "activity"}) {
                    const auto issue = snapshot.at("issues").value(collector, "");
                    if (issue.empty())
                        continue;
                    auto* warning = Column(6);
                    Add(warning, TextLabel(std::string(collector) == "keys"
                                               ? "键频存储只读，原路径不会被覆盖。"
                                               : "输入趋势存储只读，原路径不会被覆盖。"));
                    auto* detail = TextLabel(issue, "caption");
                    gtk_label_set_line_wrap(GTK_LABEL(detail), TRUE);
                    Add(warning, detail);
                    Add(warning, Button("备份并重建…", [this, collector = std::string(collector)] {
                            if (Confirm(parent, "重建统计存储？",
                                        "先在同一目录保留损坏文件或符号链接，再建立空库；不会读取符"
                                        "号链接目标。"))
                                Request({{"op", "repair"}, {"collector", collector}});
                        }));
                    Add(warnings, warning);
                }
                gtk_widget_show_all(warnings);
                Refresh();
                NextRequest();
            });
    }
    void NextRequest() {
        if (commands.empty())
            return;
        auto next = std::move(commands.front());
        commands.pop_front();
        Request(std::move(next));
    }
    void SaveConsent() {
        if (rendering)
            return;
        try {
            runtime::ConfigurationStore config(runtime::UserRoot() / "configuration");
            auto value = config.Load();
            value["plugins"][mt::kPluginID] = {
                {"recordsKeyFrequency",
                 bool(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(key_toggle)))},
                {"recordsTypingActivity",
                 bool(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(activity_toggle)))}};
            config.Save(value, value.at("revision"));
            ++consent_generation;
            if (changed)
                changed();
            // Leave plugin installation/enabled state and historical data alone.
            revision = 0;
            Request({{"op", "status"}});
        } catch (const std::exception& error) {
            Notice(error.what());
        }
    }
    void ClearKeys() {
        if (rendering ||
            !Confirm(parent, history ? "清空全部键频历史？" : "清空当天键频？",
                     "只删除按键次数；输入趋势、用户词库、学习记录和文章测速成绩不受影响。"))
            return;
        Json command = {{"op", "clear"}, {"collector", "keys"}};
        if (!history)
            command["day"] = selected;
        Request(std::move(command));
    }
    void Export() {
        if (rendering)
            return;
        auto* chooser = gtk_file_chooser_dialog_new(
            "导出本机统计", parent, GTK_FILE_CHOOSER_ACTION_SAVE, "取消", GTK_RESPONSE_CANCEL,
            "导出", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(chooser), TRUE);
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(chooser), TRUE);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(chooser), "XSIME-statistics.json");
        if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
            auto* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
            if (path) {
                // Export only aggregate schema fields, never warnings, paths,
                // plugin settings, app identities, keystroke text or credentials.
                const auto value = Json{{"formatVersion", 1},
                                        {"keys", snapshot.at("keys")},
                                        {"activity", snapshot.at("activity")}};
                auto* file = g_file_new_for_path(path);
                const auto bytes = value.dump(2) + '\n';
                GError* error = nullptr;
                const bool ok = g_file_replace_contents(
                    file, bytes.data(), bytes.size(), nullptr, FALSE,
                    static_cast<GFileCreateFlags>(G_FILE_CREATE_PRIVATE |
                                                  G_FILE_CREATE_REPLACE_DESTINATION),
                    nullptr, nullptr, &error);
                Notice(ok ? "已导出本机统计记录。" : error ? error->message : "导出失败");
                g_clear_error(&error);
                g_object_unref(file);
                g_free(path);
            }
        }
        gtk_widget_destroy(chooser);
    }
    void Refresh() {
        if (!rendering)
            today = mt::LocalDay(mt::Store::Now());
        const auto days = mt::DayRange(end, range);
        if (std::find(days.begin(), days.end(), selected) == days.end())
            selected = end;
        gtk_entry_set_text(GTK_ENTRY(date), end.c_str());
        const auto& activity = snapshot.at("activity").at("days");
        const auto& key_days = snapshot.at("keys").at("days");
        std::vector<MetricSample> samples;
        for (const auto& day : days) {
            std::optional<double> value;
            std::string detail = day + " · 未记录，不视为零速度";
            if (activity.contains(day)) {
                const auto& record = activity.at(day);
                const double seconds = record.at("activeSeconds");
                if (!speed)
                    value = record.at("committedCharacterCount").get<double>();
                else if (seconds > 0)
                    value = mt::CharactersPerMinute(record);
                detail = day + " · 活跃 " + Duration(seconds) + " · 日常输入，非文章测试";
            }
            auto label = day.substr(5);
            label[2] = '.';
            samples.push_back({day, label, detail, value});
        }
        trend->Trend(std::move(samples), speed ? "字符/分" : "字符", "日期",
                     speed ? "尚无可计算速度的输入记录" : "尚无成文记录 · 开始日常输入", selected);
        const bool present = activity.contains(selected);
        const auto day = activity.value(selected, Json::object());
        cards[0]->Card("成文字数", present ? Number(day.at("committedCharacterCount")) : "—",
                       "字符", 0);
        cards[1]->Card("输入按键", present ? Number(day.at("keyCount")) : "—", "键", 1);
        cards[2]->Card("活跃时长", present ? Duration(day.at("activeSeconds")) : "—", "", 2);
        cards[3]->Card("日常速度",
                       present && day.at("activeSeconds").get<double>() > 0
                           ? Number(mt::CharactersPerMinute(day), 1)
                           : "—",
                       "字符/分", 3);
        const auto keys =
            key_days.contains(selected) ? key_days.at(selected).at("keys") : Json::object();
        keyboard->Keyboard(keys);
        const auto summary_text =
            history ? std::to_string(key_days.size()) + " 个记录日" : selected + " · 日常输入";
        gtk_label_set_text(GTK_LABEL(summary), summary_text.c_str());
        for (int i = 0; i < 2; ++i)
            for (const auto& pair : {std::make_pair(metric_buttons[i], speed == bool(i)),
                                     std::make_pair(range_buttons[i], (range == 30) == bool(i))}) {
                auto* style = gtk_widget_get_style_context(pair.first);
                if (pair.second)
                    gtk_style_context_add_class(style, "selected");
                else
                    gtk_style_context_remove_class(style, "selected");
            }
        gtk_widget_set_sensitive(clear_keys, snapshot.at("issues").value("keys", "").empty() &&
                                                 (history ? !key_days.empty() : !keys.empty()));
        gtk_widget_set_sensitive(clear_activity,
                                 snapshot.at("issues").value("activity", "").empty() &&
                                     !activity.empty());
        if (history) {
            calendar->Calendar(key_days, selected);
            gtk_label_set_text(GTK_LABEL(day_label), ("日期详情 · " + selected).c_str());
            std::int64_t total = 0;
            for (const auto& count : keys)
                total += count.get<std::int64_t>();
            gtk_label_set_text(GTK_LABEL(keyboard_summary),
                               ("键盘分布 · " + std::to_string(total) + " 次 · " +
                                std::to_string(keys.size()) + " 个键")
                                   .c_str());
            const auto& history = snapshot.at("activity").at("sessions");
            std::optional<double> maximum;
            std::vector<MetricSample> recent;
            for (std::size_t i = history.size() > 100 ? history.size() - 100 : 0;
                 i < history.size(); ++i) {
                const auto& session = history[i];
                const double seconds = session.at("activeSeconds");
                std::optional<double> value;
                if (seconds > 0) {
                    value = mt::CharactersPerMinute(session);
                    maximum = std::max(maximum.value_or(0), *value);
                }
                if (history.size() - i <= 20)
                    recent.push_back(
                        {"session-" + std::to_string(i), SessionLabel(session.at("startedAt")),
                         "成文 " + Number(session.at("committedCharacterCount")) + " 字符 · 按键 " +
                             Number(session.at("keyCount")) + " 次 · 并击 " +
                             Number(session.at("chordCount")) + " 拍 · 活跃 " + Duration(seconds),
                         value});
            }
            gtk_label_set_text(
                GTK_LABEL(best),
                (maximum ? "近期最佳 " + Number(*maximum, 1) + " 字符/分" : "暂无有效速度")
                    .c_str());
            sessions->Trend(std::move(recent), "字符/分", "会话顺序 · 最近 20 次",
                            "开始日常输入，积累会话记录", sessions->Selection());
        }
    }
};
StatisticsPane::StatisticsPane(GtkWindow* parent, std::string page, std::string theme,
                               bool rendering, const Json& fixture, std::function<void()> changed)
    : impl_(std::make_unique<Impl>(parent, std::move(page), std::move(theme), rendering, fixture,
                                   std::move(changed))) {}
StatisticsPane::~StatisticsPane() = default;
GtkWidget* StatisticsPane::Widget() const { return impl_->root; }
Json StatisticsPane::Geometry() const {
    Json result = Json::object();
    for (const auto& [name, widget] : impl_->geometry) {
        int x = 0, y = 0;
        gtk_widget_translate_coordinates(widget, impl_->root, 0, 0, &x, &y);
        result[name] = {{"x", x},
                        {"y", y},
                        {"width", gtk_widget_get_allocated_width(widget)},
                        {"height", gtk_widget_get_allocated_height(widget)}};
    }
    return result;
}
} // namespace rimes::desktop
