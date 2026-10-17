#include "translation_resources.hpp"
#include "desktop.hpp"
#include <algorithm>
#include <csignal>
#include <filesystem>
#include <memory>

namespace rimes::desktop {
namespace {
struct Download : std::enable_shared_from_this<Download> {
    GtkWidget *dialog = nullptr, *status = nullptr, *progress = nullptr,
        *install = nullptr, *stop = nullptr, *check = nullptr;
    GSubprocess* child = nullptr;
    GInputStream* stream = nullptr;
    bool alive = true, running = false, cancelling = false, ready = false;
    std::string incoming, state;
    ~Download() { g_clear_object(&stream); g_clear_object(&child); }
    void Cancel() {
        if (child && running) {
            cancelling = true;
            g_subprocess_send_signal(child, SIGTERM);
            if (alive) gtk_label_set_text(GTK_LABEL(status), "正在取消，已下载的完整资源会保留…");
        }
    }
    void Controls() {
        if (!alive) return;
        gtk_widget_set_sensitive(install, !running && !ready);
        gtk_widget_set_sensitive(check, !running);
        gtk_widget_set_sensitive(stop, running && !cancelling);
    }
    void Event(const Json& value) {
        if (!alive) return;
        state = value.value("state", "error");
        ready = state == "ready";
        std::string message;
        if (state == "ready") { message = "中文与英语双向翻译已就绪，可离线使用。"; gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress), 1); }
        else if (state == "missing") message = "尚未安装语言资源。安装后可离线翻译。";
        else if (state == "damaged") message = "资源不完整或校验失败，请重新安装。";
        else if (state == "downloading") {
            message = "正在下载 " + value.value("resource", std::string("语言资源")) + "…";
            const auto total = value.value("total", 0.0), received = value.value("received", 0.0);
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress), total > 0 ? std::clamp(received / total, 0.0, 1.0) : 0);
        } else if (state == "verifying") { message = "正在校验并安装资源…"; gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progress)); }
        else if (state == "cancelled") message = "已取消，可以稍后重试。";
        else if (value.value("error", "") == "unsupported_python") message = "当前本地翻译需要 Python 3.12，请安装后重试。";
        else if (value.value("error", "") == "busy") message = "另一项语言资源操作正在进行，请稍后检查。";
        else message = "资源操作未完成，请检查网络或目录权限后重试。";
        gtk_label_set_text(GTK_LABEL(status), message.c_str());
    }
    void Read() {
        auto* keep = new std::shared_ptr<Download>(shared_from_this());
        g_input_stream_read_bytes_async(stream, 4096, G_PRIORITY_DEFAULT, nullptr,
            +[](GObject* source, GAsyncResult* result, gpointer pointer) {
                std::unique_ptr<std::shared_ptr<Download>> holder(static_cast<std::shared_ptr<Download>*>(pointer));
                const auto self = *holder;
                GError* error = nullptr;
                auto* bytes = g_input_stream_read_bytes_finish(G_INPUT_STREAM(source), result, &error);
                gsize size = 0;
                const auto* data = bytes ? static_cast<const char*>(g_bytes_get_data(bytes, &size)) : nullptr;
                if (data && size && self->alive) {
                    self->incoming.append(data, size);
                    if (self->incoming.size() > 64 * 1024) self->Cancel();
                    else {
                        for (auto at = self->incoming.find('\n'); at != std::string::npos; at = self->incoming.find('\n')) {
                            const auto line = self->incoming.substr(0, at);
                            self->incoming.erase(0, at + 1);
                            try { self->Event(Json::parse(line)); } catch (const std::exception&) { self->Cancel(); }
                        }
                    }
                }
                if (bytes) g_bytes_unref(bytes);
                g_clear_error(&error);
                if (size) { self->Read(); return; }
                auto* pending = new std::shared_ptr<Download>(self);
                g_subprocess_wait_async(self->child, nullptr, +[](GObject* child, GAsyncResult* result, gpointer pointer) {
                    std::unique_ptr<std::shared_ptr<Download>> holder(static_cast<std::shared_ptr<Download>*>(pointer));
                    auto self = *holder;
                    GError* error = nullptr;
                    const bool success = g_subprocess_wait_finish(G_SUBPROCESS(child), result, &error) && g_subprocess_get_successful(G_SUBPROCESS(child));
                    g_clear_error(&error);
                    self->running = false;
                    if (self->alive) {
                        if (self->cancelling) self->Event({{"state", "cancelled"}});
                        else if (!success && self->state != "error") self->Event({{"state", "error"}});
                        self->Controls();
                    }
                    g_clear_object(&self->stream);
                    g_clear_object(&self->child);
                }, pending);
            }, keep);
    }
    void Start(const std::string& op) {
        if (running || !alive) return;
        cancelling = false;
        state.clear();
        incoming.clear();
        GError* error = nullptr;
        std::error_code path_error;
        const auto executable = std::filesystem::read_symlink("/proc/self/exe", path_error);
        if (path_error) { Event({{"state", "error"}}); return; }
        const auto binary = executable.parent_path() / "rimes-translation";
        child = g_subprocess_new(GSubprocessFlags(G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE),
            &error, binary.c_str(), "--request", nullptr);
        if (!child) { g_clear_error(&error); Event({{"state", "error"}}); return; }
        const auto command = Json{{"op",op}}.dump();
        auto* input = g_subprocess_get_stdin_pipe(child);
        const bool sent = g_output_stream_write_all(input, command.data(), command.size(), nullptr, nullptr, &error);
        g_output_stream_close(input, nullptr, nullptr);
        g_clear_error(&error);
        running = true;
        stream = G_INPUT_STREAM(g_object_ref(g_subprocess_get_stdout_pipe(child)));
        gtk_label_set_text(GTK_LABEL(status), op == "install" ? "正在准备下载…" : "正在检查语言资源…");
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress), 0);
        Controls();
        if (!sent) Cancel();
        Read();
    }
};
}
void TranslationResources(GtkWindow* parent) {
    auto self = std::make_shared<Download>();
    self->dialog = gtk_dialog_new_with_buttons("本地翻译语言资源", parent, GTK_DIALOG_MODAL, "关闭", GTK_RESPONSE_CLOSE, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(self->dialog), 520, 240);
    auto* content = Column(12);
    for (auto setter : {gtk_widget_set_margin_start, gtk_widget_set_margin_end, gtk_widget_set_margin_top, gtk_widget_set_margin_bottom}) setter(content, 18);
    Add(content, Label("中文 ⇄ English", "section"));
    auto* info = Label("首次安装约需下载 180–204 MB。安装完成后无需网络，输入内容保留在本机。", "caption");
    gtk_label_set_line_wrap(GTK_LABEL(info), TRUE);
    Add(content, info);
    self->status = Label("正在检查语言资源…");
    gtk_label_set_line_wrap(GTK_LABEL(self->status), TRUE);
    Add(content, self->status);
    self->progress = gtk_progress_bar_new();
    Add(content, self->progress);
    auto* buttons = Row(8);
    const auto weak = std::weak_ptr<Download>(self);
    self->install = Button("安装 / 修复", [weak] { if (auto s = weak.lock()) s->Start("install"); });
    self->stop = Button("取消下载", [weak] { if (auto s = weak.lock()) { s->Cancel(); s->Controls(); } });
    self->check = Button("重新检查", [weak] { if (auto s = weak.lock()) s->Start("status"); });
    for (auto* button : {self->install, self->stop, self->check}) Add(buttons, button);
    Add(content, buttons);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(self->dialog))), content);
    gtk_widget_show_all(self->dialog);
    self->Start("status");
    gtk_dialog_run(GTK_DIALOG(self->dialog));
    self->alive = false;
    self->Cancel();
    gtk_widget_destroy(self->dialog);
}
} // namespace rimes::desktop
