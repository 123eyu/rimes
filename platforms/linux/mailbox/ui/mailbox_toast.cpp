#include "desktop.hpp"
#include "mailbox_toast.hpp"
namespace rimes::mailbox {
using namespace desktop;
CompletionToast::CompletionToast(std::function<void(const std::string&)> open)
    : open_(std::move(open)) {
    window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window_), "Mailbox 通知");
    gtk_window_set_decorated(GTK_WINDOW(window_), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window_), FALSE);
    Class(window_, "mailbox-toast");
    auto* visual = gdk_screen_get_rgba_visual(gtk_widget_get_screen(window_));
    if (visual)
        gtk_widget_set_visual(window_, visual);
    panel_ = std::make_unique<PanelWindow>(GTK_WINDOW(window_), "mailbox-toast", 16, false);
    auto* button = Button(
        "",
        [this] {
            const auto thread = target_;
            Hide();
            if (!thread.empty())
                open_(thread);
        },
        "mailbox-toast-button");
    gtk_widget_set_can_focus(button, FALSE);
    atk_object_set_name(gtk_widget_get_accessible(button), "打开 Mailbox 查看新消息");
    gtk_container_remove(GTK_CONTAINER(button), gtk_bin_get_child(GTK_BIN(button)));
    auto* row = Row(8);
    gtk_widget_set_margin_start(row, 12);
    gtk_widget_set_margin_end(row, 12);
    gtk_widget_set_valign(row, GTK_ALIGN_CENTER);
    auto* dot = Class(gtk_drawing_area_new(), "mailbox-toast-dot");
    gtk_widget_set_size_request(dot, 8, 8);
    gtk_widget_set_valign(dot, GTK_ALIGN_CENTER);
    Add(row, dot);
    label_ = Label("", "mailbox-toast-label");
    gtk_label_set_ellipsize(GTK_LABEL(label_), PANGO_ELLIPSIZE_END);
    Add(row, label_, true);
    Add(row, Icon("chevron.right", 10));
    gtk_container_add(GTK_CONTAINER(button), row);
    gtk_container_add(GTK_CONTAINER(window_), button);
    css_ = gtk_css_provider_new();
    gtk_style_context_add_provider_for_screen(gtk_widget_get_screen(window_),
                                              GTK_STYLE_PROVIDER(css_),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 2);
}
CompletionToast::~CompletionToast() {
    Hide();
    gtk_style_context_remove_provider_for_screen(gtk_widget_get_screen(window_),
                                                 GTK_STYLE_PROVIDER(css_));
    g_object_unref(css_);
    panel_.reset();
    gtk_widget_destroy(window_);
}
void CompletionToast::Hide() {
    if (timer_) {
        g_source_remove(timer_);
        timer_ = 0;
    }
    target_.clear();
    gtk_widget_hide(window_);
}
void CompletionToast::Show(const std::string& thread, const std::string& label,
                           const std::string& theme) {
    Hide();
    const auto& palettes = Visuals().at("palettes");
    const auto& palette = palettes.at(palettes.contains(theme) ? theme : "day");
    auto c = [&](const char* name) { return palette.at(name).get<std::string>(); };
    const auto css =
        "window.mailbox-toast { background: transparent; }"
        "button.mailbox-toast-button { background: " +
        c("surfaceSecondary") + "; border: 1px solid " + c("border") +
        "; border-radius: 10px; padding: 0; min-width: 290px; min-height: 46px; box-shadow: none; }"
        ".mailbox-toast-label { color: " +
        c("textPrimary") +
        "; font-size: 12px; font-weight: 500; }"
        ".mailbox-toast-dot { background: " +
        c("accentGreen") +
        "; border-radius: 4px; }"
        ".mailbox-toast .glyph { color: " +
        c("textMuted") + "; }";
    GError* error = nullptr;
    if (!gtk_css_provider_load_from_data(css_, css.c_str(), css.size(), &error)) {
        g_clear_error(&error);
        return;
    }
    target_ = thread;
    gtk_label_set_text(GTK_LABEL(label_), label.c_str());
    panel_->Place(292, 48, {}, true);
    auto* display = gtk_widget_get_display(window_);
    auto* pointer = gdk_seat_get_pointer(gdk_display_get_default_seat(display));
    gint x = 0, y = 0;
    if (pointer)
        gdk_device_get_position(pointer, nullptr, &x, &y);
    auto* monitor = gdk_display_get_monitor_at_point(display, x, y);
    if (monitor) {
        GdkRectangle work{};
        gdk_monitor_get_workarea(monitor, &work);
        panel_->Move({work.x + work.width - 292 - 16, work.y + 16, 292, 48});
    }
    gtk_widget_show_all(window_);
    timer_ = g_timeout_add(
        8000,
        +[](gpointer data) -> gboolean {
            auto* self = static_cast<CompletionToast*>(data);
            self->timer_ = 0;
            self->Hide();
            return G_SOURCE_REMOVE;
        },
        this);
}
} // namespace rimes::mailbox
