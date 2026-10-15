#include "../../product_brand.hpp"
#include "buffer_placement.hpp"
#include "buffer_protocol.hpp"

#include <gdk/gdk.h>
#include <gtk/gtk.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif

#ifdef HAVE_GTK_LAYER_SHELL
#include <gtk-layer-shell.h>
#endif

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int kWindowWidth = 760;
constexpr int kWindowHeight = 78;
constexpr int kToolbarHeight = 33;
constexpr int kGapAroundCaret = 10;

struct App {
    GtkWidget* window = nullptr;
    GtkWidget* chip_box = nullptr;
    GtkWidget* placeholder = nullptr;
    GtkWidget* preedit = nullptr;
    GtkWidget* status = nullptr;
    GtkWidget* send = nullptr;
    GtkWidget* progress = nullptr;
    GtkWidget* target = nullptr;
    int socket_fd = -1;
    guint io_id = 0;
    std::string incoming;
    std::string dump_geometry;
    bool quit_after_dump = false;
    bool dumped_geometry = false;
    rimes::buffer::Snapshot snapshot;
    bool wayland_layer = false;
    bool placed = false;
    bool moving = false;
    guint drag_poll_id = 0;
    guint drag_cap_id = 0;
};

App* g_app = nullptr;

std::string DefaultSocketPath() {
    if (const char* override_path = g_getenv("RIMES_BUFFER_SOCKET")) {
        if (override_path[0] != '\0') {
            return override_path;
        }
    }
    if (const char* runtime = g_getenv("XDG_RUNTIME_DIR")) {
        return std::string(runtime) + "/rimes-buffer.sock";
    }
    return "/tmp/rimes-buffer-" + std::to_string(geteuid()) + ".sock";
}

bool SendCommand(App* app, const std::string& json) {
    std::string frame;
    if (app->socket_fd < 0 || !rimes::buffer::EncodeFrame(json, &frame)) {
        return false;
    }
    return send(app->socket_fd, frame.data(), frame.size(), MSG_NOSIGNAL) ==
           static_cast<ssize_t>(frame.size());
}

void ApplyLayerShell(GtkWindow* window, App* app) {
#ifdef HAVE_GTK_LAYER_SHELL
    GdkDisplay* display = gtk_widget_get_display(GTK_WIDGET(window));
#ifdef GDK_WINDOWING_WAYLAND
    if (display == nullptr || !GDK_IS_WAYLAND_DISPLAY(display)) {
        return;
    }
#else
    (void)display;
    return;
#endif
    if (!gtk_layer_is_supported()) {
        return;
    }
    gtk_layer_init_for_window(window);
    gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_BOTTOM, 48);
    gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_LEFT, 80);
    gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_RIGHT, 80);
    gtk_layer_set_namespace(window, "rimes-buffer");
#if GTK_LAYER_SHELL_MAJOR >= 0
    gtk_layer_set_keyboard_mode(window, GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
#endif
    app->wayland_layer = true;
#else
    (void)window;
    (void)app;
#endif
}

void PlaceOnX11(App* app) {
    if (app->wayland_layer || !app->snapshot.caret.valid) {
        return;
    }
    rimes::buffer::Workarea work;
    GdkDisplay* display = gtk_widget_get_display(app->window);
    GdkMonitor* monitor = gdk_display_get_monitor_at_point(display, app->snapshot.caret.x,
                                                           app->snapshot.caret.y);
    if (monitor != nullptr) {
        GdkRectangle area{};
        gdk_monitor_get_workarea(monitor, &area);
        work.x = area.x;
        work.y = area.y;
        work.width = area.width;
        work.height = area.height;
    } else {
        work.width = 1280;
        work.height = 800;
    }
    const auto placed = rimes::buffer::PlaceX11Panel(app->snapshot.caret, work, kWindowWidth,
                                                     kWindowHeight, kGapAroundCaret);
    gtk_window_move(GTK_WINDOW(app->window), placed.x, placed.y);
    app->placed = true;
}

void RebuildChips(App* app) {
    GList* children = gtk_container_get_children(GTK_CONTAINER(app->chip_box));
    for (GList* cursor = children; cursor != nullptr; cursor = cursor->next) {
        gtk_widget_destroy(GTK_WIDGET(cursor->data));
    }
    g_list_free(children);

    if (app->snapshot.secure) {
        gtk_widget_hide(app->placeholder);
        gtk_widget_hide(app->preedit);
        return;
    }

    const bool idle = app->snapshot.blocks.empty() && app->snapshot.preedit.empty() &&
                      !app->snapshot.capturing;
    if (idle) {
        gtk_label_set_text(GTK_LABEL(app->placeholder), app->snapshot.placeholder.c_str());
        gtk_widget_show(app->placeholder);
    } else {
        gtk_widget_hide(app->placeholder);
    }

    for (const auto& block : app->snapshot.blocks) {
        GtkWidget* chip = gtk_label_new(block.text.c_str());
        gtk_widget_set_name(chip, "rimes-chip");
        gtk_widget_set_tooltip_text(chip, block.text.c_str());
        gtk_label_set_single_line_mode(GTK_LABEL(chip), TRUE);
        gtk_label_set_ellipsize(GTK_LABEL(chip), PANGO_ELLIPSIZE_END);
        gtk_box_pack_start(GTK_BOX(app->chip_box), chip, FALSE, FALSE, 0);
        gtk_widget_show(chip);
    }
    if (!app->snapshot.preedit.empty()) {
        gtk_label_set_text(GTK_LABEL(app->preedit), app->snapshot.preedit.c_str());
        gtk_widget_show(app->preedit);
    } else {
        gtk_widget_hide(app->preedit);
    }
}

void ApplySnapshotJson(App* app, const std::string& json) {
    if (json.find("\"type\":\"request_clipboard\"") != std::string::npos) {
        GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        gchar* text = gtk_clipboard_wait_for_text(clipboard);
        if (text != nullptr && text[0] != '\0') {
            std::string command = std::string("{\"v\":1,\"op\":\"paste\",\"text\":") +
                                  rimes::buffer::JsonEscape(text) + '}';
            SendCommand(app, command);
            g_free(text);
        }
        return;
    }
    if (json.find("\"type\":\"snapshot\"") == std::string::npos) {
        return;
    }
    app->snapshot.visible = json.find("\"visible\":true") != std::string::npos;
    app->snapshot.capturing = json.find("\"capturing\":true") != std::string::npos;
    app->snapshot.secure = json.find("\"secure\":true") != std::string::npos;
    app->snapshot.empty = json.find("\"empty\":true") != std::string::npos;
    const auto preedit_key = json.find("\"preedit\":");
    if (preedit_key != std::string::npos) {
        rimes::buffer::Command ignored;
        std::string error;
        (void)ignored;
        (void)error;
    }

    // Re-parse through the same encoder fields we control.
    // A full JSON parser is unnecessary: the IME writes our snapshot schema.
    auto extract_string = [&](const char* key) -> std::string {
        const std::string needle = std::string("\"") + key + "\":\"";
        const auto start = json.find(needle);
        if (start == std::string::npos) {
            return {};
        }
        std::string raw;
        for (std::size_t index = start + needle.size(); index < json.size(); ++index) {
            if (json[index] == '\\' && index + 1 < json.size()) {
                raw.push_back(json[index]);
                raw.push_back(json[index + 1]);
                ++index;
                continue;
            }
            if (json[index] == '"') {
                break;
            }
            raw.push_back(json[index]);
        }
        std::string out;
        rimes::buffer::JsonUnescape(raw, &out);
        return out;
    };
    app->snapshot.preedit = extract_string("preedit");
    app->snapshot.placeholder = extract_string("placeholder");
    app->snapshot.target = extract_string("target");
    app->snapshot.staged_text = extract_string("staged_text");

    auto extract_int = [&](const char* key, int fallback) {
        const std::string needle = std::string("\"") + key + "\":";
        const auto start = json.find(needle);
        if (start == std::string::npos) {
            return fallback;
        }
        return static_cast<int>(g_ascii_strtoll(json.c_str() + start + needle.size(), nullptr, 10));
    };
    app->snapshot.caret.x = extract_int("x", 0);
    app->snapshot.caret.y = extract_int("y", 0);
    app->snapshot.caret.width = extract_int("w", 0);
    app->snapshot.caret.height = extract_int("h", 0);
    app->snapshot.caret.valid = json.find("\"valid\":true") != std::string::npos;

    const auto progress_at = json.find("\"hold_progress\":");
    if (progress_at != std::string::npos) {
        app->snapshot.hold_progress =
            g_ascii_strtod(json.c_str() + progress_at + std::strlen("\"hold_progress\":"), nullptr);
    }

    app->snapshot.blocks.clear();
    std::size_t search = json.find("\"blocks\":[");
    while (search != std::string::npos) {
        const auto id_at = json.find("\"id\":\"", search);
        const auto text_at = json.find("\"text\":\"", search);
        const auto origin_at = json.find("\"origin\":\"", search);
        const auto next_obj = json.find("},{", search);
        const auto end_arr = json.find(']', search);
        if (id_at == std::string::npos || text_at == std::string::npos ||
            (end_arr != std::string::npos && id_at > end_arr)) {
            break;
        }
        rimes::buffer::Block block;
        auto take = [&](std::size_t at, const char* prefix) {
            std::string raw;
            for (std::size_t index = at + std::strlen(prefix); index < json.size(); ++index) {
                if (json[index] == '\\' && index + 1 < json.size()) {
                    raw.push_back(json[index]);
                    raw.push_back(json[index + 1]);
                    ++index;
                    continue;
                }
                if (json[index] == '"') {
                    break;
                }
                raw.push_back(json[index]);
            }
            std::string out;
            rimes::buffer::JsonUnescape(raw, &out);
            return out;
        };
        block.id = take(id_at, "\"id\":\"");
        block.text = take(text_at, "\"text\":\"");
        if (origin_at != std::string::npos && (next_obj == std::string::npos || origin_at < next_obj)) {
            rimes::buffer::ParseOrigin(take(origin_at, "\"origin\":\""), &block.origin);
        }
        app->snapshot.blocks.push_back(block);
        if (next_obj == std::string::npos || (end_arr != std::string::npos && next_obj > end_arr)) {
            break;
        }
        search = next_obj + 1;
    }

    if (app->snapshot.visible && !app->snapshot.secure) {
        gtk_widget_show_all(app->window);
        gtk_window_set_keep_above(GTK_WINDOW(app->window), TRUE);
        if (!app->wayland_layer && !app->placed) {
            PlaceOnX11(app);
        }
    } else if (!app->snapshot.visible) {
        gtk_widget_hide(app->window);
        app->placed = false;
    }

    RebuildChips(app);
    const char* status = app->snapshot.capturing ? "Buffer" : "Paused";
    if (app->snapshot.secure) {
        status = "Protected";
    }
    gtk_label_set_text(GTK_LABEL(app->status), status);
    if (app->snapshot.target.empty()) {
        gtk_label_set_text(GTK_LABEL(app->target), "·");
    } else {
        gtk_label_set_text(GTK_LABEL(app->target), app->snapshot.target.c_str());
    }
    gtk_widget_set_sensitive(app->send, !app->snapshot.blocks.empty() && app->snapshot.capturing);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress),
                                  CLAMP(app->snapshot.hold_progress, 0.0, 1.0));
}

gboolean OnSocket(GIOChannel* /*channel*/, GIOCondition condition, gpointer data) {
    auto* app = static_cast<App*>(data);
    if ((condition & (G_IO_ERR | G_IO_HUP)) != 0) {
        gtk_main_quit();
        return FALSE;
    }
    char chunk[4096];
    const auto got = read(app->socket_fd, chunk, sizeof(chunk));
    if (got <= 0) {
        gtk_main_quit();
        return FALSE;
    }
    app->incoming.append(chunk, static_cast<std::size_t>(got));
    while (app->incoming.size() >= 4) {
        std::uint32_t length = 0;
        if (!rimes::buffer::DecodeFrameHeader(app->incoming.data(), &length)) {
            gtk_main_quit();
            return FALSE;
        }
        if (app->incoming.size() < 4 + length) {
            break;
        }
        const std::string payload = app->incoming.substr(4, length);
        app->incoming.erase(0, 4 + length);
        ApplySnapshotJson(app, payload);
    }
    return TRUE;
}

void WriteGeometryDump(App* app) {
    if (app->dump_geometry.empty() || app->dumped_geometry) {
        return;
    }
    GtkAllocation allocation{};
    gtk_widget_get_allocation(app->window, &allocation);
    if (allocation.width < 2 || allocation.height < 2) {
        gtk_window_get_size(GTK_WINDOW(app->window), &allocation.width, &allocation.height);
    }
    if (allocation.width < 2 && !app->quit_after_dump) {
        return;
    }
    gint request_w = 0;
    gint request_h = 0;
    gtk_widget_get_size_request(app->window, &request_w, &request_h);
    FILE* file = std::fopen(app->dump_geometry.c_str(), "w");
    if (file == nullptr) {
        return;
    }
    std::fprintf(file,
                 "{\"width\":%d,\"height\":%d,\"size_request_w\":%d,\"size_request_h\":%d,"
                 "\"layer\":%s,\"min_width\":%d,\"min_height\":%d}\n",
                 allocation.width, allocation.height, request_w, request_h,
                 app->wayland_layer ? "true" : "false", kWindowWidth, kWindowHeight);
    std::fclose(file);
    app->dumped_geometry = true;
    if (app->quit_after_dump) {
        gtk_main_quit();
    }
}

void OnSizeAllocate(GtkWidget* /*widget*/, GdkRectangle* allocation, gpointer data) {
    if (allocation == nullptr || allocation->width < 2 || allocation->height < 2) {
        return;
    }
    WriteGeometryDump(static_cast<App*>(data));
}

void CancelDragTimers(App* app) {
    if (app->drag_poll_id != 0) {
        g_source_remove(app->drag_poll_id);
        app->drag_poll_id = 0;
    }
    if (app->drag_cap_id != 0) {
        g_source_remove(app->drag_cap_id);
        app->drag_cap_id = 0;
    }
}

void FinishMoveDrag(App* app) {
    if (!app->moving) {
        return;
    }
    app->moving = false;
    CancelDragTimers(app);
    SendCommand(app, R"({"v":1,"op":"drag_end"})");
}

bool PointerButton1Down(App* app) {
    if (app->window == nullptr) {
        return false;
    }
    GdkWindow* gdk_window = gtk_widget_get_window(app->window);
    GdkDisplay* display = gtk_widget_get_display(app->window);
    if (gdk_window == nullptr || display == nullptr) {
        return true;
    }
    GdkSeat* seat = gdk_display_get_default_seat(display);
    if (seat == nullptr) {
        return true;
    }
    GdkDevice* pointer = gdk_seat_get_pointer(seat);
    if (pointer == nullptr) {
        return true;
    }
    GdkModifierType mask = static_cast<GdkModifierType>(0);
    gdk_device_get_state(pointer, gdk_window, nullptr, &mask);
    return (mask & GDK_BUTTON1_MASK) != 0;
}

gboolean OnDragPoll(gpointer data) {
    auto* app = static_cast<App*>(data);
    if (!app->moving) {
        app->drag_poll_id = 0;
        return G_SOURCE_REMOVE;
    }
    if (!PointerButton1Down(app)) {
        FinishMoveDrag(app);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

gboolean OnDragHardCap(gpointer data) {
    auto* app = static_cast<App*>(data);
    app->drag_cap_id = 0;
    FinishMoveDrag(app);
    return G_SOURCE_REMOVE;
}

gboolean OnButtonReleaseDuringDrag(GtkWidget* /*widget*/, GdkEventButton* event, gpointer data) {
    auto* app = static_cast<App*>(data);
    if (event->button == 1) {
        FinishMoveDrag(app);
    }
    return FALSE;
}

gboolean OnToolbarDrag(GtkWidget* /*widget*/, GdkEventButton* event, gpointer data) {
    auto* app = static_cast<App*>(data);
    if (event->type == GDK_BUTTON_PRESS && event->button == 1 && !app->wayland_layer) {
        CancelDragTimers(app);
        app->moving = true;
        SendCommand(app, R"({"v":1,"op":"drag_begin"})");
        app->drag_poll_id = g_timeout_add(50, OnDragPoll, app);
        app->drag_cap_id = g_timeout_add(30000, OnDragHardCap, app);
        gtk_window_begin_move_drag(GTK_WINDOW(app->window), static_cast<gint>(event->button),
                                   static_cast<gint>(event->x_root),
                                   static_cast<gint>(event->y_root), event->time);
        return TRUE;
    }
    return FALSE;
}

int ConnectDelayMs() {
    const char* value = g_getenv("RIMES_BUFFER_CONNECT_DELAY_MS");
    if (value == nullptr || value[0] == '\0') {
        return 0;
    }
    char* end = nullptr;
    const auto parsed = std::strtol(value, &end, 10);
    if (end == value || parsed < 0 || parsed > 30000) {
        return 0;
    }
    return static_cast<int>(parsed);
}

gboolean ConnectSocket(App* app, const std::string& path) {
    const int delay_ms = ConnectDelayMs();
    if (delay_ms > 0) {
        g_usleep(static_cast<gulong>(delay_ms) * 1000);
    }
    app->socket_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (app->socket_fd < 0) {
        return FALSE;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
    if (connect(app->socket_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        close(app->socket_fd);
        app->socket_fd = -1;
        return FALSE;
    }
    const int flags = fcntl(app->socket_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(app->socket_fd, F_SETFL, flags | O_NONBLOCK);
    }
    GIOChannel* channel = g_io_channel_unix_new(app->socket_fd);
    g_io_channel_set_encoding(channel, nullptr, nullptr);
    g_io_channel_set_buffered(channel, FALSE);
    app->io_id = g_io_add_watch(channel, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR),
                                OnSocket, app);
    g_io_channel_unref(channel);
    SendCommand(app, R"({"v":1,"op":"hello"})");
    return TRUE;
}

void LoadCss() {
    GtkCssProvider* provider = gtk_css_provider_new();
    const char* css = R"CSS(
window.rimes-buffer { background: transparent; min-width: 760px; min-height: 78px; }
#rimes-chrome {
  background-color: #1c2420;
  border: 1px solid #3d5a4c;
  border-radius: 12px;
  min-width: 760px;
  min-height: 78px;
}
#rimes-toolbar { min-height: 33px; }
#rimes-title, #rimes-status, #rimes-target {
  color: #c5d4c8;
  font-size: 11px;
}
#rimes-chip {
  background-color: #2a3830;
  color: #e8f0ea;
  border-radius: 4px;
  padding: 1px 6px;
  font-size: 12px;
  margin-right: 3px;
}
#rimes-placeholder, #rimes-preedit { color: #9aada0; font-size: 12px; }
#rimes-preedit { text-decoration: underline; color: #e8f0ea; }
button.rimes-action {
  background: #2f463a;
  color: #e8f0ea;
  border: 1px solid #4a6b58;
  border-radius: 6px;
  min-width: 22px;
  min-height: 22px;
  padding: 0 6px;
}
button.rimes-action:hover { background: #3b5a49; }
button.rimes-action:disabled { opacity: 0.4; }
#rimes-progress trough { min-height: 2px; background: #1c2420; }
#rimes-progress progress { background: #7cbc8a; min-height: 2px; }
)CSS";
    gtk_css_provider_load_from_data(provider, css, -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

void BuildUi(App* app) {
    app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->window), rimes::branding::Title(" Buffer").c_str());
    gtk_window_set_icon_name(GTK_WINDOW(app->window), "rimes");
    gtk_window_set_default_size(GTK_WINDOW(app->window), kWindowWidth, kWindowHeight);
    gtk_widget_set_size_request(app->window, kWindowWidth, kWindowHeight);
    gtk_widget_set_hexpand(app->window, TRUE);
    gtk_window_set_resizable(GTK_WINDOW(app->window), FALSE);
    gtk_window_set_decorated(GTK_WINDOW(app->window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(app->window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(app->window), TRUE);
    gtk_window_set_accept_focus(GTK_WINDOW(app->window), FALSE);
    gtk_window_set_focus_on_map(GTK_WINDOW(app->window), FALSE);
    // UTILITY (not DOCK): Xfce/xfwm can move it via begin_move_drag.
    gtk_window_set_type_hint(GTK_WINDOW(app->window), GDK_WINDOW_TYPE_HINT_UTILITY);
    gtk_window_set_keep_above(GTK_WINDOW(app->window), TRUE);
    gtk_widget_set_name(app->window, "rimes-buffer-window");
    gtk_style_context_add_class(gtk_widget_get_style_context(app->window), "rimes-buffer");
    gtk_widget_set_app_paintable(app->window, TRUE);
    gtk_widget_add_events(app->window, GDK_BUTTON_RELEASE_MASK);
    g_signal_connect(app->window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    g_signal_connect(app->window, "size-allocate", G_CALLBACK(OnSizeAllocate), app);
    // Button-release is a backup. The WM move-grab usually swallows it;
    // the 50 ms pointer-button poll is the real end signal. Do not treat
    // grab-broken as drag_end: xfwm fires that when it *takes* the grab.
    g_signal_connect(app->window, "button-release-event", G_CALLBACK(OnButtonReleaseDuringDrag),
                     app);

    ApplyLayerShell(GTK_WINDOW(app->window), app);
    // Layer-shell ignores default_size unless a request or stretch anchors exist.
    gtk_widget_set_size_request(app->window, kWindowWidth, kWindowHeight);

    GtkWidget* chrome = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(chrome, "rimes-chrome");
    gtk_container_add(GTK_CONTAINER(app->window), chrome);

    GtkWidget* toolbar = gtk_event_box_new();
    gtk_widget_set_name(toolbar, "rimes-toolbar");
    gtk_widget_add_events(toolbar, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    g_signal_connect(toolbar, "button-press-event", G_CALLBACK(OnToolbarDrag), app);
    g_signal_connect(toolbar, "button-release-event", G_CALLBACK(OnButtonReleaseDuringDrag), app);
    gtk_box_pack_start(GTK_BOX(chrome), toolbar, FALSE, FALSE, 0);

    GtkWidget* toolbar_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_add(GTK_CONTAINER(toolbar), toolbar_row);
    gtk_container_set_border_width(GTK_CONTAINER(toolbar_row), 6);

    GtkWidget* title = gtk_label_new("Default");
    gtk_widget_set_name(title, "rimes-title");
    gtk_box_pack_start(GTK_BOX(toolbar_row), title, FALSE, FALSE, 0);

    app->status = gtk_label_new("Buffer");
    gtk_widget_set_name(app->status, "rimes-status");
    gtk_box_pack_start(GTK_BOX(toolbar_row), app->status, FALSE, FALSE, 0);

    GtkWidget* spacer = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(toolbar_row), spacer, TRUE, TRUE, 0);

    app->target = gtk_label_new("·");
    gtk_widget_set_name(app->target, "rimes-target");
    gtk_widget_set_tooltip_text(app->target, "Exact delivery target");
    gtk_box_pack_start(GTK_BOX(toolbar_row), app->target, FALSE, FALSE, 0);

    GtkWidget* close = gtk_button_new_with_label("×");
    gtk_style_context_add_class(gtk_widget_get_style_context(close), "rimes-action");
    gtk_widget_set_tooltip_text(close, "Close and pause (keeps staged blocks)");
    g_signal_connect(close, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
                         SendCommand(static_cast<App*>(data), R"({"v":1,"op":"close"})");
                     }),
                     app);
    gtk_box_pack_end(GTK_BOX(toolbar_row), close, FALSE, FALSE, 0);

    GtkWidget* body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(body), 6);
    gtk_box_pack_start(GTK_BOX(chrome), body, TRUE, TRUE, 0);

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_NEVER);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_NONE);
    gtk_box_pack_start(GTK_BOX(body), scroll, TRUE, TRUE, 0);

    GtkWidget* rail = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_container_add(GTK_CONTAINER(scroll), rail);

    app->placeholder = gtk_label_new(rimes::buffer::kPlaceholder);
    gtk_widget_set_name(app->placeholder, "rimes-placeholder");
    gtk_box_pack_start(GTK_BOX(rail), app->placeholder, FALSE, FALSE, 0);

    app->chip_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    gtk_box_pack_start(GTK_BOX(rail), app->chip_box, FALSE, FALSE, 0);

    app->preedit = gtk_label_new("");
    gtk_widget_set_name(app->preedit, "rimes-preedit");
    gtk_box_pack_start(GTK_BOX(rail), app->preedit, FALSE, FALSE, 0);

    GtkWidget* clipboard = gtk_button_new();
    gtk_button_set_image(GTK_BUTTON(clipboard),
                         gtk_image_new_from_icon_name("edit-paste", GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(clipboard), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(clipboard), "rimes-action");
    gtk_widget_set_tooltip_text(clipboard, "Import clipboard into Buffer");
    g_signal_connect(clipboard, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
                         auto* self = static_cast<App*>(data);
                         GtkClipboard* board = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
                         gchar* text = gtk_clipboard_wait_for_text(board);
                         if (text == nullptr || text[0] == '\0') {
                             g_free(text);
                             return;
                         }
                         std::string command = std::string("{\"v\":1,\"op\":\"paste\",\"text\":") +
                                               rimes::buffer::JsonEscape(text) + '}';
                         SendCommand(self, command);
                         g_free(text);
                     }),
                     app);
    gtk_box_pack_end(GTK_BOX(body), clipboard, FALSE, FALSE, 0);

    app->send = gtk_button_new();
    gtk_button_set_image(GTK_BUTTON(app->send),
                         gtk_image_new_from_icon_name("go-next", GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(app->send), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(app->send), "rimes-action");
    gtk_widget_set_tooltip_text(app->send, "Send next block");
    g_signal_connect(app->send, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
                         SendCommand(static_cast<App*>(data), R"({"v":1,"op":"send_next"})");
                     }),
                     app);
    gtk_box_pack_end(GTK_BOX(body), app->send, FALSE, FALSE, 0);

    app->progress = gtk_progress_bar_new();
    gtk_widget_set_name(app->progress, "rimes-progress");
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress), 0);
    gtk_box_pack_end(GTK_BOX(chrome), app->progress, FALSE, FALSE, 0);
}

}  // namespace

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    std::string socket_path = DefaultSocketPath();
    bool preview = false;
    std::string dump_geometry;
    bool quit_after_dump = false;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--socket") == 0 && index + 1 < argc) {
            socket_path = argv[++index];
        } else if (std::strcmp(argv[index], "--preview") == 0) {
            preview = true;
        } else if (std::strcmp(argv[index], "--dump-geometry") == 0 && index + 1 < argc) {
            dump_geometry = argv[++index];
        } else if (std::strcmp(argv[index], "--quit-after-dump") == 0) {
            quit_after_dump = true;
        }
    }

    App app;
    g_app = &app;
    app.dump_geometry = dump_geometry;
    app.quit_after_dump = quit_after_dump;
    LoadCss();
    BuildUi(&app);

    if (preview) {
        app.snapshot.visible = true;
        app.snapshot.capturing = true;
        app.snapshot.placeholder = rimes::buffer::kPlaceholder;
        rimes::buffer::Block demo;
        demo.id = "preview";
        demo.text = "你好";
        demo.origin = rimes::buffer::Origin::Rime;
        app.snapshot.blocks.push_back(demo);
        gtk_widget_show_all(app.window);
        RebuildChips(&app);
        if (!app.dump_geometry.empty()) {
            g_timeout_add(200, [](gpointer data) -> gboolean {
                WriteGeometryDump(static_cast<App*>(data));
                return G_SOURCE_REMOVE;
            }, &app);
        }
        if (app.quit_after_dump) {
            g_timeout_add(2000, [](gpointer data) -> gboolean {
                WriteGeometryDump(static_cast<App*>(data));
                gtk_main_quit();
                return G_SOURCE_REMOVE;
            }, &app);
        }
        gtk_main();
        return 0;
    }

    if (!ConnectSocket(&app, socket_path)) {
        g_printerr("rimes-buffer: could not connect to %s\n", socket_path.c_str());
        return 1;
    }
    gtk_main();
    if (app.socket_fd >= 0) {
        close(app.socket_fd);
    }
    return 0;
}
