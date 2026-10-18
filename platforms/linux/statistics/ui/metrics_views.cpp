#include "metrics_core.hpp"
#include "metrics_views.hpp"
#include "native_metrics_contract.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>

namespace rimes::desktop {
namespace {
GdkRGBA Color(const std::string& theme, const char* key, double alpha = 1) {
    GdkRGBA color{};
    gdk_rgba_parse(&color, Visuals()
                               .at("palettes")
                               .at(theme == "liquidGlass" ? "day" : theme)
                               .at(key)
                               .get<std::string>()
                               .c_str());
    color.alpha *= alpha;
    return color;
}
void Ink(cairo_t* cr, GdkRGBA c) { gdk_cairo_set_source_rgba(cr, &c); }
GdkRGBA Blend(GdkRGBA a, GdkRGBA b, double amount) {
    return {a.red + (b.red - a.red) * amount, a.green + (b.green - a.green) * amount,
            a.blue + (b.blue - a.blue) * amount, 1};
}
void Round(cairo_t* cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}
double Text(cairo_t* cr, const std::string& value, double x, double y, double size, GdkRGBA color,
            double width = -1, PangoAlignment align = PANGO_ALIGN_LEFT, bool bold = false,
            bool mono = false, bool fit = false) {
    auto* layout = pango_cairo_create_layout(cr);
    auto* font = pango_font_description_new();
    pango_font_description_set_family(font, mono ? "SF Pro Display,Inter,Noto Sans CJK SC"
                                                 : "SF Pro Text,Inter,Noto Sans CJK SC");
    pango_font_description_set_absolute_size(font, size * PANGO_SCALE);
    pango_font_description_set_weight(font, bold ? PANGO_WEIGHT_SEMIBOLD : PANGO_WEIGHT_NORMAL);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, value.c_str(), -1);
    if (fit && width > 0) {
        int measured = 0;
        pango_layout_get_pixel_size(layout, &measured, nullptr);
        while (size > 6 && measured > width) {
            size = std::max(6., size - .5);
            pango_font_description_set_absolute_size(font, size * PANGO_SCALE);
            pango_layout_set_font_description(layout, font);
            pango_layout_get_pixel_size(layout, &measured, nullptr);
        }
    }
    pango_font_description_free(font);
    if (width >= 0) {
        pango_layout_set_width(layout, int(width * PANGO_SCALE));
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_alignment(layout, align);
    }
    if (mono) {
        auto* attributes = pango_attr_list_new();
        pango_attr_list_insert(attributes, pango_attr_font_features_new("tnum"));
        pango_layout_set_attributes(layout, attributes);
        pango_attr_list_unref(attributes);
    }
    int actual = 0, height = 0;
    pango_layout_get_pixel_size(layout, &actual, &height);
    Ink(cr, color);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
    return actual;
}
std::string Number(double value, int places = 0) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(places) << value;
    return out.str();
}
double Nice(double value) {
    if (value <= 0)
        return 1;
    const auto power = std::pow(10, std::floor(std::log10(value)));
    const auto ratio = value / power;
    return power * (ratio <= 1 ? 1 : ratio <= 2 ? 2 : ratio <= 5 ? 5 : 10);
}
std::int64_t Total(const Json& counts) {
    std::int64_t result = 0;
    for (const auto& n : counts)
        result += n.get<std::int64_t>();
    return result;
}
} // namespace
const Json& MetricsContract() {
    static const auto contract = Json::parse(rimes::native::kMetricsContract);
    return contract;
}
MetricsGraphic::MetricsGraphic(Kind kind, std::string theme, int height)
    : kind_(kind), theme_(std::move(theme)), widget_(gtk_drawing_area_new()) {
    g_object_ref_sink(widget_);
    gtk_widget_set_size_request(widget_, kind == Kind::Card ? 155 : 650, height);
    gtk_widget_set_can_focus(widget_, kind != Kind::Card);
    gtk_widget_add_events(widget_, GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK |
                                       GDK_KEY_PRESS_MASK | GDK_LEAVE_NOTIFY_MASK);
    g_signal_connect(
        widget_, "motion-notify-event",
        G_CALLBACK(+[](GtkWidget* widget, GdkEventMotion* event, gpointer data) -> gboolean {
            auto* self = static_cast<MetricsGraphic*>(data);
            std::string hovered;
            for (const auto& hit : self->hits_)
                if (event->x >= hit.x && event->x < hit.x + hit.w && event->y >= hit.y &&
                    event->y < hit.y + hit.h) {
                    hovered = hit.id;
                    break;
                }
            if (hovered != self->hovered_) {
                self->hovered_ = hovered;
                gtk_widget_queue_draw(widget);
            }
            return FALSE;
        }),
        this);
    g_signal_connect(
        widget_, "leave-notify-event",
        G_CALLBACK(+[](GtkWidget* widget, GdkEventCrossing*, gpointer data) -> gboolean {
            static_cast<MetricsGraphic*>(data)->hovered_.clear();
            gtk_widget_queue_draw(widget);
            return FALSE;
        }),
        this);
    g_signal_connect(widget_, "draw",
                     G_CALLBACK(+[](GtkWidget*, cairo_t* cr, gpointer data) -> gboolean {
                         static_cast<MetricsGraphic*>(data)->Draw(cr);
                         return TRUE;
                     }),
                     this);
    g_signal_connect(
        widget_, "button-press-event",
        G_CALLBACK(+[](GtkWidget* widget, GdkEventButton* event, gpointer data) -> gboolean {
            if (event->button != 1)
                return FALSE;
            auto* self = static_cast<MetricsGraphic*>(data);
            gtk_widget_grab_focus(widget);
            for (const auto& hit : self->hits_)
                if (event->x >= hit.x && event->x < hit.x + hit.w && event->y >= hit.y &&
                    event->y < hit.y + hit.h) {
                    self->Select(hit.id);
                    return TRUE;
                }
            return FALSE;
        }),
        this);
    gtk_widget_set_has_tooltip(widget_, TRUE);
    g_signal_connect(widget_, "query-tooltip",
                     G_CALLBACK(+[](GtkWidget*, gint x, gint y, gboolean keyboard, GtkTooltip* tip,
                                    gpointer data) -> gboolean {
                         auto* self = static_cast<MetricsGraphic*>(data);
                         for (const auto& hit : self->hits_)
                             if ((keyboard && hit.id == self->selected_) ||
                                 (!keyboard && x >= hit.x && x < hit.x + hit.w && y >= hit.y &&
                                  y < hit.y + hit.h)) {
                                 gtk_tooltip_set_text(tip, hit.detail.c_str());
                                 return TRUE;
                             }
                         return FALSE;
                     }),
                     this);
    g_signal_connect(widget_, "key-press-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventKey* event, gpointer data) -> gboolean {
                         auto* self = static_cast<MetricsGraphic*>(data);
                         if (self->hits_.empty())
                             return FALSE;
                         int delta = 0;
                         if (event->keyval == GDK_KEY_Left)
                             delta = self->kind_ == Kind::Calendar ? -7 : -1;
                         else if (event->keyval == GDK_KEY_Right)
                             delta = self->kind_ == Kind::Calendar ? 7 : 1;
                         else if (event->keyval == GDK_KEY_Up)
                             delta = -1;
                         else if (event->keyval == GDK_KEY_Down)
                             delta = 1;
                         else if (event->keyval != GDK_KEY_Home && event->keyval != GDK_KEY_End)
                             return FALSE;
                         auto found =
                             std::find_if(self->hits_.begin(), self->hits_.end(),
                                          [&](const auto& h) { return h.id == self->selected_; });
                         int index =
                             found == self->hits_.end() ? 0 : int(found - self->hits_.begin());
                         if (event->keyval == GDK_KEY_Home)
                             index = 0;
                         else if (event->keyval == GDK_KEY_End)
                             index = int(self->hits_.size()) - 1;
                         else
                             index = std::clamp(index + delta, 0, int(self->hits_.size()) - 1);
                         self->Select(self->hits_[index].id);
                         return TRUE;
                     }),
                     this);
}
MetricsGraphic::~MetricsGraphic() {
    g_signal_handlers_disconnect_by_data(widget_, this);
    gtk_widget_destroy(widget_);
    g_object_unref(widget_);
}
void MetricsGraphic::Describe(const std::string& title, const std::string& detail) {
    auto* access = gtk_widget_get_accessible(widget_);
    atk_object_set_name(access, title.c_str());
    atk_object_set_description(access, detail.c_str());
}
void MetricsGraphic::Select(const std::string& id) {
    selected_ = id;
    for (const auto& hit : hits_)
        if (hit.id == id) {
            Describe(title_, hit.detail);
            auto* parent = gtk_widget_get_parent(widget_);
            if (GTK_IS_VIEWPORT(parent)) {
                auto* adjustment = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(parent));
                gtk_adjustment_clamp_page(adjustment, hit.x, hit.x + hit.w);
            }
            break;
        }
    gtk_widget_queue_draw(widget_);
    if (on_select)
        on_select(id);
}
void MetricsGraphic::Card(std::string title, std::string value, std::string unit, int icon) {
    title_ = std::move(title);
    value_ = std::move(value);
    unit_ = std::move(unit);
    icon_ = icon;
    Describe(title_, value_ + " " + unit_);
    gtk_widget_queue_draw(widget_);
}
void MetricsGraphic::Trend(std::vector<MetricSample> samples, std::string unit, std::string axis,
                           std::string empty, std::string selected) {
    samples_ = std::move(samples);
    unit_ = std::move(unit);
    axis_ = std::move(axis);
    empty_ = std::move(empty);
    selected_ = std::move(selected);
    title_ = "统计趋势 · " + unit_;
    std::string detail;
    for (const auto& sample : samples_)
        detail += sample.label + " " + (sample.value ? Number(*sample.value, 1) : "未记录") + "; ";
    Describe(title_, detail);
    gtk_widget_queue_draw(widget_);
}
void MetricsGraphic::Keyboard(const Json& counts) {
    counts_ = counts;
    title_ = "键盘分布";
    std::string detail;
    for (const auto& key : MetricsContract().at("keyboard")) {
        const std::string id = key.at("id");
        const auto n = counts_.value(id, std::int64_t(0));
        if (n)
            detail += key.at("label").get<std::string>() + " " + std::to_string(n) + " 次; ";
    }
    Describe(title_, detail.empty() ? "尚无按键记录" : detail);
    gtk_widget_queue_draw(widget_);
}
void MetricsGraphic::Calendar(const Json& days, const std::string& selected) {
    counts_ = Json::object();
    days_.clear();
    selected_ = selected;
    title_ = "按键日历 · 全部历史";
    for (const auto& day : days.items())
        counts_[day.key()] = Total(day.value().at("keys"));
    int width = 650;
    if (!counts_.empty()) {
        const auto first = *rimes::metrics::DaySerial(counts_.begin().key());
        const auto last = *rimes::metrics::DaySerial(std::prev(counts_.end()).key());
        monday_ = int((first + 3) % 7);
        if (monday_ < 0)
            monday_ += 7;
        for (auto day = first; day <= last; ++day)
            days_.push_back(rimes::metrics::DayFromSerial(day));
        width = std::max(650, 44 + int((days_.size() + monday_ + 6) / 7) * 15 - 3);
    }
    gtk_widget_set_size_request(widget_, width, 164);
    Describe(title_,
             std::to_string(counts_.size()) + " 个记录日；方向键选择日期，回车查看日期详情。");
    gtk_widget_queue_draw(widget_);
}
void MetricsGraphic::Draw(cairo_t* cr) {
    const double w = gtk_widget_get_allocated_width(widget_),
                 h = gtk_widget_get_allocated_height(widget_);
    const auto surface = Color(theme_, "surfaceSecondary"), border = Color(theme_, "border"),
               primary = Color(theme_, "textPrimary"), secondary = Color(theme_, "textSecondary"),
               muted = Color(theme_, "textMuted"), accent = Color(theme_, "accentGreen");
    hits_.clear();
    if (kind_ == Kind::Card || kind_ == Kind::Trend) {
        Round(cr, .5, .5, w - 1, h - 1, 12);
        Ink(cr, surface);
        cairo_fill_preserve(cr);
        Ink(cr, border);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
    }
    if (kind_ == Kind::Card) {
        // Symbol footprints and text baselines follow MetricsValueCard.
        const auto y = (h - 57) / 2;
        Ink(cr, accent);
        cairo_set_line_width(cr, 1.2);
        if (icon_ == 0)
            Text(cr, "Aa", 13, y - 1, 13, accent, 17);
        else if (icon_ == 1) {
            Round(cr, 13, y + 2, 16, 10, 2);
            cairo_stroke(cr);
            for (int row = 0; row < 2; ++row)
                for (int col = 0; col < 4; ++col) {
                    cairo_rectangle(cr, 16 + col * 3, y + 5 + row * 3, 1, 1);
                    cairo_fill(cr);
                }
        } else {
            cairo_arc(cr, 21, y + 7, 7, icon_ == 3 ? M_PI : 0, 2 * M_PI);
            cairo_stroke(cr);
            cairo_move_to(cr, 21, y + 7);
            cairo_line_to(cr, icon_ == 3 ? 25 : 21, y + 3);
            cairo_stroke(cr);
            if (icon_ == 2) {
                cairo_move_to(cr, 21, y + 7);
                cairo_line_to(cr, 25, y + 9);
                cairo_stroke(cr);
            }
        }
        Text(cr, title_, 36, y, 11, secondary, w - 43, PANGO_ALIGN_LEFT, true);
        const auto value_width =
            Text(cr, value_, 13, y + 25, 27, primary, -1, PANGO_ALIGN_LEFT, true, true);
        Text(cr, unit_, 13 + value_width + 3, y + 41, 10, muted);
    } else if (kind_ == Kind::Trend) {
        const double x = 55, y = 30, pw = w - 76, ph = h - 64;
        double maximum = 0;
        bool any = false;
        for (const auto& s : samples_)
            if (s.value) {
                maximum = std::max(maximum, *s.value);
                any = true;
            }
        maximum = Nice(maximum);
        for (int i = 0; i < 3; ++i) {
            const auto py = y + ph - ph * i / 2;
            Ink(cr, border);
            cairo_set_line_width(cr, .7);
            cairo_move_to(cr, x, py);
            cairo_line_to(cr, x + pw, py);
            cairo_stroke(cr);
            Text(cr, Number(maximum * i / 2), 7, py - 7, 9, muted, 39, PANGO_ALIGN_RIGHT, false,
                 true);
        }
        Text(cr, unit_, 16, 10, 10, muted);
        Text(cr, axis_, x, 10, 10, muted, pw, PANGO_ALIGN_RIGHT);
        auto px = [&](std::size_t i) {
            return samples_.size() <= 1 ? x + pw / 2 : x + pw * i / (samples_.size() - 1);
        };
        if (!samples_.empty()) {
            for (auto i : std::set<std::size_t>{0, (samples_.size() - 1) / 2, samples_.size() - 1})
                Text(cr, samples_[i].label, std::clamp(px(i) - 35, 4.0, w - 80), y + ph + 9, 9,
                     muted, 70, PANGO_ALIGN_CENTER);
        }
        bool previous = false;
        for (std::size_t i = 0; i < samples_.size(); ++i) {
            const auto& s = samples_[i];
            const double left = i == 0 ? x - 8 : (px(i - 1) + px(i)) / 2;
            const double right = i + 1 == samples_.size() ? x + pw + 8 : (px(i) + px(i + 1)) / 2;
            hits_.push_back({left, y - 8, right - left, ph + 16, s.id,
                             s.label + " · " +
                                 (s.value ? Number(*s.value, 1) + " " + unit_ : "未记录") + "\n" +
                                 s.detail});
            if (!s.value) {
                previous = false;
                continue;
            }
            const double py = y + ph - ph * *s.value / maximum;
            if (s.id == selected_) {
                auto faded = accent;
                faded.alpha = .22;
                Ink(cr, faded);
                cairo_set_line_width(cr, 1);
                cairo_move_to(cr, px(i), y);
                cairo_line_to(cr, px(i), y + ph);
                cairo_stroke(cr);
            }
            Ink(cr, accent);
            if (previous) {
                const auto prev = *samples_[i - 1].value;
                cairo_set_line_width(cr, 2);
                cairo_move_to(cr, px(i - 1), y + ph - ph * prev / maximum);
                cairo_line_to(cr, px(i), py);
                cairo_stroke(cr);
            }
            cairo_arc(cr, px(i), py, s.id == selected_ ? 4.5 : 2.5, 0, 2 * M_PI);
            cairo_fill(cr);
            previous = true;
        }
        if (!any)
            Text(cr, empty_, x, y + ph / 2, 11, secondary, pw, PANGO_ALIGN_CENTER);
    } else if (kind_ == Kind::Keyboard) {
        Round(cr, .5, .5, w - 1, h - 1, 8);
        Ink(cr, Color(theme_, "surfaceSecondary", .66));
        cairo_fill_preserve(cr);
        const double dash[] = {4, 3};
        cairo_set_dash(cr, dash, 2, 0);
        Ink(cr, Color(theme_, "borderStrong", .72));
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
        cairo_set_dash(cr, nullptr, 0, 0);
        double max_x = 0, max_y = 0;
        std::int64_t maximum = 0;
        for (const auto& key : MetricsContract().at("keyboard")) {
            max_x = std::max(max_x, key.at("x").get<double>() + key.at("width").get<double>());
            max_y = std::max(max_y, key.at("y").get<double>() + key.at("height").get<double>());
            maximum =
                std::max(maximum, counts_.value(key.at("id").get<std::string>(), std::int64_t(0)));
        }
        const auto total = Total(counts_);
        const double scale = std::min((w - 32) / max_x, (h - 32) / max_y),
                     ox = (w - max_x * scale) / 2, oy = (h - max_y * scale) / 2;
        for (const auto& key : MetricsContract().at("keyboard")) {
            const std::string id = key.at("id"), label = key.at("label");
            const auto count = counts_.value(id, std::int64_t(0));
            const double x = ox + key.at("x").get<double>() * scale + 2,
                         y = oy + key.at("y").get<double>() * scale + 2,
                         kw = key.at("width").get<double>() * scale - 4,
                         kh = key.at("height").get<double>() * scale - 4;
            auto base = surface;
            auto fill =
                count && maximum
                    ? Blend(base, accent, std::max(.16, std::sqrt(double(count) / maximum)) * .86)
                    : base;
            if (hovered_ == id)
                fill = count ? Blend(fill, GdkRGBA{1, 1, 1, 1}, .12) : Blend(fill, accent, .20);
            Round(cr, x, y, kw, kh, std::min(7.0, kh * .22));
            Ink(cr, fill);
            cairo_fill_preserve(cr);
            Ink(cr, border);
            cairo_set_line_width(cr, .6);
            cairo_stroke(cr);
            const double luma = fill.red * .2126 + fill.green * .7152 + fill.blue * .0722;
            const auto ink =
                GdkRGBA{luma > .48 ? 0.05 : 1., luma > .48 ? 0.05 : 1., luma > .48 ? 0.05 : 1., 1};
            const bool show_count = kw >= 28 && kh >= 26;
            std::string display = label;
            if (id.find("Control") != std::string::npos)
                display = "⌃";
            if (id.find("Option") != std::string::npos)
                display = "⌥";
            if (id.find("Command") != std::string::npos)
                display = "⌘";
            Text(cr, display, x + 3, y + (show_count ? 3 : (kh - 12) / 2), kh < 24 ? 9 : 11, ink,
                 kw - 6, PANGO_ALIGN_CENTER, true, false, true);
            if (show_count && count)
                Text(cr, std::to_string(count), x, y + kh - 12, 8, ink, kw, PANGO_ALIGN_CENTER,
                     false, true);
            hits_.push_back({x, y, kw, kh, id,
                             label + " · " + std::to_string(count) + " 次 · " +
                                 Number(total ? 100. * count / total : 0, 1) + "%"});
            if (gtk_widget_has_focus(widget_) && selected_ == id) {
                Round(cr, x - 1, y - 1, kw + 2, kh + 2, 5);
                Ink(cr, accent);
                cairo_set_line_width(cr, 1.5);
                cairo_stroke(cr);
            }
        }
    } else {
        if (days_.empty()) {
            Text(cr, "尚无按键记录", 0, 65, 11, secondary, w, PANGO_ALIGN_CENTER);
            return;
        }
        std::int64_t maximum = 0;
        for (const auto& n : counts_)
            maximum = std::max(maximum, n.get<std::int64_t>());
        const char* weekdays[] = {"一", "二", "三", "四", "五", "六", "日"};
        for (int i = 0; i < 7; ++i)
            Text(cr, weekdays[i], 14, 28 + 15 * i, 9, muted);
        std::string month;
        double label_end = -1;
        for (std::size_t i = 0; i < days_.size(); ++i) {
            const auto& day = days_[i];
            const auto n = counts_.value(day, std::int64_t(0));
            const double x = 32 + 15 * ((i + monday_) / 7), y = 28 + 15 * ((i + monday_) % 7);
            const auto current_month = day.substr(0, 7);
            if (current_month != month) {
                const auto title = (month.empty() || month.substr(0, 4) != day.substr(0, 4)
                                        ? day.substr(0, 4) + "年"
                                        : "") +
                                   std::to_string(std::stoi(day.substr(5, 2))) + "月";
                if (x >= label_end)
                    label_end =
                        x + Text(cr, title, x, 7, 10, secondary, -1, PANGO_ALIGN_LEFT, true) + 12;
                month = current_month;
            }
            const double level =
                n && maximum ? std::ceil(std::sqrt(double(n) / maximum) * 4) / 4 : 0;
            Round(cr, x, y, 12, 12, 2.5);
            Ink(cr, level ? Blend(surface, accent, .15 + level * .75) : surface);
            cairo_fill_preserve(cr);
            Ink(cr, selected_ == day || hovered_ == day ? accent : border);
            cairo_set_line_width(cr, selected_ == day || hovered_ == day ? 1.5 : .5);
            cairo_stroke(cr);
            hits_.push_back({x, y, 12, 12, day, day + " · " + std::to_string(n) + " 次"});
        }
        const double legend_x = std::max(32., w - 165);
        Text(cr, "少", legend_x, 140, 9, muted);
        for (int i = 0; i < 5; ++i) {
            Round(cr, legend_x + 22 + i * 15, 140, 12, 12, 2.5);
            Ink(cr, i ? Blend(surface, accent, .15 + .75 * i / 4) : surface);
            cairo_fill(cr);
        }
        Text(cr, "多", legend_x + 102, 140, 9, muted);
    }
}
} // namespace rimes::desktop
