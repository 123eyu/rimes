#include "background_jobs.hpp"
#include "capsule_resources.hpp"
#include "capsule_text.hpp"
#include "desktop.hpp"
#include "terminal_files.hpp"
#include "terminal_workspace.hpp"
#include <algorithm>
#include <mutex>
namespace rimes::mailbox {
namespace {
using namespace rimes::desktop;
GtkWidget* Named(GtkWidget* widget, const char* name) {
    gtk_widget_set_name(widget, name);
    return widget;
}
GtkWidget* Pad(GtkWidget* child, int inset) {
    auto* box = Column(0);
    gtk_container_set_border_width(GTK_CONTAINER(box), inset);
    Add(box, child, true);
    return box;
}
GtkWidget* Scroll(GtkWidget* child) {
    auto* view = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(view), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(view), child);
    return view;
}
GtkWidget* Paragraph(const std::string& text, const char* style = "terminal-paragraph") {
    auto* label = Label(text, style);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_NONE);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 1);
    return label;
}
GtkWidget* Tool(const char* icon, const char* label, std::function<void()> action) {
    auto* button = Class(IconButton(icon, label, std::move(action)), "mailbox-tool");
    gtk_widget_set_size_request(button, 28, 28);
    return button;
}
GtkWidget* Action(const std::string& label, const char* icon, std::function<void()> action) {
    auto* button = Button("", std::move(action), "mailbox-action");
    auto* face = Row(5);
    gtk_widget_set_halign(face, GTK_ALIGN_CENTER);
    Add(face, Icon(icon, 14));
    Add(face, Label(label));
    Clear(button);
    gtk_container_add(GTK_CONTAINER(button), face);
    gtk_widget_set_size_request(button, -1, 28);
    return button;
}
void Post(std::function<void()> action) {
    g_idle_add_full(
        G_PRIORITY_DEFAULT,
        +[](gpointer raw) -> gboolean {
            (*static_cast<std::function<void()>*>(raw))();
            return G_SOURCE_REMOVE;
        },
        new std::function<void()>(std::move(action)),
        +[](gpointer raw) { delete static_cast<std::function<void()>*>(raw); });
}
void Detach(GtkWidget* widget) {
    if (auto* parent = gtk_widget_get_parent(widget))
        gtk_container_remove(GTK_CONTAINER(parent), widget);
}
} // namespace
struct TerminalWorkspace::Impl {
    struct Access {
        std::mutex mutex;
        std::uint64_t epoch = 0;
        bool enabled = false, alive = true;
    };
    GtkWindow* parent;
    GtkWidget *root, *layout, *sidebar, *inspector, *main, *index, *inspector_body, *host, *title,
        *path, *status, *stop, *kind, *scrim, *dialog = nullptr;
    std::vector<GtkWidget*> tabs;
    std::vector<std::unique_ptr<TerminalSession>> sessions;
    std::function<void(const std::string&)> open_receipt;
    std::function<void()> changed;
    std::shared_ptr<Access> access = std::make_shared<Access>();
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    runtime::BackgroundJobs jobs;
    std::shared_ptr<std::atomic<bool>> work = std::make_shared<std::atomic<bool>>(false);
    Snapshot snapshot{{{"threads", Json::array()}}, ""};
    std::string selected, mounted, preview_session, notice;
    std::optional<TerminalFile> preview;
    std::vector<std::filesystem::path> files;
    bool enabled = false, collecting = false, loading = false, list_visible = false,
         inspector_visible = false, list_overlay = false, inspector_overlay = false;
    std::optional<bool> list_preference, inspector_preference;
    int tab = 0, allocated_width = 0;
    Impl(GtkWindow* parent, std::function<void(const std::string&)> receipt,
         std::function<void()> changed)
        : parent(parent), open_receipt(std::move(receipt)), changed(std::move(changed)) {
        root = Named(gtk_overlay_new(), "mailbox-terminal");
        g_object_ref_sink(root);
        gtk_widget_set_no_show_all(root, TRUE);
        layout = Row(0);
        gtk_container_add(GTK_CONTAINER(root), layout);
        sidebar = Class(Column(0), "mailbox-side");
        main = Column(0);
        inspector = Class(Column(0), "mailbox-side");
        Add(layout, sidebar);
        Add(layout, main, true);
        Add(layout, inspector);
        auto* header = Column(12);
        auto* heading = Row(0);
        Add(heading, Label("终端", "mailbox-list-title"), true);
        Add(heading, Tool("chevron.left", "收起会话列表", [this] { Toggle(false); }));
        Add(header, heading);
        kind = Named(gtk_combo_box_text_new(), "terminal-kind");
        for (const auto* name : {"Codex", "Claude Code", "Shell"})
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(kind), name);
        gtk_combo_box_set_active(GTK_COMBO_BOX(kind), 0);
        Add(header, kind);
        auto* create = Named(Action("新建会话", "plus", [this] { Choose(true); }), "terminal-new");
        gtk_widget_set_size_request(create, -1, 28);
        Add(header, create);
        Add(sidebar, Pad(header, 16));
        index = Column(0);
        Add(sidebar, Scroll(index), true);
        auto* top = Row(4);
        auto* labels = Column(4);
        title = Label("终端", "terminal-title");
        path = Label("交互式命令行工作区", "mailbox-source");
        Add(labels, title);
        Add(labels, path);
        Add(top, labels, true);
        Add(top, Named(Tool("sidebar.left", "显示或隐藏会话列表", [this] { Toggle(false); }),
                       "terminal-toggle-list"));
        stop = Named(Tool("stop", "结束当前终端进程",
                          [this] {
                              if (enabled && Current())
                                  Current()->Stop();
                          }),
                     "terminal-stop");
        Add(top, stop);
        Add(top, Named(Tool("sidebar.right", "显示或隐藏扩展区", [this] { Toggle(true); }),
                       "terminal-toggle-inspector"));
        Add(main, Pad(top, 16));
        host = Named(Class(Column(0), "terminal-host"), "terminal-host");
        Add(main, host, true);
        status = Named(Label("会话在本次 RIMES 运行期间保留", "mailbox-source"), "terminal-status");
        Add(main, Pad(status, 10));
        auto* tools = Column(12);
        auto* inspector_top = Row(4);
        Add(inspector_top, Label("扩展区", "terminal-panel-title"), true);
        Add(inspector_top,
            Named(Tool("plus", "选择文件", [this] { Choose(false); }), "terminal-choose-file"));
        Add(inspector_top, Tool("chevron.right", "收起扩展区", [this] { Toggle(true); }));
        Add(tools, inspector_top);
        auto* segments = Class(Row(0), "mailbox-filters");
        gtk_box_set_homogeneous(GTK_BOX(segments), TRUE);
        const std::vector<std::string> names{"产物", "文件", "会话"};
        for (int i = 0; i != 3; ++i) {
            auto* button = Button(
                names[i],
                [this, i] {
                    tab = i;
                    RenderInspector();
                    if (i == 1)
                        RefreshFiles();
                },
                "mailbox-filter");
            gtk_widget_set_name(button, ("terminal-tab-" + std::to_string(i)).c_str());
            tabs.push_back(button);
            Add(segments, button, true);
        }
        Add(tools, segments);
        Add(inspector, Pad(tools, 16));
        inspector_body = Column(0);
        Add(inspector, Scroll(inspector_body), true);
        scrim = Class(Button("", [this] { Dismiss(); }), "mailbox-scrim");
        gtk_widget_set_no_show_all(scrim, TRUE);
        gtk_overlay_add_overlay(GTK_OVERLAY(root), scrim);
        for (auto* panel : {sidebar, inspector}) {
            gtk_widget_show_all(panel);
            gtk_widget_set_no_show_all(panel, TRUE);
        }
        g_signal_connect(root, "size-allocate",
                         G_CALLBACK(+[](GtkWidget*, GtkAllocation* allocation, gpointer p) {
                             auto* self = static_cast<Impl*>(p);
                             if (self->allocated_width == allocation->width)
                                 return;
                             self->allocated_width = allocation->width;
                             // Visibility changes during size-allocate can leave
                             // GtkBox's old child allocations in place. Apply
                             // them on the next loop so hidden panels give their
                             // space back to the terminal immediately.
                             Post([self, life = self->alive] {
                                 if (life->load())
                                     self->Layout(self->allocated_width);
                             });
                         }),
                         this);
        auto* css = gtk_css_provider_new();
        gtk_css_provider_load_from_data(
            css,
            ".mailbox .terminal-panel-title{font-size:13px;font-weight:600;}"
            ".mailbox .terminal-title{font-size:15px;font-weight:600;}"
            ".mailbox .terminal-host,.mailbox .terminal-host viewport{background:rgb(23,28,33);}"
            ".mailbox .terminal-welcome{font-size:24px;font-weight:500;color:rgb(219,219,219);}"
            ".mailbox .terminal-hint{font-size:14px;color:rgb(148,148,148);}"
            ".mailbox .terminal-paragraph{font-size:12px;}"
            ".mailbox .terminal-host vte-terminal{padding:0;font-family:monospace;font-size:13px;}"
            ".mailbox #terminal-kind button{font-size:12px;min-height:0;padding:0 4px;}"
            ".mailbox #terminal-status{font-size:11px;}"
            ".mailbox #mailbox-terminal .mailbox-filter{min-height:22px;}",
            -1, nullptr);
        gtk_style_context_add_provider_for_screen(gtk_widget_get_screen(root),
                                                  GTK_STYLE_PROVIDER(css),
                                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 2);
        g_object_set_data_full(G_OBJECT(root), "terminal-css", css, g_object_unref);
        Mount();
        Render();
        gtk_widget_show_all(layout);
    }
    ~Impl() {
        Suspend();
        alive->store(false);
        {
            std::lock_guard<std::mutex> lock(access->mutex);
            access->alive = false;
        }
        jobs.Shutdown();
        sessions.clear();
        auto* css = static_cast<GtkCssProvider*>(g_object_get_data(G_OBJECT(root), "terminal-css"));
        gtk_style_context_remove_provider_for_screen(gtk_widget_get_screen(root),
                                                     GTK_STYLE_PROVIDER(css));
        g_signal_handlers_disconnect_by_data(root, this);
        gtk_widget_destroy(root);
        g_object_unref(root);
    }
    TerminalSession* Current() const {
        for (const auto& session : sessions)
            if (session->ID() == selected)
                return session.get();
        return nullptr;
    }
    void Suspend() {
        work->store(true);
        jobs.CancelAll();
        collecting = loading = false;
        {
            std::lock_guard<std::mutex> lock(access->mutex);
            ++access->epoch;
        }
        if (dialog) {
            gtk_widget_destroy(dialog);
            dialog = nullptr;
        }
    }
    void SetEnabled(bool value) {
        if (enabled && !value)
            Suspend();
        enabled = value;
        {
            std::lock_guard<std::mutex> lock(access->mutex);
            access->enabled = value;
        }
        gtk_widget_set_sensitive(root, value);
        for (auto& session : sessions)
            session->SetInputEnabled(value);
    }
    void Notice(const std::string& value) {
        notice = value;
        gtk_label_set_text(GTK_LABEL(status), value.c_str());
    }
    std::string Launch(const std::string& name, const std::filesystem::path& directory,
                       const std::filesystem::path& executable,
                       const std::vector<std::string>& arguments) {
        if (!enabled)
            throw std::runtime_error("终端工作区不可用");
        const auto raw_directory = directory.string();
        if (!g_utf8_validate(raw_directory.c_str(), raw_directory.size(), nullptr) ||
            raw_directory.find_first_of("\r\n") != raw_directory.npos)
            throw std::runtime_error("工作目录名称需要有效的 UTF-8 文本");
        if (name != "Shell" && name != "Codex" && name != "Claude Code")
            throw std::runtime_error("未知终端类型");
        if (sessions.size() >= 16)
            throw std::runtime_error("本次运行最多保留 16 个终端会话");
        auto session = std::make_unique<TerminalSession>(name, directory, [this, life = alive] {
            if (life->load())
                Render();
        });
        const auto id = session->ID();
        sessions.insert(sessions.begin(), std::move(session));
        Select(id);
        try {
            sessions.front()->Start(executable, arguments);
        } catch (...) {
            sessions.erase(sessions.begin());
            selected.clear();
            Mount();
            Render();
            throw;
        }
        Render();
        gtk_widget_grab_focus(Current()->Widget());
        return id;
    }
    bool Select(const std::string& id) {
        if (!enabled)
            return false;
        if (std::none_of(sessions.begin(), sessions.end(),
                         [&](const auto& session) { return session->ID() == id; }))
            return false;
        Suspend();
        selected = id;
        preview.reset();
        preview_session.clear();
        files.clear();
        notice.clear();
        Mount();
        Dismiss();
        Render();
        if (tab == 1)
            RefreshFiles();
        return true;
    }
    void Mount() {
        for (const auto& session : sessions)
            if (gtk_widget_get_parent(session->Widget()))
                Detach(session->Widget());
        Clear(host);
        if (auto* current = Current()) {
            auto* padding =
                Pad(current->Widget(), TerminalContract().at("metrics").at("terminalInset"));
            Add(host, padding, true);
            mounted = selected;
        } else {
            auto* empty = Column(18);
            Add(empty, Paragraph("你的终端，也有一个工作台", "terminal-welcome"));
            Add(empty, Paragraph("从左侧选择 Codex、Claude Code 或 "
                                 "Shell，\n新建会话后即可直接操作终端。\n\n右侧可以预览文件，把选中"
                                 "的成果收入 Mailbox。",
                                 "terminal-hint"));
            Add(empty, gtk_label_new(""), true);
            // Wrapped guidance must not impose a minimum window height when
            // adaptive side panels collapse and the text width changes.
            Add(host, Scroll(Pad(empty, 30)), true);
            mounted.clear();
        }
        gtk_widget_show_all(host);
    }
    void Render() {
        Clear(index);
        for (const auto& session : sessions) {
            const auto data = session->Inspect();
            const std::string id = session->ID();
            auto* button = Button("", [this, id] { Select(id); }, "mailbox-list-row");
            if (id == selected)
                Class(button, "selected");
            auto* lines = Column(5);
            Add(lines, Label(data.at("title"), "mailbox-list-name"));
            Add(lines, Label(data.at("directory"), "mailbox-list-preview"));
            Add(lines, Label(data.at("kind").get<std::string>() +
                                 (data.at("running") == true ? " · 运行中" : " · 已结束"),
                             "mailbox-list-meta"));
            Clear(button);
            gtk_container_add(GTK_CONTAINER(button), Pad(lines, 10));
            gtk_widget_set_size_request(button, -1, 84);
            Add(index, button);
        }
        if (sessions.empty())
            Add(index, Pad(Paragraph("会话将在这里保留。\n切换工作区不会中断运行。"), 18));
        auto* current = Current();
        const auto data = current ? current->Inspect() : Json::object();
        const std::string heading = current ? data.at("kind").get<std::string>() + " · " +
                                                  data.at("title").get<std::string>()
                                            : "终端";
        const auto location = current ? current->Directory().string() : "交互式命令行工作区";
        gtk_label_set_text(GTK_LABEL(title), heading.c_str());
        gtk_widget_set_tooltip_text(title, heading.c_str());
        gtk_label_set_text(GTK_LABEL(path), location.c_str());
        gtk_widget_set_tooltip_text(path, location.c_str());
        gtk_widget_set_sensitive(
            stop, current && (data.at("running") == true || data.at("starting") == true) &&
                      data.at("stopping") == false);
        std::string state = "会话在本次 RIMES 运行期间保留";
        if (current) {
            if (!data.at("error").get<std::string>().empty())
                state = data.at("error");
            else if (data.at("starting") == true)
                state = "正在启动终端…";
            else if (data.at("stopping") == true && data.at("running") == true)
                state = "正在结束终端进程…";
            else if (data.at("running") == true)
                state = "● 终端运行中 · 关闭窗口后继续运行";
            else
                state = "终端已结束 · " + (data.at("exitCode").is_null()
                                               ? std::string("未取得退出状态")
                                               : "退出状态 " + data.at("exitCode").dump());
        }
        gtk_label_set_text(GTK_LABEL(status), notice.empty() ? state.c_str() : notice.c_str());
        RenderInspector();
        gtk_widget_show_all(index);
    }
    void RenderInspector() {
        for (int i = 0; i != 3; ++i) {
            auto* style = gtk_widget_get_style_context(tabs[i]);
            if (i == tab)
                gtk_style_context_add_class(style, "selected");
            else
                gtk_style_context_remove_class(style, "selected");
        }
        Clear(inspector_body);
        auto* body = Column(14);
        if (preview && preview_session == selected && tab != 2) {
            Add(body, Label(preview->path.filename().string(), "mailbox-list-name"));
            if (preview->png.empty())
                Add(body,
                    Paragraph(capsule::CharacterPrefix(
                        preview->body, TerminalContract().at("limits").at("previewCharacters"))));
            else {
                auto* loader = gdk_pixbuf_loader_new();
                // Only our bounded, normalized PNG reaches the GTK process.
                gdk_pixbuf_loader_set_size(loader, 208, 168);
                gdk_pixbuf_loader_write(loader,
                                        reinterpret_cast<const guchar*>(preview->png.data()),
                                        preview->png.size(), nullptr);
                gdk_pixbuf_loader_close(loader, nullptr);
                auto* image = gtk_image_new_from_pixbuf(gdk_pixbuf_loader_get_pixbuf(loader));
                gtk_widget_set_size_request(image, -1, 168);
                Add(body, image);
                g_object_unref(loader);
            }
            auto* collect = Named(Action(collecting ? "正在收取…" : "收入收件箱",
                                         "tray.and.arrow.down", [this] { Collect(); }),
                                  "terminal-collect");
            gtk_widget_set_sensitive(collect, !collecting && !loading);
            gtk_widget_set_size_request(collect, -1, 28);
            Add(body, collect);
            Add(body, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
        }
        if (tab == 1) {
            Add(body, Label("工作目录 · 最多显示 60 个文件", "mailbox-source"));
            auto* refresh = Named(Action("刷新文件", "arrow.clockwise", [this] { RefreshFiles(); }),
                                  "terminal-refresh-files");
            Add(body, refresh);
            gtk_widget_set_sensitive(refresh, !collecting);
            for (const auto& file : files) {
                auto* button = Button(
                    file.filename().string(), [this, file] { Preview(file); }, "mailbox-tool");
                gtk_widget_set_tooltip_text(button, file.filename().c_str());
                Add(body, button);
            }
            if (files.empty())
                Add(body,
                    Paragraph("此目录暂无文件。也可以用上方的“选择文件”打开子目录中的产物。"));
        } else if (tab == 2) {
            if (auto* current = Current()) {
                const auto data = current->Inspect();
                Add(body, Label(data.at("kind"), "mailbox-heading"));
                auto time = static_cast<time_t>(data.at("startedAt").get<double>() + 978307200);
                std::tm local{};
                localtime_r(&time, &local);
                char formatted[32]{};
                std::strftime(formatted, sizeof(formatted), "%H:%M", &local);
                Add(body, Paragraph("启动于 " + std::string(formatted) + "\n\n" +
                                    current->Directory().string() + "\n\n" +
                                    (data.at("running") == true ? "进程正在运行" : "进程已结束") +
                                    "\n会话 ID · " + selected.substr(0, 8)));
                const auto uri = capsule::ResourceUri(current->Directory());
                Add(body, Button("在文件管理器中打开", [this, uri] {
                        if (!enabled)
                            return;
                        GError* error = nullptr;
                        if (!gtk_show_uri_on_window(parent, uri.c_str(), GDK_CURRENT_TIME, &error))
                            Notice("无法打开文件管理器");
                        g_clear_error(&error);
                    }));
                Add(body,
                    Paragraph(
                        "终端进程状态不代表某项任务已完成。选中要保存的文件后，可手动收取成果。"));
            } else
                Add(body, Paragraph("新建一个终端会话后，查看工作目录和运行状态。"));
        } else {
            std::vector<Json> receipts;
            for (const auto& receipt : snapshot.document.at("threads"))
                if (!selected.empty() &&
                    receipt.value("terminalSessionID", Json(nullptr)) == selected)
                    receipts.push_back(receipt);
            Add(body, Label("已收取 · " + std::to_string(receipts.size()), "mailbox-author"));
            for (const auto& receipt : receipts) {
                const std::string id = receipt.at("id");
                Add(body, Button(Title(receipt), [this, id] {
                        if (enabled)
                            open_receipt(id);
                    }));
            }
            if (receipts.empty())
                Add(body,
                    Paragraph(
                        "选择本次工作的图片或文本，预览后收入收件箱。收件会保留与此终端的关联。"));
            Add(body, Paragraph("切换工作区后，终端会继续运行。\n已收取的内容会保存在本地。",
                                "mailbox-source"));
        }
        Add(inspector_body, Pad(body, 16));
        gtk_widget_show_all(inspector_body);
    }
    void RefreshFiles() {
        if (!enabled || !Current() || collecting)
            return;
        work->store(true);
        work = std::make_shared<std::atomic<bool>>(false);
        auto token = work;
        const auto id = selected;
        const auto directory = Current()->Directory();
        loading = true;
        if (!jobs.Run(token, [this, life = alive, token, id, directory] {
                std::vector<std::filesystem::path> result;
                std::string error;
                try {
                    result = TerminalFiles(directory, *token);
                } catch (const std::exception& e) {
                    error = e.what();
                }
                Post([this, life, token, id, result, error] {
                    if (!life->load() || token->load() || id != selected)
                        return;
                    loading = false;
                    files = result;
                    if (!error.empty())
                        Notice(error);
                    RenderInspector();
                });
            })) {
            loading = false;
            Notice("文件任务繁忙，请重试");
        }
    }
    void Preview(const std::filesystem::path& file) {
        if (!enabled || !Current() || collecting)
            return;
        work->store(true);
        work = std::make_shared<std::atomic<bool>>(false);
        auto token = work;
        const auto id = selected;
        preview.reset();
        preview_session.clear();
        loading = true;
        RenderInspector();
        Notice("正在预览文件…");
        if (!jobs.Run(token, [this, life = alive, token, id, file] {
                std::optional<TerminalFile> result;
                std::string error;
                try {
                    result = ReadTerminalFile(file, *token);
                } catch (const std::exception& e) {
                    error = e.what();
                }
                Post([this, life, token, id, result = std::move(result), error] {
                    if (!life->load() || token->load() || id != selected)
                        return;
                    loading = false;
                    preview = result;
                    preview_session = result ? id : "";
                    if (result)
                        tab = 0;
                    Notice(error);
                    Render();
                });
            })) {
            loading = false;
            Notice("文件任务繁忙，请重试");
        }
    }
    void Collect() {
        if (!enabled || !Current() || !preview || preview_session != selected || collecting ||
            loading)
            return;
        work->store(true);
        work = std::make_shared<std::atomic<bool>>(false);
        auto token = work;
        const auto file = *preview;
        const auto id = selected;
        const auto source = Current()->Inspect().at("kind").get<std::string>() + " · " +
                            Current()->Directory().filename().string();
        const auto gate = access;
        std::uint64_t epoch;
        {
            std::lock_guard<std::mutex> lock(gate->mutex);
            epoch = gate->epoch;
        }
        collecting = true;
        RenderInspector();
        if (!jobs.Run(token, [this, life = alive, gate, epoch, token, file, id, source] {
                std::string error;
                try {
                    Store store(runtime::UserRoot() / "mailbox", {},
                                [gate, epoch, token](const auto& publish) {
                                    std::lock_guard<std::mutex> lock(gate->mutex);
                                    if (gate->alive && gate->enabled && gate->epoch == epoch &&
                                        !token->load())
                                        publish();
                                });
                    store.ImportTerminalResult(id, source, file.path.filename().string(), file.body,
                                               file.png);
                } catch (const std::exception& e) {
                    error = e.what();
                }
                Post([this, life, token, id, error] {
                    if (!life->load() || token->load() || id != selected)
                        return;
                    collecting = false;
                    if (error.empty()) {
                        preview.reset();
                        preview_session.clear();
                        changed();
                        Notice("已收入收件箱");
                    } else
                        Notice(error);
                    RenderInspector();
                });
            })) {
            collecting = false;
            Notice("文件任务繁忙，请重试");
            RenderInspector();
        }
    }
    void Choose(bool directory) {
        if (!enabled || dialog || collecting)
            return;
        if (!directory && !Current()) {
            Notice("请先新建终端会话");
            return;
        }
        auto* raw = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(kind));
        const std::string chosen_kind = raw ? raw : "Shell";
        g_free(raw);
        dialog = gtk_file_chooser_dialog_new(
            directory ? "选择此终端会话的工作目录" : "选择此会话的图片或文本成果", parent,
            directory ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER : GTK_FILE_CHOOSER_ACTION_OPEN,
            "取消", GTK_RESPONSE_CANCEL, directory ? "启动会话" : "预览", GTK_RESPONSE_ACCEPT,
            nullptr);
        gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
        if (Current())
            gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),
                                                Current()->Directory().c_str());
        struct Choice {
            Impl* self;
            std::string session, kind;
            bool directory;
        };
        auto* choice = new Choice{this, selected, chosen_kind, directory};
        g_signal_connect_data(
            dialog, "response", G_CALLBACK(+[](GtkDialog* view, gint response, gpointer raw) {
                const auto choice = *static_cast<Choice*>(raw);
                auto* self = choice.self;
                auto* filename = response == GTK_RESPONSE_ACCEPT
                                     ? gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(view))
                                     : nullptr;
                const std::filesystem::path file = filename ? filename : "";
                g_free(filename);
                gtk_widget_destroy(GTK_WIDGET(view));
                if (file.empty() || !self->enabled ||
                    (!choice.directory && self->selected != choice.session))
                    return;
                try {
                    if (choice.directory)
                        self->Launch(choice.kind, file, TerminalExecutable(choice.kind),
                                     choice.kind == "Shell" ? std::vector<std::string>{"-l"}
                                                            : std::vector<std::string>{});
                    else
                        self->Preview(file);
                } catch (const std::exception& e) {
                    self->Notice(e.what());
                }
            }),
            choice, +[](gpointer raw, GClosure*) { delete static_cast<Choice*>(raw); },
            G_CONNECT_DEFAULT);
        g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer raw) {
                             static_cast<Impl*>(raw)->dialog = nullptr;
                         }),
                         this);
        gtk_widget_show_all(dialog);
    }
    void Layout(int width) {
        bool left = list_preference.value_or(width >= 720),
             right = inspector_preference.value_or(width >= 920);
        bool left_over = left && width < 720, right_over = right && width < 920;
        if (left_over && right_over)
            left = left_over = false;
        auto panel = [&](GtkWidget* widget, bool visible, bool over, bool was_over, int size,
                         bool is_right) {
            if (over != was_over) {
                g_object_ref(widget);
                Detach(widget);
                if (over)
                    gtk_overlay_add_overlay(GTK_OVERLAY(root), widget);
                else {
                    Add(layout, widget);
                    gtk_box_reorder_child(GTK_BOX(layout), widget, is_right ? -1 : 0);
                }
                g_object_unref(widget);
            }
            gtk_widget_set_size_request(widget, size, -1);
            gtk_widget_set_halign(widget, over ? (is_right ? GTK_ALIGN_END : GTK_ALIGN_START)
                                               : GTK_ALIGN_FILL);
            gtk_widget_set_visible(widget, visible);
            if (over)
                gtk_overlay_reorder_overlay(GTK_OVERLAY(root), widget, -1);
        };
        panel(sidebar, left, left_over, list_overlay, 200, false);
        panel(inspector, right, right_over, inspector_overlay, 240, true);
        list_visible = left;
        inspector_visible = right;
        list_overlay = left_over;
        inspector_overlay = right_over;
        gtk_widget_set_visible(scrim, left_over || right_over);
    }
    void Toggle(bool right) {
        auto& preference = right ? inspector_preference : list_preference;
        const bool visible = right ? inspector_visible : list_visible,
                   overlay = right ? inspector_overlay : list_overlay;
        preference = visible ? (overlay ? std::optional<bool>{} : std::optional<bool>{false})
                             : std::optional<bool>{true};
        if (right && list_overlay)
            list_preference.reset();
        if (!right && inspector_overlay)
            inspector_preference.reset();
        Layout(gtk_widget_get_allocated_width(root));
    }
    void Dismiss() {
        if (list_overlay)
            list_preference.reset();
        if (inspector_overlay)
            inspector_preference.reset();
        Layout(gtk_widget_get_allocated_width(root));
    }
};
TerminalWorkspace::TerminalWorkspace(GtkWindow* parent,
                                     std::function<void(const std::string&)> receipt,
                                     std::function<void()> changed)
    : impl_(std::make_unique<Impl>(parent, std::move(receipt), std::move(changed))) {}
TerminalWorkspace::~TerminalWorkspace() = default;
GtkWidget* TerminalWorkspace::Widget() const { return impl_->root; }
std::string TerminalWorkspace::Launch(const std::string& kind,
                                      const std::filesystem::path& directory,
                                      const std::filesystem::path& executable,
                                      const std::vector<std::string>& arguments) {
    return impl_->Launch(kind, directory, executable, arguments);
}
bool TerminalWorkspace::Select(const std::string& id) { return impl_->Select(id); }
void TerminalWorkspace::SetEnabled(bool enabled) { impl_->SetEnabled(enabled); }
void TerminalWorkspace::Suspend() { impl_->Suspend(); }
void TerminalWorkspace::Refresh(const Snapshot& state) {
    impl_->snapshot = state;
    impl_->Render();
}
void TerminalWorkspace::Preview(const std::filesystem::path& path) { impl_->Preview(path); }
void TerminalWorkspace::Collect() { impl_->Collect(); }
Json TerminalWorkspace::Inspect() const {
    Json sessions = Json::array();
    for (const auto& session : impl_->sessions)
        sessions.push_back(session->Inspect());
    return {{"sessions", sessions},
            {"selected", impl_->selected},
            {"previewSession", impl_->preview_session},
            {"preview", impl_->preview ? impl_->preview->path.filename().string() : ""},
            {"collecting", impl_->collecting},
            {"loading", impl_->loading},
            {"enabled", impl_->enabled},
            {"listVisible", impl_->list_visible},
            {"inspectorVisible", impl_->inspector_visible},
            {"listOverlay", impl_->list_overlay},
            {"inspectorOverlay", impl_->inspector_overlay}};
}
} // namespace rimes::mailbox
