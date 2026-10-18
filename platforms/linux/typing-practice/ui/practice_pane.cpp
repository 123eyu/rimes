#include "practice_pane.hpp"
#include "metrics_views.hpp"
#include "native_practice_articles.hpp"
#include "practice_telemetry.hpp"
#include "practice_worker.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace rimes::desktop {
namespace pt = rimes::practice;
namespace {
const Json &Articles() {
    static const Json value = Json::parse(pt::kNativeArticles);
    return value;
}
std::string Number(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(value >= 100 ? 0 : 1) << value;
    return out.str();
}
std::string Duration(double seconds) {
    const int n = std::max(0, int(seconds));
    char text[32];
    g_snprintf(text, sizeof(text), "%02d:%02d", n / 60, n % 60);
    return text;
}
std::string Date(double stamp) {
    const time_t seconds = stamp;
    tm value{};
    if (!localtime_r(&seconds, &value))
        return "时间未知";
    char text[32];
    strftime(text, sizeof(text), "%m.%d %H:%M", &value);
    return text;
}
GtkWidget *Name(GtkWidget *widget, const std::string &title) {
    atk_object_set_name(gtk_widget_get_accessible(widget), title.c_str());
    return widget;
}
GtkWidget *Text(const std::string &text, double size = 11, bool bold = false) {
    auto *value = Label(text);
    auto *attributes = pango_attr_list_new();
    pango_attr_list_insert(attributes, pango_attr_size_new_absolute(size * PANGO_SCALE));
    pango_attr_list_insert(attributes,
                           pango_attr_line_height_new_absolute((size + 3) * PANGO_SCALE));
    pango_attr_list_insert(
        attributes, pango_attr_weight_new(bold ? PANGO_WEIGHT_SEMIBOLD : PANGO_WEIGHT_NORMAL));
    gtk_label_set_attributes(GTK_LABEL(value), attributes);
    pango_attr_list_unref(attributes);
    gtk_label_set_ellipsize(GTK_LABEL(value), PANGO_ELLIPSIZE_END);
    return value;
}
void LabelText(GtkWidget *widget, const std::string &text) {
    gtk_label_set_text(GTK_LABEL(widget), text.c_str());
    Name(widget, text);
}
void Main(std::function<void()> function) {
    g_idle_add_full(
        G_PRIORITY_DEFAULT,
        +[](gpointer p) -> gboolean {
            (*static_cast<std::function<void()> *>(p))();
            return G_SOURCE_REMOVE;
        },
        new std::function<void()>(std::move(function)),
        +[](gpointer p) { delete static_cast<std::function<void()> *>(p); });
}
std::string Length(const Json &article) {
    return article.at("language") == "english"
               ? std::to_string(article.at("wordCount").get<int>()) + " words"
               : std::to_string(article.at("characterCount").get<int>()) + " 字";
}
bool Comparable(const Json &result) {
    return result.at("isComplete").get<bool>() && result.at("practiceReasons").empty();
}
GtkWidget *ScoreIcon(bool comparable, const std::string &theme) {
    auto *widget = gtk_drawing_area_new();
    gtk_widget_set_size_request(widget, 17, 16);
    GdkRGBA color;
    gdk_rgba_parse(&color, Visuals()
                               .at("palettes")
                               .at(theme == "liquidGlass" ? "day" : theme)
                               .at(comparable ? "accentGreen" : "warningText")
                               .get<std::string>()
                               .c_str());
    using State = std::pair<bool, GdkRGBA>;
    g_signal_connect_data(
        widget, "draw", G_CALLBACK(+[](GtkWidget *widget, cairo_t *cr, gpointer p) -> gboolean {
            const auto &state = *static_cast<State *>(p);
            const double y = gtk_widget_get_allocated_height(widget) / 2.;
            gdk_cairo_set_source_rgba(cr, &state.second);
            cairo_set_line_width(cr, 1.1);
            cairo_arc(cr, 8.5, y, 6, 0, 2 * M_PI);
            if (state.first) {
                cairo_fill(cr);
                cairo_set_source_rgb(cr, 1, 1, 1);
                cairo_move_to(cr, 5.8, y);
                cairo_line_to(cr, 8, y + 2.5);
                cairo_line_to(cr, 11.3, y - 3);
            } else {
                cairo_stroke(cr);
                cairo_move_to(cr, 7, y - 2.5);
                cairo_line_to(cr, 7, y + 2.5);
                cairo_move_to(cr, 10, y - 2.5);
                cairo_line_to(cr, 10, y + 2.5);
            }
            cairo_stroke(cr);
            return TRUE;
        }),
        new State{comparable, color},
        +[](gpointer p, GClosure *) { delete static_cast<State *>(p); }, G_CONNECT_DEFAULT);
    return widget;
}
} // namespace
struct PracticePane::Impl {
    GtkWindow *parent;
    bool history, rendering, armed = false, stopping = false, starting = false, updating = false,
                             reading = false;
    std::string theme, phase = "ready", selected_context, revision;
    GtkWidget *root = Class(Column(0), "practice"), *canvas = gtk_fixed_new(), *warning = Text(""),
              *article_picker = gtk_combo_box_text_new(), *context_picker = nullptr,
              *start = nullptr, *finish = nullptr, *status = nullptr, *clock_label = nullptr,
              *progress = nullptr, *percent = nullptr, *article_detail = nullptr, *target = nullptr,
              *chart_title = nullptr, *result_detail = nullptr, *rows = Column(10),
              *clear = nullptr, *repair = nullptr;
    std::vector<std::unique_ptr<MetricsGraphic>> cards, result_cards;
    std::unique_ptr<MetricsGraphic> chart;
    PracticeEditor editor;
    PracticeTelemetry telemetry;
    pt::HistoryWorker worker{runtime::UserRoot()};
    std::unique_ptr<pt::Session> session;
    Json article = Articles().front(), results = Json::array(), snapshot = Json::object(),
         result = Json::object(), rendered_states = Json(), frozen = Json::object();
    int chords = 0;
    std::map<std::pair<guint32, guint16>, bool> backspaces;
    std::set<std::pair<guint32, guint16>> upgrades;
    std::map<std::string, GtkWidget *> geometry;
    std::function<double()> clock;
    std::function<Json()> context;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    guint timer = 0;
    std::uint64_t generation = 0;
    int page_height = 831;
    Impl(GtkWindow *host, std::string page, std::string style, bool render, const Json &fixture,
         std::function<double()> source_clock, std::function<Json()> source_context)
        : parent(host), history(page == "history" || page == "test-results"), rendering(render),
          theme(std::move(style)), clock(std::move(source_clock)),
          context(std::move(source_context)) {
        g_object_ref_sink(root);
        if (!clock)
            clock = [] { return g_get_monotonic_time() / 1e6; };
        if (!fixture.empty()) {
            if (fixture.value("purpose", "") != "public-typing-practice-dashboard")
                throw std::runtime_error("无效的公开跟打样例");
            for (const auto &a : Articles())
                if (a.at("id") == fixture.at("articleID"))
                    article = a;
            results = pt::HistoryStore::NormalizeFile(
                          {{"version", 1}, {"results", fixture.at("history")}})
                          .at("results");
            phase = fixture.at("phase");
        }
        Build();
        Bind();
        if (history)
            RefreshHistory();
        else
            Reset();
        if (!fixture.empty() && !history) {
            phase = fixture.at("phase");
            gtk_widget_set_sensitive(finish, phase == "live");
            if (fixture.contains("metrics")) {
                snapshot = {{"metrics", fixture.at("metrics")},
                            {"targetStates", fixture.at("targetStates")}};
                editor.Reset(fixture.at("editor"));
                RenderSnapshot();
                LabelText(status,
                          phase == "complete" ? "本次完成 · 成绩已保存" : "重练 · 从第一个字开始");
                gtk_button_set_label(GTK_BUTTON(start),
                                     phase == "complete" ? "同篇重练" : "重新开始");
                if (phase == "complete")
                    ShowResult(results.front());
            }
        }
        if (!rendering)
            Request({{"op", "status"}});
    }
    ~Impl() {
        // A compositor can destroy the window while a final telemetry read is
        // pending. Preserve a marked partial result and drain its queued write.
        if (stopping)
            stopping = false;
        Stop("interrupted", false);
        alive->store(false);
        ++generation;
        if (timer)
            g_source_remove(timer);
        telemetry.End();
        editor.Enable(false);
        cards.clear();
        result_cards.clear();
        chart.reset();
        gtk_widget_destroy(root);
        g_object_unref(root);
        // HistoryWorker drains queued aggregate writes after UI teardown.
    }
    double Now() const { return clock(); }
    void Put(GtkWidget *value, int x, int y, int width, int height, const std::string &name = {}) {
        gtk_widget_set_size_request(value, width, height);
        gtk_fixed_put(GTK_FIXED(canvas), value, x, y);
        if (!name.empty())
            geometry[name] = value;
    }
    void Height(int height) {
        page_height = height;
        gtk_widget_set_size_request(canvas, 698, height);
    }
    void Notice(const std::string &text) {
        LabelText(warning, text);
        gtk_widget_set_visible(warning, !text.empty());
    }
    void Build() {
        Add(root, canvas);
        Add(root, warning);
        gtk_widget_set_margin_start(warning, 26);
        gtk_widget_set_margin_end(warning, 26);
        gtk_widget_set_no_show_all(warning, TRUE);
        gtk_widget_hide(warning);
        gtk_label_set_line_wrap(GTK_LABEL(warning), TRUE);
        for (const auto &a : Articles())
            gtk_combo_box_text_append(
                GTK_COMBO_BOX_TEXT(article_picker), a.at("id").get<std::string>().c_str(),
                (a.at("title").get<std::string>() + "  ·  " + Length(a)).c_str());
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(article_picker),
                                    article.at("id").get<std::string>().c_str());
        Name(article_picker, "选择测速文章");
        Put(article_picker, 26, 24, 229, 20);
        article_detail = Class(Text(""), "practice-muted");
        Put(article_detail, 26, history ? 92 : 58, 646, 14);
        const int y = history ? 120 : 86;
        const char *titles[] = {"有效速度", "击键", "回退", "过程正确率"};
        const int icons[] = {3, 1, 4, 5};
        for (int i = 0; i < 4; ++i) {
            auto card = std::make_unique<MetricsGraphic>(MetricsGraphic::Kind::Card, theme, 92);
            card->Card(titles[i], "—", "", icons[i]);
            Put(card->Widget(), 26 + 164 * i, y, 154, 92, "card-" + std::to_string(i));
            cards.push_back(std::move(card));
        }
        chart = std::make_unique<MetricsGraphic>(MetricsGraphic::Kind::Trend, theme, 165);
        chart_title = Text(history ? "等待你的第一次成绩" : "速度轨迹", 12, true);
        Put(chart_title, 26, history ? 226 : 609, 646, 15);
        Put(chart->Widget(), 26, history ? 255 : 638, 646, 165, "trend");
        result_detail = Class(Text(""), "practice-muted");
        gtk_widget_set_no_show_all(result_detail, TRUE);
        Put(result_detail, 26, 711, 646, 14);
        const char *result_titles[] = {"最终字准", "回改", "并击拍数"};
        for (int i = 0; i < 3; ++i) {
            auto card = std::make_unique<MetricsGraphic>(MetricsGraphic::Kind::Card, theme, 92);
            card->Card(result_titles[i], "—", "", 6 + i);
            gtk_widget_set_no_show_all(card->Widget(), TRUE);
            Put(card->Widget(),
                i == 0   ? 26
                : i == 1 ? 245
                         : 464,
                609, i == 2 ? 208 : 209, 92, "result-" + std::to_string(i));
            result_cards.push_back(std::move(card));
        }
        if (history) {
            context_picker = Name(gtk_combo_box_text_new(), "筛选输入方案与文章版本");
            Put(context_picker, 26, 58, 646, 20);
            repair = Button("备份并重建…", [this] {
                if (!rendering && Confirm(parent, "备份并重建跟打成绩库？",
                                          "保留原路径条目，再创建空成绩库；日常统计不受影响。"))
                    Request({{"op", "repair"}, {"revision", revision}});
            });
            gtk_widget_set_no_show_all(repair, TRUE);
            Put(repair, 449, 24, 109, 20);
            clear = Name(Button("清空成绩…",
                                [this] {
                                    if (!rendering && Confirm(parent, "清空文章跟打成绩？",
                                                              "只删除本机文章跟打成绩，无法撤销。日"
                                                              "常统计与输入速度历史保持不变。"))
                                        Request({{"op", "clear"}, {"revision", revision}});
                                }),
                         "清空文章跟打成绩");
            Put(clear, 590, 24, 82, 20);
            Put(rows, 26, 434, 646, 1, "history-rows");
            chart->on_select = [this](const std::string &id) { SelectResult(id); };
        } else {
            auto *shuffle =
                Name(Button("",
                            [this] {
                                const auto index =
                                    gtk_combo_box_get_active(GTK_COMBO_BOX(article_picker));
                                const auto offset = g_random_int_range(1, Articles().size());
                                gtk_combo_box_set_active(GTK_COMBO_BOX(article_picker),
                                                         (index + offset) % Articles().size());
                            }),
                     "随机选择另一篇文章");
            auto *shuffle_icon = gtk_drawing_area_new();
            gtk_widget_set_size_request(shuffle_icon, 16, 13);
            g_signal_connect(shuffle_icon, "draw",
                             G_CALLBACK(+[](GtkWidget *widget, cairo_t *cr, gpointer) -> gboolean {
                                 GdkRGBA color;
                                 gtk_style_context_get_color(gtk_widget_get_style_context(widget),
                                                             gtk_widget_get_state_flags(widget),
                                                             &color);
                                 gdk_cairo_set_source_rgba(cr, &color);
                                 cairo_set_line_width(cr, 1.2);
                                 cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
                                 cairo_move_to(cr, 1, 3);
                                 cairo_line_to(cr, 4, 3);
                                 cairo_curve_to(cr, 7, 3, 8, 10, 11, 10);
                                 cairo_line_to(cr, 15, 10);
                                 cairo_move_to(cr, 12, 7);
                                 cairo_line_to(cr, 15, 10);
                                 cairo_line_to(cr, 12, 13);
                                 cairo_move_to(cr, 1, 10);
                                 cairo_line_to(cr, 4, 10);
                                 cairo_curve_to(cr, 7, 10, 8, 3, 11, 3);
                                 cairo_line_to(cr, 15, 3);
                                 cairo_move_to(cr, 12, 0);
                                 cairo_line_to(cr, 15, 3);
                                 cairo_line_to(cr, 12, 6);
                                 cairo_stroke(cr);
                                 return TRUE;
                             }),
                             nullptr);
            gtk_button_set_image(GTK_BUTTON(shuffle), shuffle_icon);
            gtk_button_set_always_show_image(GTK_BUTTON(shuffle), TRUE);
            Put(shuffle, 265, 24, 34, 20);
            start = Name(Button("开始跟打", [this] { Start(); }), "开始或重新跟打");
            finish = Name(Button("结束",
                                 [this] {
                                     if (editor.Composing()) {
                                         LabelText(status, "请先确认或取消当前组字，再结束");
                                         return;
                                     }
                                     Stop("", true);
                                 }),
                          "结束跟打");
            gtk_widget_set_focus_on_click(start, FALSE);
            gtk_widget_set_focus_on_click(finish, FALSE);
            Put(start, 544, 24, 72, 20);
            Put(finish, 626, 24, 46, 20);
            status = Class(Text("准备好后开始，第一枚按键触发计时", 11, true), "practice-muted");
            clock_label = Text("00:00", 15, true);
            gtk_label_set_xalign(GTK_LABEL(clock_label), 1);
            Put(status, 26, 195, 580, 14);
            Put(clock_label, 625, 192, 47, 19);
            progress = gtk_progress_bar_new();
            g_signal_connect(
                progress, "draw",
                G_CALLBACK(+[](GtkWidget *widget, cairo_t *cr, gpointer data) -> gboolean {
                    auto *self = static_cast<Impl *>(data);
                    const auto &theme = Visuals()
                                            .at("palettes")
                                            .at(self->theme == "liquidGlass" ? "day" : self->theme);
                    GdkRGBA color;
                    gdk_rgba_parse(&color, theme.at("border").get<std::string>().c_str());
                    gdk_cairo_set_source_rgba(cr, &color);
                    cairo_set_line_width(cr, 3);
                    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
                    const auto width = gtk_widget_get_allocated_width(widget);
                    cairo_move_to(cr, 1.5, 5.5);
                    cairo_line_to(cr, width - 1.5, 5.5);
                    cairo_stroke(cr);
                    const auto fraction = gtk_progress_bar_get_fraction(GTK_PROGRESS_BAR(widget));
                    if (fraction > 0) {
                        cairo_set_source_rgb(cr, .467, .471, .482);
                        cairo_move_to(cr, 1.5, 5.5);
                        cairo_line_to(cr, std::max(1.5, width * fraction - 1.5), 5.5);
                        cairo_stroke(cr);
                    }
                    return TRUE;
                }),
                this);
            percent = Text("0%", 11, true);
            gtk_label_set_xalign(GTK_LABEL(percent), 1);
            Put(progress, 26, 226, 618, 12, "progress");
            Put(percent, 645, 225, 27, 14);
            target = Class(gtk_text_view_new(), "practice-target");
            gtk_text_view_set_editable(GTK_TEXT_VIEW(target), FALSE);
            gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(target), FALSE);
            gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(target), GTK_WRAP_WORD_CHAR);
            gtk_text_view_set_left_margin(GTK_TEXT_VIEW(target), 19);
            gtk_text_view_set_right_margin(GTK_TEXT_VIEW(target), 19);
            gtk_text_view_set_top_margin(GTK_TEXT_VIEW(target), 12);
            gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(target), 12);
            gtk_text_view_set_pixels_inside_wrap(GTK_TEXT_VIEW(target), 0);
            gtk_text_view_set_pixels_below_lines(GTK_TEXT_VIEW(target), 10);
            Name(target, "跟打原文，绿色为正确，红色为待改，灰色为未输入");
            auto panel = [this](GtkWidget *text, const char *title, int top, int height,
                                const char *key) {
                auto *box = Class(Column(5), "practice-panel");
                auto *label = Text(title, 11, true);
                gtk_widget_set_margin_start(label, 14);
                Add(box, label);
                auto *scroll = gtk_scrolled_window_new(nullptr, nullptr);
                gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                               GTK_POLICY_AUTOMATIC);
                gtk_widget_set_size_request(scroll, -1, height - 19);
                gtk_container_add(GTK_CONTAINER(scroll), text);
                Add(box, scroll, true);
                Put(box, 26, top, 646, height, key);
            };
            panel(target, "原文", 253, 189, "target");
            panel(editor.Widget(), "输入", 456, 139, "editor");
        }
        Height(history ? 477 : 831);
    }
    void Bind() {
        g_signal_connect(article_picker, "changed",
                         G_CALLBACK(+[](GtkComboBox *picker, gpointer data) {
                             auto *s = static_cast<Impl *>(data);
                             if (s->updating)
                                 return;
                             s->Stop("interrupted", false);
                             const char *id = gtk_combo_box_get_active_id(picker);
                             if (!id)
                                 return;
                             for (const auto &article : Articles())
                                 if (article.at("id") == id)
                                     s->article = article;
                             if (s->history) {
                                 s->selected_context.clear();
                                 s->RefreshHistory();
                             } else
                                 s->Reset();
                         }),
                         this);
        if (context_picker)
            g_signal_connect(context_picker, "changed",
                             G_CALLBACK(+[](GtkComboBox *picker, gpointer data) {
                                 auto *s = static_cast<Impl *>(data);
                                 if (s->updating)
                                     return;
                                 const char *id = gtk_combo_box_get_active_id(picker);
                                 s->selected_context = id ? id : "";
                                 s->RefreshHistory(false);
                             }),
                             this);
        editor.key = [this](guint32 stamp, guint16 code, bool repeat, bool backspace,
                            bool composing) {
            if (!armed || stopping || !session)
                return;
            session->Key(Now(), repeat, backspace, composing);
            if (backspace)
                backspaces[{stamp, code}] = composing;
            Live();
        };
        editor.composition = [this] {
            if (armed && session)
                session->Start(Now());
        };
        editor.assistance = [this] {
            if (armed && session) {
                session->Mark("assistedInput");
                LabelText(status, "已阻止粘贴或辅助输入 · 本次记为练习");
            }
        };
        editor.before_blur = [this] {
            if (armed && session)
                session->Mark("focusLost");
        };
        editor.blur = [this] { Stop("focusLost", true); };
        editor.commit = [this](const std::string &text, auto insertion) {
            if (!armed || stopping || !session)
                return;
            if (!session->Commit(text, Now(), insertion)) {
                Stop("assistedInput", true);
                return;
            }
            Live();
            if (snapshot.value("canComplete", false))
                Stop("", true);
        };
    }
    void Request(Json command) {
        const auto op = command.at("op").get<std::string>();
        if (!worker.Request(std::move(command), [this, flag = alive, op](Json value) {
                Main([this, flag, op, value = std::move(value)] {
                    if (!flag->load())
                        return;
                    if (!value.value("ok", false)) {
                        Notice(value.value("message", "成绩未能保存，原文件已保留。"));
                        return;
                    }
                    results = value.at("file").at("results");
                    revision = value.at("revision");
                    const auto issue = value.at("issue").get<std::string>();
                    Notice(issue.empty() ? "" : "成绩存储暂不可用，原文件不会被覆盖。" + issue);
                    if (repair)
                        gtk_widget_set_visible(repair, !issue.empty());
                    if (clear)
                        gtk_widget_set_sensitive(clear, issue.empty() && !results.empty());
                    if (history)
                        RefreshHistory();
                    else if (op == "add" && !armed && !result.empty())
                        LabelText(status, result.at("isComplete").get<bool>()
                                              ? "本次完成 · 成绩已保存"
                                              : "提前结束 · 已保存为练习");
                });
            }))
            Notice("成绩正在处理，请稍后重试。");
    }
    void Reset() {
        ++generation;
        starting = armed = stopping = reading = false;
        if (timer) {
            g_source_remove(timer);
            timer = 0;
        }
        telemetry.End();
        session.reset();
        result = snapshot = Json::object();
        editor.Enable(false);
        editor.Reset();
        rendered_states = Json();
        phase = "ready";
        backspaces.clear();
        upgrades.clear();
        chords = 0;
        LabelText(article_detail, article.at("theme").get<std::string>() + "  ·  " +
                                      article.at("difficulty").get<std::string>() + "  ·  " +
                                      Length(article));
        LabelText(status, "准备好后开始，第一枚按键触发计时");
        LabelText(clock_label, "00:00");
        gtk_button_set_label(GTK_BUTTON(start), "开始跟打");
        gtk_widget_set_sensitive(finish, FALSE);
        gtk_widget_set_sensitive(start, TRUE);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress), 0);
        LabelText(percent, "0%");
        HideResult();
        Cards({});
        RenderTarget(Json::array());
        chart->Trend({}, Unit(), "用时", "开始输入后，速度曲线会在这里生长", "");
    }
    std::string Unit() const { return article.at("language") == "english" ? "WPM" : "字/分"; }
    void Start() {
        if (rendering || starting || stopping)
            return;
        Stop("interrupted", false);
        Reset();
        starting = true;
        gtk_widget_set_sensitive(start, FALSE);
        // Focus the native editor before asking Fcitx for a context-bound lease.
        editor.PrepareInputContext();
        gtk_widget_grab_focus(editor.Widget());
        const auto current = generation;
        auto begin = [this, flag = alive, current](Json reply) {
            if (!flag->load() || current != generation)
                return;
            starting = false;
            gtk_widget_set_sensitive(start, TRUE);
            if (!context && !gtk_widget_has_focus(editor.Widget())) {
                telemetry.End();
                editor.Enable(false);
                return;
            }
            Json state = reply.value("context", Json::object());
            if (!reply.value("ok", false) || !state.is_object() || !state.contains("schemaID")) {
                state = {{"schemaID", "unknown"},
                         {"inputSourceID", "gtk.unknown"},
                         {"chordCountingAvailable", false}};
                Notice("输入状态暂不可用，仍可跟打；本次不计入可比较成绩，并击数据留空。");
            }
            state["mode"] = "firstAttempt";
            for (const auto &row : results)
                if (row.at("articleID") == article.at("id") &&
                    row.at("articleVersion") == article.at("version") &&
                    row.at("context").at("schemaID") == state.at("schemaID") &&
                    row.at("context").value("keymapRevision", "") ==
                        state.value("keymapRevision", "") &&
                    row.at("context").value("inputSourceID", "") ==
                        state.value("inputSourceID", ""))
                    state["mode"] = "practice";
            frozen = state;
            auto *uuid = g_uuid_string_random();
            session = std::make_unique<pt::Session>(article, state, uuid);
            g_free(uuid);
            if (!reply.value("ok", false))
                session->Mark("interrupted");
            armed = true;
            phase = "live";
            editor.Enable(true);
            gtk_widget_grab_focus(editor.Widget());
            gtk_button_set_label(GTK_BUTTON(start), "重新开始");
            gtk_widget_set_sensitive(finish, TRUE);
            LabelText(status, state.at("mode") == "practice" ? "重练 · 从第一个字开始"
                                                             : "首次挑战 · 从第一个字开始");
            timer = g_timeout_add(
                250,
                +[](gpointer p) -> gboolean {
                    auto *s = static_cast<Impl *>(p);
                    s->Live();
                    s->ReadTelemetry();
                    return G_SOURCE_CONTINUE;
                },
                this);
        };
        if (context)
            begin({{"ok", true}, {"context", context()}});
        else {
            // Let GTK send FocusIn before Fcitx snapshots its focused context.
            Main([this, flag = alive, current, begin = std::move(begin)]() mutable {
                if (flag->load() && current == generation)
                    telemetry.Begin(std::move(begin));
            });
        }
    }
    void ReadTelemetry() {
        if (!armed || stopping || reading || !telemetry.Active())
            return;
        reading = true;
        const auto current = generation;
        telemetry.Read(false, [this, flag = alive, current](Json reply) {
            if (!flag->load() || current != generation)
                return;
            reading = false;
            if (stopping || !armed)
                return;
            if (!ApplyTelemetry(reply, Now())) {
                Stop("interrupted", false);
                return;
            }
            const auto reason = reply.value("reason", "");
            if (!reason.empty())
                Stop(reason, true);
        });
    }
    bool ApplyTelemetry(const Json &reply, double at) {
        try {
            if (!reply.value("ok", false))
                return false;
            const auto n = reply.at("chords").get<int>();
            if (n < chords || n > pt::kMaximumCounter || n - chords > 4096)
                return false;
            while (chords < n) {
                session->Chord(at);
                ++chords;
            }
            for (const auto &item : reply.at("backspaces")) {
                const auto id =
                    std::make_pair(item.at("time").get<guint32>(), item.at("code").get<guint16>());
                const auto found = backspaces.find(id);
                if (found != backspaces.end() && !found->second && upgrades.insert(id).second)
                    session->UpgradeBackspace();
            }
            return true;
        } catch (...) {
            return false;
        }
    }
    void Live() {
        if (armed && !stopping && session) {
            snapshot = session->Snapshot(Now());
            RenderSnapshot();
        }
    }
    void Stop(const std::string &reason, bool drain) {
        if (starting) {
            ++generation;
            starting = false;
            telemetry.End();
        }
        if (!armed || stopping || !session)
            return;
        if (!reason.empty())
            session->Mark(reason);
        stopping = true;
        if (timer) {
            g_source_remove(timer);
            timer = 0;
        }
        // Keep the GTK input context focused until Fcitx has captured the final
        // counters. Read-only GTK would send FocusOut and mark a normal finish.
        editor.PrepareInputContext();
        gtk_widget_set_sensitive(finish, FALSE);
        const auto at = Now();
        const auto current = generation;
        auto complete = [this, flag = alive, at, current](bool known) {
            if (!flag->load() || current != generation || !session)
                return;
            if (!known && frozen.value("chordCountingAvailable", false))
                session->Mark("interrupted");
            const auto value = session->Finish(at, g_get_real_time() / 1e6);
            armed = stopping = false;
            telemetry.End();
            editor.Enable(false);
            gtk_button_set_label(GTK_BUTTON(start), "同篇重练");
            if (!value) {
                session->Cancel();
                LabelText(status, "尚未输入，不产生空成绩");
                return;
            }
            result = *value;
            if (!known) {
                result["context"]["chordCountingAvailable"] = false;
                result["metrics"].erase("chordCount");
            }
            snapshot = session->Snapshot(at);
            snapshot["metrics"] = result.at("metrics");
            RenderSnapshot();
            ShowResult(result);
            LabelText(status, "本次结束 · 正在保存成绩");
            Request({{"op", "add"}, {"result", result}});
        };
        if (drain && telemetry.Active())
            telemetry.Read(true, [this, flag = alive, current, at, complete](Json reply) {
                if (!flag->load() || current != generation)
                    return;
                const bool known = ApplyTelemetry(reply, at);
                const auto reason = reply.value("reason", "");
                if (!reason.empty())
                    session->Mark(reason);
                complete(known);
            });
        else
            complete(bool(context) || !frozen.value("chordCountingAvailable", false));
    }
    void Cards(const Json &metrics) {
        const bool present = !metrics.empty();
        const auto rates = present ? pt::Rates(metrics) : Json::object();
        cards[0]->Card(
            "有效速度",
            present ? Number(rates.at(Unit() == "WPM" ? "wordsPerMinute" : "effectiveCPM")) : "—",
            Unit(), 3);
        cards[1]->Card("击键",
                       present ? std::to_string(metrics.at("physicalKeyCount").get<int>()) : "—",
                       present ? "键 · " + Number(rates.at("keysPerSecond")) + "/秒" : "键", 1);
        cards[2]->Card("回退",
                       present ? std::to_string(metrics.at("backspaceCount").get<int>()) : "—",
                       "次", 4);
        cards[3]->Card("过程正确率",
                       present && !rates.at("processAccuracy").is_null()
                           ? Number(100 * rates.at("processAccuracy").get<double>())
                           : "—",
                       "%", 5);
        if (present)
            gtk_widget_set_tooltip_text(
                cards[2]->Widget(),
                ("组字退格 " + std::to_string(metrics.at("compositionBackspaceCount").get<int>()) +
                 " · 上屏退格 " + std::to_string(metrics.at("committedBackspaceCount").get<int>()) +
                 " · 删除 " + std::to_string(metrics.at("deletedCharacterCount").get<int>()) +
                 " 字符")
                    .c_str());
    }
    void RenderSnapshot() {
        const auto &m = snapshot.at("metrics");
        Cards(m);
        const auto rates = pt::Rates(m);
        LabelText(clock_label, Duration(m.at("elapsedSeconds")));
        const auto ratio = rates.at("progress").get<double>();
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress), ratio);
        const auto percent_text = std::to_string(int(std::round(ratio * 100))) + "%";
        LabelText(percent, percent_text);
        const int width = percent_text.size() == 2 ? 618 : percent_text.size() == 3 ? 610 : 603;
        gtk_widget_set_size_request(progress, width, 12);
        gtk_fixed_move(GTK_FIXED(canvas), percent, 26 + width + 10, 225);
        gtk_widget_set_size_request(percent, 636 - width, 14);
        RenderTarget(snapshot.at("targetStates"));
        std::vector<MetricSample> samples;
        for (const auto &sample : m.at("speedSamples")) {
            const double elapsed = sample.at("elapsedSeconds");
            const double speed = sample.at("effectiveCPM");
            samples.push_back({std::to_string(samples.size()), Duration(elapsed), "",
                               Unit() == "WPM" ? speed / 5 : speed, elapsed});
        }
        chart->Trend(std::move(samples), Unit(), "用时", "开始输入后，速度曲线会在这里生长", "");
    }
    void RenderTarget(const Json &states) {
        if (rendered_states == states)
            return;
        rendered_states = states;
        auto *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(target));
        const std::string text = article.at("text");
        gtk_text_buffer_set_text(buffer, text.c_str(), text.size());
        const auto &palette = Visuals().at("palettes").at(theme == "liquidGlass" ? "day" : theme);
        auto *table = gtk_text_buffer_get_tag_table(buffer);
        for (const auto &pair :
             std::vector<std::pair<const char *, const char *>>{{"correct", "accentText"},
                                                                {"pending", "textMuted"},
                                                                {"incorrect", "dangerText"},
                                                                {"omitted", "dangerText"}}) {
            auto *tag = gtk_text_tag_table_lookup(table, pair.first);
            if (!tag)
                tag = gtk_text_buffer_create_tag(buffer, pair.first, nullptr);
            const auto color = palette.value(pair.second, "#e86565");
            g_object_set(tag, "foreground", color.c_str(), nullptr);
        }
        int offset = 0, pending = -1;
        const auto characters = pt::Characters(text);
        for (std::size_t i = 0; i < characters.size(); ++i) {
            const int length = g_utf8_strlen(characters[i].text.c_str(), characters[i].text.size());
            GtkTextIter a, b;
            gtk_text_buffer_get_iter_at_offset(buffer, &a, offset);
            gtk_text_buffer_get_iter_at_offset(buffer, &b, offset + length);
            const auto state = i < states.size() ? states[i].get<std::string>() : "pending";
            gtk_text_buffer_apply_tag_by_name(buffer, state.c_str(), &a, &b);
            if (state == "pending" && pending < 0)
                pending = offset;
            offset += length;
        }
        if (pending > 0) {
            GtkTextIter at;
            gtk_text_buffer_get_iter_at_offset(buffer, &at, pending);
            gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(target), &at, 0, FALSE, 0, 0);
        }
    }
    void HideResult() {
        result = Json::object();
        for (auto &card : result_cards)
            gtk_widget_hide(card->Widget());
        gtk_widget_hide(result_detail);
        if (!history) {
            gtk_fixed_move(GTK_FIXED(canvas), chart_title, 26, 609);
            gtk_fixed_move(GTK_FIXED(canvas), chart->Widget(), 26, 638);
            Height(831);
        }
    }
    void ShowResult(const Json &row) {
        result = row;
        const auto &m = row.at("metrics");
        const auto rates = pt::Rates(m);
        const int top = history ? 434 : 609;
        const char *titles[] = {"最终字准", "回改", "并击拍数"};
        const std::string values[] = {
            rates.at("finalAccuracy").is_null()
                ? "—"
                : Number(100 * rates.at("finalAccuracy").get<double>()),
            std::to_string(m.at("correctionCount").get<int>()),
            m.contains("chordCount") ? std::to_string(m.at("chordCount").get<int>()) : "—"};
        for (int i = 0; i < 3; ++i) {
            result_cards[i]->Card(titles[i], values[i], i == 0 ? "%" : i == 1 ? "次" : "拍", i + 6);
            gtk_fixed_move(GTK_FIXED(canvas), result_cards[i]->Widget(),
                           i == 0   ? 26
                           : i == 1 ? 245
                                    : 464,
                           top);
            gtk_widget_show(result_cards[i]->Widget());
        }
        LabelText(result_detail,
                  "错 " + std::to_string(m.at("substitutionCount").get<int>()) + " · 漏 " +
                      std::to_string(m.at("omissionCount").get<int>()) + " · 多 " +
                      std::to_string(m.at("extraCharacterCount").get<int>()) + "　｜　" +
                      row.at("context").at("schemaID").get<std::string>() + "　｜　" +
                      (row.at("context").at("mode") == "firstAttempt" ? "首次" : "重练") +
                      (row.at("practiceReasons").empty() ? "" : " · 非正式练习"));
        gtk_fixed_move(GTK_FIXED(canvas), result_detail, 26, top + 102);
        gtk_widget_show(result_detail);
        if (history) {
            gtk_fixed_move(GTK_FIXED(canvas), rows, 26, 564);
            Height(HistoryHeight() + 130);
        } else {
            phase = "complete";
            gtk_fixed_move(GTK_FIXED(canvas), chart_title, 26, 739);
            gtk_fixed_move(GTK_FIXED(canvas), chart->Widget(), 26, 768);
            Height(961);
        }
    }
    int HistoryHeight() const {
        auto *children = gtk_container_get_children(GTK_CONTAINER(rows));
        const auto count = g_list_length(children);
        g_list_free(children);
        return std::max(477, 434 + int(count) * 42 - 10 + 28);
    }
    void RefreshHistory(bool contexts = true) {
        updating = true;
        std::vector<Json> filtered;
        if (contexts) {
            gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(context_picker));
            std::set<std::string> seen;
            for (const auto &row : results)
                if (row.at("articleID") == article.at("id")) {
                    const auto key = pt::ComparisonKey(row);
                    if (!seen.insert(key).second)
                        continue;
                    const auto text = row.at("context").at("schemaID").get<std::string>() +
                                      " · 文章 v" +
                                      std::to_string(row.at("articleVersion").get<int>()) +
                                      (row.at("context").contains("keymapRevision")
                                           ? " · " + row.at("context")
                                                         .at("keymapRevision")
                                                         .get<std::string>()
                                                         .substr(0, 6)
                                           : "");
                    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(context_picker), key.c_str(),
                                              text.c_str());
                }
            if (!seen.count(selected_context))
                selected_context.clear();
            if (seen.empty())
                gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(context_picker), "",
                                          "暂无输入方案记录");
            if (selected_context.empty())
                gtk_combo_box_set_active(GTK_COMBO_BOX(context_picker), 0);
            else
                gtk_combo_box_set_active_id(GTK_COMBO_BOX(context_picker),
                                            selected_context.c_str());
            const char *selected = gtk_combo_box_get_active_id(GTK_COMBO_BOX(context_picker));
            selected_context = selected ? selected : "";
        }
        for (const auto &row : results)
            if (row.at("articleID") == article.at("id") &&
                pt::ComparisonKey(row) == selected_context)
                filtered.push_back(row);
        LabelText(article_detail, article.at("theme").get<std::string>() + " · " + Length(article) +
                                      " · " + std::to_string(filtered.size()) + " 次记录");
        Cards(filtered.empty() ? Json::object() : filtered.front().at("metrics"));
        HideResult();
        const int complete = std::count_if(filtered.begin(), filtered.end(), [](const Json &row) {
            return row.at("isComplete").get<bool>();
        });
        LabelText(chart_title, filtered.empty()
                                   ? "等待你的第一次成绩"
                                   : "最近成绩 · " + std::to_string(complete) + " 次完成");
        if (filtered.size() > 30)
            filtered.resize(30);
        std::vector<MetricSample> samples;
        for (auto it = filtered.rbegin(); it != filtered.rend(); ++it) {
            const auto &row = *it;
            const auto rates = pt::Rates(row.at("metrics"));
            samples.push_back(
                {row.at("id"), Date(row.at("completedAt")),
                 Comparable(row) ? "正式成绩" : "练习，不连入曲线",
                 Comparable(row) ? std::optional<double>(rates.at(Unit() == "WPM" ? "wordsPerMinute"
                                                                                  : "effectiveCPM"))
                                 : std::nullopt});
        }
        chart->Trend(std::move(samples), Unit(), "测次", "这篇文章还没有成绩，去完成第一次跟打吧",
                     "");
        Clear(rows);
        for (const auto &row : filtered) {
            const auto id = row.at("id").get<std::string>();
            const auto rates = pt::Rates(row.at("metrics"));
            auto *button =
                Class(Button("", [this, id] { SelectResult(id); }), "practice-history-row");
            gtk_container_remove(GTK_CONTAINER(button), gtk_bin_get_child(GTK_BIN(button)));
            auto *content = Row(6);
            Add(content, ScoreIcon(Comparable(row), theme));
            Add(content, Text(Number(rates.at(Unit() == "WPM" ? "wordsPerMinute" : "effectiveCPM")),
                              22, true));
            Add(content, Class(Text(Unit(), 10), "practice-muted"));
            auto *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
            Add(content, spacer, true);
            const auto caption =
                Date(row.at("completedAt")) + "  ·  " +
                (row.at("context").at("mode") == "firstAttempt" ? "首次" : "重练") +
                (row.at("practiceReasons").empty() ? "" : " · 练习");
            Add(content, Class(Text(caption), "practice-muted"));
            Add(content, Text((rates.at("processAccuracy").is_null()
                                   ? "—"
                                   : Number(100 * rates.at("processAccuracy").get<double>())) +
                                  "%",
                              13));
            gtk_container_add(GTK_CONTAINER(button), content);
            Name(button, "查看成绩 " + id);
            gtk_widget_set_size_request(button, 646, 32);
            Add(rows, button);
        }
        if (filtered.empty())
            Add(rows, Class(Text("在「文章跟打」完成练习后，这里会显示速度趋势与成绩。", 12),
                            "practice-muted"));
        gtk_widget_show_all(rows);
        gtk_fixed_move(GTK_FIXED(canvas), rows, 26, 434);
        Height(HistoryHeight());
        updating = false;
    }
    void SelectResult(const std::string &id) {
        for (const auto &row : results)
            if (row.at("id") == id && row.at("articleID") == article.at("id") &&
                pt::ComparisonKey(row) == selected_context) {
                chart->Highlight(id);
                Cards(row.at("metrics"));
                LabelText(
                    chart_title,
                    Date(row.at("completedAt")) + " · " +
                        (row.at("context").at("mode") == "firstAttempt" ? "首次挑战" : "同篇重练"));
                ShowResult(row);
                return;
            }
    }
};
PracticePane::PracticePane(GtkWindow *parent, std::string page, std::string theme, bool render,
                           const Json &fixture, std::function<double()> clock,
                           std::function<Json()> context)
    : impl_(std::make_unique<Impl>(parent, std::move(page), std::move(theme), render, fixture,
                                   std::move(clock), std::move(context))) {}
PracticePane::~PracticePane() = default;
GtkWidget *PracticePane::Widget() const { return impl_->root; }
PracticeEditor &PracticePane::Editor() { return impl_->editor; }
Json PracticePane::Snapshot() const {
    return {{"armed", impl_->armed},       {"starting", impl_->starting},
            {"stopping", impl_->stopping}, {"snapshot", impl_->snapshot},
            {"result", impl_->result},     {"results", impl_->results},
            {"context", impl_->frozen}};
}
bool PracticePane::CanLeave() {
    if (impl_->stopping)
        return false;
    impl_->Stop("interrupted", false);
    return true;
}
Json PracticePane::Geometry() const {
    Json result;
    for (const auto &entry : impl_->geometry)
        if (gtk_widget_get_visible(entry.second)) {
            GtkAllocation box;
            gtk_widget_get_allocation(entry.second, &box);
            int x = 0, y = 0;
            gtk_widget_translate_coordinates(entry.second, impl_->root, 0, 0, &x, &y);
            result[entry.first] = {
                {"x", x}, {"y", y}, {"width", box.width}, {"height", box.height}};
        }
    result["page"] = {{"width", gtk_widget_get_allocated_width(impl_->root)},
                      {"height", gtk_widget_get_allocated_height(impl_->root)}};
    return result;
}
} // namespace rimes::desktop
