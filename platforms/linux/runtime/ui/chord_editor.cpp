#include "chord_editor.hpp"
#include "chord_learning.hpp"
#include "chord_protocol.hpp"
#include "chord_store.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

namespace rimes::desktop {
namespace {
using namespace rimes::chord;
GtkWidget* ChordLabel(const std::string& value, const char* style = nullptr) {
    auto* label = Class(Label(value, style), "chord-label");
    auto* attributes = pango_attr_list_new();
    const std::string name = style ? style : "";
    const int height = name == "chord-target"     ? 40
                       : name == "chord-hint"     ? 17
                       : name == "chord-feedback" ? 15
                       : name == "tiny"           ? 11
                       : name == "caption"        ? 12
                                                  : 14;
    pango_attr_list_insert(attributes, pango_attr_line_height_new_absolute(height * PANGO_SCALE));
    gtk_label_set_attributes(GTK_LABEL(label), attributes);
    pango_attr_list_unref(attributes);
    if (name == "caption")
        gtk_widget_set_size_request(label, -1, 13);
    return label;
}
GtkWidget* Named(GtkWidget* widget, const char* name) {
    atk_object_set_name(gtk_widget_get_accessible(widget), name);
    return widget;
}
void Text(GtkWidget* label, const std::string& value) {
    if (label)
        gtk_label_set_text(GTK_LABEL(label), value.c_str());
}
std::string EntryText(GtkWidget* entry) { return gtk_entry_get_text(GTK_ENTRY(entry)); }
void MappingButtonTitle(GtkWidget* button, const char* text) {
    // Keep the compact label's line metrics when changing Add to Update.
    auto* label = gtk_bin_get_child(GTK_BIN(button));
    if (GTK_IS_LABEL(label))
        gtk_label_set_text(GTK_LABEL(label), text);
}
GtkWidget* EntryField(const char* name, const std::string& value, int width) {
    auto* entry = Named(gtk_entry_new(), name);
    gtk_entry_set_text(GTK_ENTRY(entry), value.c_str());
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 1);
    gtk_widget_set_size_request(entry, width, 19);
    return entry;
}
GtkWidget* Choice(const char* name, const std::vector<std::string>& values, int active) {
    auto* widget = Named(gtk_combo_box_text_new(), name);
    for (const auto& value : values)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(widget), value.c_str());
    gtk_combo_box_set_active(GTK_COMBO_BOX(widget), active);
    auto* cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(widget));
    for (auto* cell = cells; cell; cell = cell->next)
        if (GTK_IS_CELL_RENDERER_TEXT(cell->data))
            g_object_set(cell->data, "ypad", 0, "height", 16, nullptr);
    g_list_free(cells);
    return widget;
}
std::string Upper(std::string value) {
    for (auto& ch : value)
        if (ch >= 'a' && ch <= 'z')
            ch -= 'a' - 'A';
    return value;
}
std::string ReadImport(const std::string& path) {
    const int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0)
        throw std::runtime_error("无法读取键位文件");
    struct Close {
        int fd;
        ~Close() { close(fd); }
    } close_file{fd};
    struct stat st {};
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 ||
        static_cast<std::size_t>(st.st_size) > kMaximumProfileBytes)
        throw std::runtime_error("请选择不超过 2 MiB 的 JSON 键位文件");
    std::string bytes(st.st_size, '\0');
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = read(fd, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            throw std::runtime_error("键位文件读取不完整");
        offset += static_cast<std::size_t>(count);
    }
    char extra;
    if (read(fd, &extra, 1) != 0)
        throw std::runtime_error("键位文件读取时发生变化");
    return bytes;
}
Json Unavailable() {
    return {{"ok", false},
            {"message", "无法连接 XSIME，请启动 Fcitx5 并添加 XSIME。草稿仍可编辑和保存。"}};
}
struct Call {
    std::string bytes;
    GCancellable* cancel;
    std::function<void(Json)> done;
    ~Call() {
        if (cancel)
            g_object_unref(cancel);
    }
};
} // namespace
void ChordCall(const Json& command, GCancellable* cancel, std::function<void(Json)> completion) {
    auto bytes = command.dump();
    if (bytes.size() > chord::kMaximumProfileBytes + 4096) {
        completion({{"ok", false}, {"message", "键位命令超过大小限制"}});
        return;
    }
    auto* call = new Call{std::move(bytes), cancel ? G_CANCELLABLE(g_object_ref(cancel)) : nullptr,
                          std::move(completion)};
    g_bus_get(
        G_BUS_TYPE_SESSION, cancel,
        +[](GObject*, GAsyncResult* result, gpointer data) {
            std::unique_ptr<Call> call(static_cast<Call*>(data));
            GError* error = nullptr;
            auto* bus = g_bus_get_finish(result, &error);
            g_clear_error(&error);
            if (!bus) {
                call->done(Unavailable());
                return;
            }
            auto* pending = call.release();
            g_dbus_connection_call(
                bus, chord::kBusName, chord::kBusPath, chord::kBusInterface, "Command",
                g_variant_new("(s)", pending->bytes.c_str()), G_VARIANT_TYPE("(s)"),
                G_DBUS_CALL_FLAGS_NO_AUTO_START, 3000, pending->cancel,
                +[](GObject* source, GAsyncResult* result, gpointer data) {
                    std::unique_ptr<Call> call(static_cast<Call*>(data));
                    GError* error = nullptr;
                    auto* reply =
                        g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
                    g_clear_error(&error);
                    auto value = Unavailable();
                    if (reply) {
                        const char* text = nullptr;
                        g_variant_get(reply, "(&s)", &text);
                        if (text && std::strlen(text) <= chord::kMaximumProfileBytes + 16384) {
                            auto parsed = Json::parse(text, nullptr, false);
                            if (parsed.is_object() && parsed.contains("ok") &&
                                parsed["ok"].is_boolean())
                                value = std::move(parsed);
                        }
                        g_variant_unref(reply);
                    }
                    call->done(std::move(value));
                },
                pending);
            g_object_unref(bus);
        },
        call);
}
struct ChordPane::Impl {
    GtkWindow* parent;
    std::string page;
    bool rendering, updating = false, applying = false, request_pending = false, connected = false;
    bool rebuild_pending = false;
    std::function<void()> configuration_changed;
    Store store{runtime::UserRoot()};
    Profile profile = Profile::BuiltIn();
    ActiveProfile active = store.Active();
    Json baseline, status;
    std::string draft_revision, message;
    GtkWidget *root = nullptr, *feedback = nullptr, *profile_picker = nullptr,
              *profile_status = nullptr, *name = nullptr, *left = nullptr, *right = nullptr,
              *boundary = nullptr, *encoding = nullptr, *search = nullptr, *table = nullptr,
              *keys = nullptr, *output = nullptr, *kind = nullptr, *mode = nullptr,
              *preview = nullptr, *duration = nullptr, *availability = nullptr, *detail = nullptr,
              *make_current = nullptr, *implementation = nullptr, *save_entry = nullptr;
    std::vector<Profile> profiles;
    std::map<char, GtkWidget*> keyboard;
    GtkListStore* rows = nullptr;
    int selected_entry = -1, keyboard_mode = 0;
    std::string entry_baseline;
    GtkWidget *capture = nullptr, *capture_label = nullptr, *capture_toggle = nullptr;
    bool capturing = false, advancing = false;
    std::map<unsigned, char> down;
    std::set<char> captured;
    std::function<void(std::string)> on_chord;
    guint poll = 0, advance = 0;
    std::vector<Course> courses;
    std::vector<Exercise> exercises;
    std::unique_ptr<ProgressStore> progress;
    GtkWidget *course_picker = nullptr, *target = nullptr, *hint = nullptr, *score = nullptr,
              *practice_feedback = nullptr;
    std::size_t exercise_index = 0, streak = 0;
    GCancellable* cancel = g_cancellable_new();
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    Impl(GtkWindow* window, std::string selected, bool render, std::function<void()> changed,
         const Json& fixture_status)
        : parent(window), page(std::move(selected)), rendering(render),
          configuration_changed(std::move(changed)) {
        root = Class(Column(page == "keymap" ? 10 : 8), "chord-page");
        if (page == "keymap")
            Class(root, "chord-editor");
        g_object_ref_sink(root);
        gtk_widget_set_size_request(root, 650, -1);
        try {
            profile = active.profile;
            if (page == "keymap" && !profile.IsPreset()) {
                try {
                    profile = store.Draft(profile.id);
                    draft_revision = store.Revision(profile.id);
                } catch (...) {
                    profile = active.profile;
                }
            }
            baseline = profile.Value();
            if (rendering && fixture_status.value("ok", false)) {
                status = fixture_status;
                connected = true;
            }
            Build();
            if (!rendering) {
                Query();
                poll = g_timeout_add(
                    500,
                    +[](gpointer data) -> gboolean {
                        auto* self = static_cast<Impl*>(data);
                        self->Query();
                        return G_SOURCE_CONTINUE;
                    },
                    this);
            }
        } catch (const std::exception& error) {
            Add(root, ChordLabel(error.what(), "caption"));
        }
    }
    ~Impl() {
        alive->store(false);
        StopCapture();
        if (poll)
            g_source_remove(poll);
        if (advance)
            g_source_remove(advance);
        g_cancellable_cancel(cancel);
        g_object_unref(cancel);
        gtk_widget_destroy(root);
        g_object_unref(root);
    }
    template <class F> bool Try(F action) {
        try {
            action();
            return true;
        } catch (const std::exception& error) {
            Message(error.what());
            return false;
        }
    }
    void Message(std::string value) {
        message = std::move(value);
        Text(feedback, message);
        if (feedback && page != "keymap")
            gtk_widget_set_visible(feedback, !message.empty());
    }
    GtkWidget* Card() {
        auto* card = Class(Column(page == "keymap" ? 8 : 6), "chord-card");
        Add(root, card);
        return card;
    }
    void Query() {
        if (request_pending)
            return;
        request_pending = true;
        const auto live = alive;
        ChordCall({{"op", "status"}}, cancel, [this, live](Json value) {
            if (!live->load())
                return;
            request_pending = false;
            Receive(value, false);
        });
    }
    void Command(Json command) {
        if (applying || request_pending) {
            Message("正在读取并击状态，请稍后重试。");
            return;
        }
        request_pending = true;
        applying = true;
        StopCapture();
        RefreshStatus();
        const auto live = alive;
        ChordCall(command, cancel, [this, live](Json value) {
            if (!live->load())
                return;
            request_pending = false;
            Receive(value, true);
        });
    }
    void Receive(const Json& value, bool commanded) {
        if (!value.value("ok", false)) {
            connected = false;
            applying = false;
            Message(value.value("message", std::string("并击状态不可用")));
            RefreshStatus();
            return;
        }
        const bool was_applying = applying;
        status = value;
        connected = true;
        applying = value.value("busy", false);
        const auto before = active.profile.Value();
        Try([&] { active = store.Active(); });
        if (page != "keymap" && before != active.profile.Value()) {
            profile = active.profile;
            Rebuild();
        }
        if (commanded || was_applying)
            Message(applying ? "正在部署并验证键位方案…" : value.value("message", std::string{}));
        RefreshStatus();
    }
    void RefreshStatus() {
        if (!rebuild_pending)
            gtk_widget_set_sensitive(root, !applying);
        const bool enabled = connected && status.value("enabled", false);
        const bool available = enabled && status.value("available", false);
        const bool current =
            available && status.value("selectedSchema", std::string{}) == active.profile.SchemaID();
        Text(availability, applying     ? "正在应用"
                           : !connected ? "输入法未连接"
                           : current    ? "正在使用"
                           : available  ? "可用"
                           : enabled    ? "需要重新应用键位"
                                        : "已停用");
        Text(detail, current ? "普通输入使用已应用的并击规则和键位方案。"
                     : available ? "扩展已启用。需要在普通输入中使用时，可设为当前输入方案。"
                     : enabled ? status.value("recovery", std::string("请在键位方案页重新应用。"))
                               : "请先在“插件 · 内置扩展”中启用并击。");
        Text(implementation, "当前键位方案：" + active.profile.name);
        if (make_current) {
            gtk_button_set_label(GTK_BUTTON(make_current),
                                 current ? "当前输入方案" : "设为当前输入方案");
            gtk_widget_set_sensitive(make_current, available && !current && !applying);
        }
        if (duration)
            gtk_widget_set_sensitive(duration, enabled && !applying);
        Text(profile_status,
             profile.IsPreset() ? "内置模板 · 只读，复制后即可编辑"
             : active.profile.id == profile.id
                 ? (profile.Value() == active.profile.Value() ? "当前已应用键位"
                                                              : "草稿与已应用版本不同")
                 : "自定义方案 · " + std::to_string(profile.mappings.size()) + " 条映射");
    }
    void Build() {
        StopCapture();
        Clear(root);
        keyboard.clear();
        rows = nullptr;
        feedback = profile_picker = profile_status = name = left = right = boundary = encoding =
            search = table = keys = output = kind = mode = preview = nullptr;
        duration = availability = detail = make_current = implementation = save_entry = nullptr;
        capture = capture_label = capture_toggle = nullptr;
        updating = true;
        if (page == "settings")
            Configuration();
        else if (page == "keymap")
            Editor();
        else
            Try([this] { Learning(); });
        updating = false;
        if (!feedback) {
            feedback = ChordLabel(message, "caption");
            gtk_widget_set_no_show_all(feedback, true);
            gtk_widget_set_visible(feedback, !message.empty());
            Add(root, feedback);
        }
        std::function<void(GtkWidget*)> compact_buttons = [&](GtkWidget* widget) {
            if (GTK_IS_BUTTON(widget)) {
                auto* child = gtk_bin_get_child(GTK_BIN(widget));
                if (GTK_IS_LABEL(child)) {
                    auto* attributes = pango_attr_list_new();
                    const bool key = gtk_style_context_has_class(
                        gtk_widget_get_style_context(widget), "chord-key");
                    pango_attr_list_insert(attributes, pango_attr_line_height_new_absolute(
                                                           (key ? 16 : 14) * PANGO_SCALE));
                    gtk_label_set_attributes(GTK_LABEL(child), attributes);
                    pango_attr_list_unref(attributes);
                }
            }
            if (GTK_IS_CONTAINER(widget)) {
                auto* children = gtk_container_get_children(GTK_CONTAINER(widget));
                for (auto* child = children; child; child = child->next)
                    compact_buttons(GTK_WIDGET(child->data));
                g_list_free(children);
            }
        };
        compact_buttons(root);
        RefreshStatus();
        gtk_widget_show_all(root);
    }
    void Rebuild() {
        if (rebuild_pending)
            return;
        rebuild_pending = true;
        gtk_widget_set_sensitive(root, false);
        struct Pending {
            Impl* self;
            std::shared_ptr<std::atomic<bool>> alive;
        };
        g_idle_add_full(
            G_PRIORITY_DEFAULT_IDLE,
            +[](gpointer data) -> gboolean {
                auto* pending = static_cast<Pending*>(data);
                if (pending->alive->load()) {
                    pending->self->rebuild_pending = false;
                    pending->self->Try([&] { pending->self->Build(); });
                    gtk_widget_set_sensitive(pending->self->root, !pending->self->applying);
                }
                return G_SOURCE_REMOVE;
            },
            new Pending{this, alive}, +[](gpointer data) { delete static_cast<Pending*>(data); });
    }
    void Configuration() {
        auto* card = Card();
        auto* row = Row(8);
        auto* labels = Column(4);
        availability = ChordLabel("", "name");
        detail = ChordLabel("", "caption");
        gtk_label_set_line_wrap(GTK_LABEL(detail), true);
        gtk_label_set_max_width_chars(GTK_LABEL(detail), 56);
        Add(labels, availability);
        Add(labels, detail);
        Add(row, labels, true);
        make_current = Button("设为当前输入方案", [this] { Command({{"op", "makeCurrent"}}); });
        Add(row, make_current);
        Add(card, row);
        implementation = ChordLabel("", "name");
        Add(Card(), implementation);
        card = Card();
        Add(card, ChordLabel("组键间隔", "name"));
        row = Row(8);
        runtime::ConfigurationStore config(runtime::UserRoot() / "configuration");
        const auto saved = config.Load();
        const auto fields = runtime::ConfigurationStore::NormalizePlugin(
            kPluginID, saved.at("plugins").value(kPluginID, Json::object()));
        duration = Named(gtk_spin_button_new_with_range(0.02, 0.5, 0.01), "并击组键间隔，秒");
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(duration), 2);
        gtk_widget_set_size_request(duration, 84, -1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(duration),
                                  fields.at("durationMilliseconds").get<double>() / 1000);
        g_signal_connect(
            duration, "value-changed", G_CALLBACK(+[](GtkSpinButton* control, gpointer data) {
                auto* self = static_cast<Impl*>(data);
                if (self->updating)
                    return;
                self->Try([&] {
                    runtime::ConfigurationStore config(runtime::UserRoot() / "configuration");
                    auto saved = config.Load();
                    saved["plugins"][kPluginID]["durationMilliseconds"] =
                        static_cast<int>(std::lround(gtk_spin_button_get_value(control) * 1000));
                    config.Save(saved, saved.at("revision"));
                    if (self->configuration_changed)
                        self->configuration_changed();
                    self->Message("已保存组键间隔");
                });
            }),
            this);
        Add(row, duration);
        Add(row, ChordLabel("秒", "caption"));
        Add(row, Button("恢复默认", [this] {
                if (gtk_widget_get_sensitive(duration))
                    gtk_spin_button_set_value(GTK_SPIN_BUTTON(duration), 0.1);
            }));
        Add(card, row);
        gtk_widget_set_tooltip_text(card,
                                    "此间隔决定哪些按键属于同一批；停顿本身不会取消左右配对。");
    }
    // Editor and learning pages are populated below; all capture stays local
    // to a focused widget and never commits into another application.
    void Editor();
    void Learning();
    void CaptureFields();
    bool Save();
    bool CanLeave();
    bool EntryPending() const;
    bool SaveEntry();
    void ClearEntry();
    void RefreshMappings();
    void RefreshKeyboard();
    void LoadProfile(int index);
    void New(bool copy);
    void Import();
    void Export();
    void ShowPreview(const std::string& value);
    void MakeCapture(GtkWidget* parent, std::function<void(std::string)> callback);
    void StopCapture();
    void ToggleCapture();
    bool CaptureKey(GdkEventKey* event);
    void Exercises();
    void RefreshExercise();
    void Submit(const std::string& value);
};
void ChordPane::Impl::CaptureFields() {
    if (!name || profile.IsPreset())
        return;
    profile.name = EntryText(name);
    profile.left = EntryText(left);
    profile.right = EntryText(right);
    profile.legacy = gtk_combo_box_get_active(GTK_COMBO_BOX(boundary)) == 0;
    profile.ziranma = gtk_combo_box_get_active(GTK_COMBO_BOX(encoding)) == 1;
}
bool ChordPane::Impl::EntryPending() const {
    if (!keys || profile.IsPreset())
        return false;
    return (EntryText(keys) + '\0' + EntryText(output) + '\0' +
            std::to_string(gtk_combo_box_get_active(GTK_COMBO_BOX(kind)))) != entry_baseline;
}
bool ChordPane::Impl::Save() {
    if (applying) {
        Message("方案正在应用，请稍候。");
        return false;
    }
    if (EntryPending() && !SaveEntry())
        return false;
    return Try([&] {
        CaptureFields();
        if (profile.IsPreset())
            return;
        profile = Profile::Parse(profile.Value(), false);
        store.Save(profile, draft_revision);
        draft_revision = store.Revision(profile.id);
        baseline = profile.Value();
        Message("草稿已保存。点击“应用键位方案”才会生效。");
        RefreshStatus();
    });
}
bool ChordPane::Impl::CanLeave() {
    if (rebuild_pending)
        return false;
    if (applying) {
        Message("方案正在应用，请稍候。");
        return false;
    }
    CaptureFields();
    if (page != "keymap" || (profile.Value() == baseline && !EntryPending()))
        return true;
    auto* dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
                                          GTK_BUTTONS_NONE, "%s", "保存键位方案的更改？");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s",
                                             "保存为本地草稿不会应用到输入法。");
    gtk_dialog_add_buttons(GTK_DIALOG(dialog), "保存草稿", 1, "放弃更改", 2, "继续编辑",
                           GTK_RESPONSE_CANCEL, nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    const auto answer = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (answer == 2) {
        const bool saved = baseline.is_object();
        profile = baseline.is_object() ? Profile::Parse(baseline) : active.profile;
        baseline = profile.Value();
        if (!saved) {
            draft_revision.clear();
            if (!profile.IsPreset()) {
                try {
                    draft_revision = store.Revision(profile.id);
                } catch (...) {
                }
            }
        }
        selected_entry = -1;
        Rebuild();
        return true;
    }
    return answer == 1 && Save();
}
void ChordPane::Impl::LoadProfile(int index) {
    if (updating || index < 0 || static_cast<std::size_t>(index) >= profiles.size())
        return;
    const auto selected = profiles[index];
    if (!CanLeave()) {
        updating = true;
        for (std::size_t i = 0; i < profiles.size(); ++i)
            if (profiles[i].id == profile.id)
                gtk_combo_box_set_active(GTK_COMBO_BOX(profile_picker), i);
        updating = false;
        return;
    }
    Try([&] {
        profile = selected;
        draft_revision = profile.IsPreset() ? "" : store.Revision(profile.id);
        baseline = profile.Value();
        message.clear();
        selected_entry = -1;
        Rebuild();
    });
}
void ChordPane::Impl::New(bool copy) {
    if (!CanLeave())
        return;
    Try([&] {
        if (copy && !profile.native_schema.empty())
            throw std::runtime_error("原生并击方案由方案自身定义键位，不能复制为飞耀键位");
        auto next = copy ? profile : Profile::Parse(Contract().at("compilers").at(1).at("profile"));
        next.id = NewProfileID();
        next.name = copy ? profile.name + " 副本" : "新键位方案";
        if (!copy) {
            next.left = "qwertasdfgzxcvb";
            next.right = "yuiophjklnm,.";
            next.mappings.clear();
            next.legacy = false;
            next.ziranma = false;
        }
        profile = next;
        baseline = Json();
        draft_revision.clear();
        selected_entry = -1;
        message = "新方案尚未保存。可编辑键位后保存草稿，再应用键位方案。";
        Rebuild();
    });
}
void ChordPane::Impl::Import() {
    if (!CanLeave())
        return;
    auto* dialog =
        gtk_file_chooser_dialog_new("导入键位方案", parent, GTK_FILE_CHOOSER_ACTION_OPEN, "取消",
                                    GTK_RESPONSE_CANCEL, "导入", GTK_RESPONSE_ACCEPT, nullptr);
    auto* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "JSON 键位方案");
    gtk_file_filter_add_pattern(filter, "*.json");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    std::string path;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        auto* value = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (value) {
            path = value;
            g_free(value);
        }
    }
    gtk_widget_destroy(dialog);
    if (!path.empty())
        Try([&] {
            profile = store.Import(ReadImport(path));
            draft_revision.clear();
            baseline = Json();
            selected_entry = -1;
            message = "已导入为独立的新方案。确认后保存草稿，再应用键位方案。";
            Rebuild();
        });
}
void ChordPane::Impl::Export() {
    Try([&] {
        CaptureFields();
        if (EntryPending() && !SaveEntry())
            return;
        const auto bytes = store.Export(profile);
        auto* dialog = gtk_file_chooser_dialog_new(
            "导出键位方案", parent, GTK_FILE_CHOOSER_ACTION_SAVE, "取消", GTK_RESPONSE_CANCEL,
            "导出", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), "chord-keymap.json");
        gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), true);
        std::string path;
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            auto* value = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
            if (value) {
                path = value;
                g_free(value);
            }
        }
        gtk_widget_destroy(dialog);
        if (path.empty())
            return;
        GError* error = nullptr;
        // GLib replaces the destination atomically rather than following a
        // final symlink or partially overwriting a previously exported file.
        const bool ok = g_file_set_contents_full(
            path.c_str(), bytes.data(), bytes.size(),
            static_cast<GFileSetContentsFlags>(G_FILE_SET_CONTENTS_CONSISTENT |
                                               G_FILE_SET_CONTENTS_DURABLE),
            0600, &error);
        g_clear_error(&error);
        if (!ok)
            throw std::runtime_error("导出未完成，原文件已保留");
        Message("键位方案已导出");
    });
}
void ChordPane::Impl::ClearEntry() {
    selected_entry = -1;
    if (save_entry)
        MappingButtonTitle(save_entry, "添加映射");
    gtk_entry_set_text(GTK_ENTRY(keys), "");
    gtk_entry_set_text(GTK_ENTRY(output), "");
    gtk_combo_box_set_active(GTK_COMBO_BOX(kind), 0);
    entry_baseline = std::string(1, '\0') + '\0' + "0";
    RefreshKeyboard();
}
bool ChordPane::Impl::SaveEntry() {
    return Try([&] {
        if (profile.IsPreset() || applying)
            throw std::runtime_error("请先复制内置方案，再编辑映射");
        CaptureFields();
        auto next = profile;
        Entry entry{EntryText(keys), EntryText(output),
                    gtk_combo_box_get_active(GTK_COMBO_BOX(kind)) == 0};
        if (entry.keys.empty() && entry.output.empty() && selected_entry < 0) {
            ClearEntry();
            return;
        }
        if (selected_entry >= 0 && static_cast<std::size_t>(selected_entry) < next.mappings.size())
            next.mappings[selected_entry] = entry;
        else
            next.mappings.push_back(entry);
        next = Profile::Parse(next.Value(), false);
        profile = next;
        ClearEntry();
        RefreshMappings();
        Message("映射已加入草稿。点击“保存草稿”保留修改。");
    });
}
void ChordPane::Impl::RefreshMappings() {
    if (!rows)
        return;
    updating = true;
    gtk_list_store_clear(rows);
    auto query = Upper(EntryText(search));
    const auto start = query.find_first_not_of(" \t\r\n");
    query = start == std::string::npos
                ? ""
                : query.substr(start, query.find_last_not_of(" \t\r\n") - start + 1);
    for (std::size_t index = 0; index < profile.mappings.size(); ++index) {
        const auto& entry = profile.mappings[index];
        if (!query.empty() && Upper(entry.keys).find(query) == std::string::npos &&
            Upper(entry.output).find(query) == std::string::npos)
            continue;
        GtkTreeIter row;
        gtk_list_store_append(rows, &row);
        std::string display_keys;
        for (char key : Upper(entry.keys)) {
            if (!display_keys.empty())
                display_keys += " + ";
            display_keys += key;
        }
        gtk_list_store_set(rows, &row, 0, display_keys.c_str(), 1, entry.output.c_str(), 2,
                           entry.syllable ? "完整音节" : "拼音片段", 3, static_cast<int>(index),
                           -1);
    }
    updating = false;
}
void ChordPane::Impl::RefreshKeyboard() {
    if (!keys)
        return;
    const auto selected = EntryText(keys);
    for (const auto& [key, button] : keyboard) {
        const auto side = EntryText(left).find(key) != std::string::npos    ? " L"
                          : EntryText(right).find(key) != std::string::npos ? " R"
                                                                            : "";
        gtk_button_set_label(GTK_BUTTON(button), (Upper(std::string(1, key)) + side).c_str());
        auto* style = gtk_widget_get_style_context(button);
        if (selected.find(key) != std::string::npos)
            gtk_style_context_add_class(style, "selected");
        else
            gtk_style_context_remove_class(style, "selected");
    }
}
void ChordPane::Impl::ShowPreview(const std::string& value) {
    Try([&] {
        CaptureFields();
        const auto valid = Profile::Parse(profile.Value(), false);
        const auto* entry = valid.Find(value);
        const auto result = entry               ? valid.Encoded(*entry).value_or("无法编码")
                            : value.size() == 1 ? value
                                                : "未找到精确映射";
        Text(preview, Upper(value) + " → " + result +
                          (entry ? (entry->syllable ? " · 完整音节" : " · 拼音片段") : ""));
    });
}
void ChordPane::Impl::Editor() {
    profiles = store.Profiles();
    if (std::none_of(profiles.begin(), profiles.end(),
                     [&](const auto& value) { return value.id == profile.id; }))
        profiles.push_back(profile);
    auto* card = Card();
    auto* row = Row(8);
    std::vector<std::string> names;
    int selected = 0;
    for (std::size_t i = 0; i < profiles.size(); ++i) {
        names.push_back(profiles[i].name);
        if (profiles[i].id == profile.id)
            selected = i;
    }
    profile_picker = Choice("键位方案", names, selected);
    gtk_widget_set_size_request(profile_picker, 200, -1);
    g_signal_connect(profile_picker, "changed", G_CALLBACK(+[](GtkComboBox* box, gpointer data) {
                         static_cast<Impl*>(data)->LoadProfile(gtk_combo_box_get_active(box));
                     }),
                     this);
    Class(profile_picker, "chord-profiles");
    Add(row, profile_picker);
    Add(row, Button("复制方案", [this] { New(true); }));
    Add(row, Button("新建", [this] { New(false); }));
    Add(row, Button("导入…", [this] { Import(); }));
    Add(row, Button("导出…", [this] { Export(); }));
    Add(card, row);
    profile_status = ChordLabel("", "caption");
    Add(card, profile_status);
    row = Row(8);
    Add(row, ChordLabel("名称"));
    name = EntryField("键位名称", profile.name, 200);
    Add(row, name, true);
    auto* remove = Button("删除方案…", [this] {
        if (applying || !CanLeave() ||
            !Confirm(parent, "删除键位方案？", "已应用方案不能删除。学习进度会保留。"))
            return;
        Try([&] {
            store.Remove(profile.id, draft_revision);
            profile = store.Active().profile;
            baseline = profile.Value();
            draft_revision = profile.IsPreset() ? "" : store.Revision(profile.id);
            message = "键位草稿已删除";
            Rebuild();
        });
    });
    gtk_widget_set_sensitive(name, !profile.IsPreset());
    gtk_widget_set_sensitive(remove, !profile.IsPreset() && !draft_revision.empty() &&
                                         active.profile.id != profile.id);
    Add(row, remove);
    Add(card, row);
    boundary = Choice("音节边界", {"每个多键批次（兼容飞耀）", "按映射类型（完整音节 / 片段）"},
                      profile.legacy ? 0 : 1);
    encoding = Choice("输出编码", {"全拼", "自然码双拼"}, profile.ziranma ? 1 : 0);
    row = Row(8);
    Add(row, ChordLabel("音节边界"));
    Add(row, boundary);
    Add(card, row);
    row = Row(8);
    Add(row, ChordLabel("输出编码"));
    Add(row, encoding);
    Add(card, row);
    gtk_widget_set_sensitive(boundary, !profile.IsPreset());
    gtk_widget_set_sensitive(encoding, !profile.IsPreset());
    card = Card();
    Add(card, ChordLabel("键区与和弦", "name"));
    row = Row(8);
    left = EntryField("左区字母键", profile.left, 200);
    right = EntryField("右区字母键", profile.right, 200);
    for (auto* field : {left, right}) {
        gtk_widget_set_sensitive(field, !profile.IsPreset());
        g_signal_connect(field, "changed", G_CALLBACK(+[](GtkEntry*, gpointer data) {
                             static_cast<Impl*>(data)->RefreshKeyboard();
                         }),
                         this);
    }
    Add(row, ChordLabel("左区"));
    Add(row, left, true);
    Add(row, ChordLabel("右区"));
    Add(row, right, true);
    Add(card, row);
    mode = Class(Row(0), "chord-mode");
    gtk_widget_set_halign(mode, GTK_ALIGN_START);
    gtk_widget_set_size_request(mode, -1, 18);
    const std::vector<std::string> modes = {"选择和弦", "设为左区", "设为右区", "移出键区"};
    for (int index = 0; index < 4; ++index) {
        auto* button = Button(
            modes[index],
            [this, index] {
                keyboard_mode = index;
                auto* children = gtk_container_get_children(GTK_CONTAINER(mode));
                int position = 0;
                for (auto* child = children; child; child = child->next, ++position) {
                    auto* style = gtk_widget_get_style_context(GTK_WIDGET(child->data));
                    if (position == index)
                        gtk_style_context_add_class(style, "selected");
                    else
                        gtk_style_context_remove_class(style, "selected");
                }
                g_list_free(children);
            },
            "segment");
        if (index == keyboard_mode)
            Class(button, "selected");
        Add(mode, button);
    }
    Add(card, mode);
    auto* keyboard_rows = Column(4);
    int indent = 4;
    for (const auto& line : {"qwertyuiop", "asdfghjkl", "zxcvbnm,."}) {
        row = Row(4);
        gtk_widget_set_size_request(row, -1, 28);
        gtk_widget_set_margin_start(row, indent);
        indent += 12;
        for (char key : std::string(line)) {
            auto* button = Button(
                Upper(std::string(1, key)),
                [this, key] {
                    if (applying)
                        return;
                    const int mode_index = keyboard_mode;
                    if (mode_index != 0 && profile.IsPreset()) {
                        Message("内置模板只读，请先复制方案再调整键区。");
                        return;
                    }
                    auto change = [&](GtkWidget* field, bool add) {
                        auto value = EntryText(field);
                        value.erase(std::remove(value.begin(), value.end(), key), value.end());
                        if (add)
                            value += key;
                        gtk_entry_set_text(GTK_ENTRY(field), value.c_str());
                    };
                    if (mode_index == 0)
                        change(keys, EntryText(keys).find(key) == std::string::npos);
                    else {
                        change(left, mode_index == 1);
                        change(right, mode_index == 2);
                    }
                    RefreshKeyboard();
                },
                "chord-key");
            gtk_widget_set_size_request(button, 43, 20);
            gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
            Add(row, button);
            keyboard[key] = button;
        }
        Add(keyboard_rows, row);
    }
    Add(card, keyboard_rows);
    card = Card();
    row = Row(8);
    Add(row, ChordLabel("映射表", "name"), true);
    search = EntryField("搜索键位或输出拼音", "", 330);
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(search), GTK_ENTRY_ICON_PRIMARY,
                                      "edit-find-symbolic");
    gtk_entry_set_placeholder_text(GTK_ENTRY(search), "搜索键位或输出拼音");
    Add(row, search);
    Add(card, row);
    g_signal_connect(search, "changed", G_CALLBACK(+[](GtkEntry*, gpointer data) {
                         static_cast<Impl*>(data)->RefreshMappings();
                     }),
                     this);
    rows = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
    table = Named(gtk_tree_view_new_with_model(GTK_TREE_MODEL(rows)), "键位映射表");
    g_object_unref(rows);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(table), false);
    g_object_set(table, "rules-hint", TRUE, nullptr);
    row = Row(3);
    const int widths[] = {160, 255, 140};
    const char* titles[] = {"和弦键", "输出拼音", "类型"};
    for (int i = 0; i < 3; ++i) {
        auto* label = ChordLabel(titles[i], "name");
        gtk_widget_set_size_request(label, widths[i], -1);
        Add(row, label);
        auto* renderer = gtk_cell_renderer_text_new();
        g_object_set(renderer, "height", 25, "ypad", 0, nullptr);
        auto* font = pango_font_description_from_string("monospace");
        pango_font_description_set_absolute_size(font, 11 * PANGO_SCALE);
        g_object_set(renderer, "font-desc", font, nullptr);
        pango_font_description_free(font);
        auto* column =
            gtk_tree_view_column_new_with_attributes(titles[i], renderer, "text", i, nullptr);
        gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
        gtk_tree_view_column_set_fixed_width(column, widths[i]);
        gtk_tree_view_column_set_cell_data_func(
            column, renderer,
            +[](GtkTreeViewColumn*, GtkCellRenderer* cell, GtkTreeModel* model, GtkTreeIter* iter,
                gpointer data) {
                auto* table = GTK_WIDGET(data);
                auto* path = gtk_tree_model_get_path(model, iter);
                const auto index = gtk_tree_path_get_indices(path)[0];
                GdkRGBA background{};
                gtk_style_context_lookup_color(
                    gtk_widget_get_style_context(table),
                    index % 2 ? "xsime_chord_row_odd" : "xsime_chord_row_even", &background);
                g_object_set(cell, "cell-background-rgba", &background, nullptr);
                gtk_tree_path_free(path);
            },
            table, nullptr);
        gtk_tree_view_append_column(GTK_TREE_VIEW(table), column);
    }
    gtk_widget_set_margin_start(row, 5);
    gtk_widget_set_margin_end(row, 5);
    gtk_widget_set_margin_top(row, 2);
    gtk_widget_set_margin_bottom(row, 2);
    Add(card, row);
    auto* scroll = Class(gtk_scrolled_window_new(nullptr, nullptr), "chord-mappings");
    gtk_widget_set_size_request(scroll, -1, 195);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), table);
    Add(card, scroll);
    row = Row(8);
    keys = EntryField("和弦键输入", "", 130);
    output = EntryField("输出拼音输入", "", 160);
    kind = Choice("映射类型", {"完整音节", "拼音片段"}, 0);
    Add(row, ChordLabel("和弦"));
    Add(row, keys);
    Add(row, ChordLabel("输出"));
    Add(row, output);
    Add(row, kind);
    Add(card, row);
    for (auto* field : {keys, output, kind})
        gtk_widget_set_sensitive(field, !profile.IsPreset());
    g_signal_connect(keys, "changed", G_CALLBACK(+[](GtkEntry*, gpointer data) {
                         static_cast<Impl*>(data)->RefreshKeyboard();
                     }),
                     this);
    auto* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(table));
    g_signal_connect(
        selection, "changed", G_CALLBACK(+[](GtkTreeSelection* selection, gpointer data) {
            auto* self = static_cast<Impl*>(data);
            if (self->updating)
                return;
            GtkTreeIter iter;
            GtkTreeModel* model = nullptr;
            if (!gtk_tree_selection_get_selected(selection, &model, &iter))
                return;
            int index = -1;
            gtk_tree_model_get(model, &iter, 3, &index, -1);
            if (index < 0 || static_cast<std::size_t>(index) >= self->profile.mappings.size())
                return;
            if (self->EntryPending() &&
                !Confirm(self->parent, "放弃当前映射编辑？", "尚未加入草稿的映射修改会丢弃。")) {
                self->updating = true;
                gtk_tree_selection_unselect_all(selection);
                GtkTreeIter previous;
                for (bool valid = gtk_tree_model_get_iter_first(model, &previous); valid;
                     valid = gtk_tree_model_iter_next(model, &previous)) {
                    int candidate = -1;
                    gtk_tree_model_get(model, &previous, 3, &candidate, -1);
                    if (candidate == self->selected_entry) {
                        gtk_tree_selection_select_iter(selection, &previous);
                        break;
                    }
                }
                self->updating = false;
                return;
            }
            self->selected_entry = index;
            MappingButtonTitle(self->save_entry, "更新映射");
            const auto& entry = self->profile.mappings[index];
            gtk_entry_set_text(GTK_ENTRY(self->keys), entry.keys.c_str());
            gtk_entry_set_text(GTK_ENTRY(self->output), entry.output.c_str());
            gtk_combo_box_set_active(GTK_COMBO_BOX(self->kind), entry.syllable ? 0 : 1);
            self->entry_baseline =
                entry.keys + '\0' + entry.output + '\0' + (entry.syllable ? "0" : "1");
            self->RefreshKeyboard();
        }),
        this);
    row = Row(8);
    save_entry = Named(Button("添加映射", [this] { SaveEntry(); }), "添加或更新映射");
    Add(row, save_entry);
    Add(row, Button("清空编辑", [this] {
            if (!EntryPending() ||
                Confirm(parent, "放弃当前映射编辑？", "尚未加入草稿的映射修改会丢弃。"))
                ClearEntry();
        }));
    Add(row, Button("删除映射", [this] {
            if (applying || profile.IsPreset() || selected_entry < 0)
                return;
            profile.mappings.erase(profile.mappings.begin() + selected_entry);
            ClearEntry();
            RefreshMappings();
            Message("映射已从草稿移除，尚未保存。");
        }));
    Add(row, Column(0), true);
    Add(row, Button("试译和弦", [this] { ShowPreview(EntryText(keys)); }));
    Add(card, row);
    card = Card();
    row = Row(8);
    Add(row, ChordLabel("本地试打", "name"), true);
    capture_toggle = Button("开始试打", [this] { ToggleCapture(); });
    Add(row, capture_toggle);
    Add(row, Button("停止", [this] { StopCapture(); }));
    Add(card, row);
    MakeCapture(card, [this](auto value) { ShowPreview(value); });
    preview = ChordLabel("", "caption");
    Class(preview, "chord-preview");
    Add(card, preview);
    card = Card();
    row = Row(8);
    Add(row, Button("保存草稿", [this] { Save(); }));
    Add(row, Button("应用键位方案", [this] {
            if (!Save())
                return;
            Try([&] {
                const auto frozen = Profile::Parse(profile.Value());
                Command(
                    {{"op", "apply"}, {"revision", active.revision}, {"profile", frozen.Value()}});
            });
        }));
    Add(card, row);
    feedback = ChordLabel(message, "caption");
    gtk_label_set_line_wrap(GTK_LABEL(feedback), true);
    Add(card, feedback);
    ClearEntry();
    RefreshMappings();
    RefreshKeyboard();
}
void ChordPane::Impl::MakeCapture(GtkWidget* parent_widget,
                                  std::function<void(std::string)> callback) {
    on_chord = std::move(callback);
    capture = Named(Class(gtk_event_box_new(), "chord-capture"),
                    page == "practice" ? "并击练习区域" : "并击本地试打区域");
    gtk_widget_set_can_focus(capture, true);
    gtk_widget_set_size_request(capture, -1, page == "keymap" ? 48 : 76);
    gtk_widget_add_events(capture, GDK_BUTTON_PRESS_MASK | GDK_KEY_PRESS_MASK |
                                       GDK_KEY_RELEASE_MASK | GDK_FOCUS_CHANGE_MASK);
    capture_label = ChordLabel(page == "keymap" ? "点击“开始试打”后在这里接收按键"
                                                : "点击开始后，在此处按下并松开一组键",
                               "caption");
    gtk_label_set_xalign(GTK_LABEL(capture_label), 0.5);
    gtk_container_add(GTK_CONTAINER(capture), capture_label);
    Add(parent_widget, capture);
    g_signal_connect(capture, "button-press-event",
                     G_CALLBACK(+[](GtkWidget* widget, GdkEventButton*, gpointer data) -> gboolean {
                         auto* self = static_cast<Impl*>(data);
                         if (self->capturing)
                             gtk_widget_grab_focus(widget);
                         return TRUE;
                     }),
                     this);
    g_signal_connect(capture, "key-press-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventKey* event, gpointer data) -> gboolean {
                         return static_cast<Impl*>(data)->CaptureKey(event);
                     }),
                     this);
    g_signal_connect(capture, "key-release-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventKey* event, gpointer data) -> gboolean {
                         return static_cast<Impl*>(data)->CaptureKey(event);
                     }),
                     this);
    g_signal_connect(capture, "focus-out-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventFocus*, gpointer data) -> gboolean {
                         static_cast<Impl*>(data)->StopCapture();
                         return FALSE;
                     }),
                     this);
}
void ChordPane::Impl::StopCapture() {
    const bool was_capturing = capturing;
    capturing = false;
    down.clear();
    captured.clear();
    if (!was_capturing)
        return;
    if (capture_toggle)
        gtk_button_set_label(GTK_BUTTON(capture_toggle),
                             page == "practice" ? "开始练习" : "开始试打");
    Text(capture_label, "点击开始后，在此处按下并松开一组键");
}
void ChordPane::Impl::ToggleCapture() {
    if (capturing) {
        StopCapture();
        return;
    }
    if (applying || !capture || (page == "practice" && exercise_index >= exercises.size()))
        return;
    capturing = true;
    down.clear();
    captured.clear();
    gtk_widget_grab_focus(capture);
    gtk_button_set_label(GTK_BUTTON(capture_toggle), page == "practice" ? "停止练习" : "停止试打");
    Text(capture_label, "等待并击 · Esc 退出");
}
bool ChordPane::Impl::CaptureKey(GdkEventKey* event) {
    if (!capturing || applying)
        return false;
    if (event->keyval == GDK_KEY_Escape) {
        StopCapture();
        return true;
    }
    if (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK | GDK_META_MASK)) {
        down.clear();
        captured.clear();
        return false;
    }
    const auto code = event->hardware_keycode ? event->hardware_keycode : event->keyval;
    if (event->type == GDK_KEY_RELEASE) {
        if (!down.erase(code))
            return true;
        if (down.empty() && !captured.empty()) {
            std::string value(captured.begin(), captured.end());
            captured.clear();
            Text(capture_label, Upper(value));
            on_chord(value);
        }
        return true;
    }
    const auto scalar = gdk_keyval_to_unicode(gdk_keyval_to_lower(event->keyval));
    CaptureFields();
    if (scalar > 127 || !profile.KeyHalf(scalar))
        return true;
    if (down.emplace(code, static_cast<char>(scalar)).second)
        captured.insert(static_cast<char>(scalar));
    Text(capture_label, "按住：" + Upper(std::string(captured.begin(), captured.end())));
    return true;
}
void ChordPane::Impl::Learning() {
    if (advance) {
        g_source_remove(advance);
        advance = 0;
    }
    advancing = false;
    courses = Curriculum(active.profile);
    progress = std::make_unique<ProgressStore>(runtime::UserRoot(), active.profile.SchemaID());
    const auto snapshot = progress->Load();
    const auto& items = snapshot.at("items");
    if (page == "practice") {
        auto* row = Row(8);
        gtk_widget_set_size_request(row, -1, 20);
        std::vector<std::string> titles;
        int preferred = 0;
        for (std::size_t i = 0; i < courses.size(); ++i) {
            titles.push_back(courses[i].title + "（" + std::to_string(courses[i].mappings.size()) +
                             "）");
            if (courses[i].key_count == 2)
                preferred = i;
        }
        course_picker = Choice("并击课程", titles, preferred);
        Add(row, course_picker);
        g_signal_connect(course_picker, "changed", G_CALLBACK(+[](GtkComboBox*, gpointer data) {
                             auto* self = static_cast<Impl*>(data);
                             self->StopCapture();
                             self->Exercises();
                         }),
                         this);
        capture_toggle = Button("开始练习", [this] { ToggleCapture(); });
        Add(row, capture_toggle);
        Add(row, Button("换一题", [this] {
                if (exercises.empty())
                    return;
                if (advance) {
                    g_source_remove(advance);
                    advance = 0;
                }
                advancing = false;
                exercise_index = (exercise_index + 1) % exercises.size();
                streak = 0;
                RefreshExercise();
            }));
        score = ChordLabel("", "caption");
        gtk_label_set_xalign(GTK_LABEL(score), 1);
        Add(row, score, true);
        Add(root, row);
        auto* card = Card();
        target = ChordLabel("", "chord-target");
        gtk_label_set_xalign(GTK_LABEL(target), 0.5);
        Add(card, target);
        hint = ChordLabel("", "chord-hint");
        Add(card, hint);
        MakeCapture(card, [this](auto value) { Submit(value); });
        practice_feedback = ChordLabel("", "chord-feedback");
        Add(card, practice_feedback);
        Exercises();
        return;
    }
    if (page == "progress") {
        std::size_t total = 0, attempted = 0, mastered = 0;
        for (const auto& course : courses) {
            const auto stats = CourseProgress(course, items);
            total += stats.at("total").get<std::size_t>();
            attempted += stats.at("attempted").get<std::size_t>();
            mastered += stats.at("mastered").get<std::size_t>();
        }
        Add(root,
            ChordLabel("全部 " + std::to_string(total) + " 项 · 已练 " + std::to_string(attempted) +
                           " · 已掌握 " + std::to_string(mastered),
                       "caption"));
    }
    for (const auto& course : courses) {
        const auto stats = CourseProgress(course, items);
        auto count = [&](const char* key) {
            return std::to_string(stats.at(key).get<std::uint64_t>());
        };
        auto* card = Card();
        auto* row = Row(8);
        Add(row, ChordLabel(course.title, "name"), true);
        if (page == "lessons")
            Add(row, ChordLabel(count("total") + " 项", "tiny"));
        Add(card, row);
        if (page == "lessons")
            Add(card, ChordLabel("已练 " + count("attempted") + "/" + count("total") +
                                     " · 已掌握 " + count("mastered") + " · 连续正确 3 次即掌握",
                                 "caption"));
        else {
            const auto attempts = stats.at("attempts").get<double>();
            const auto accuracy =
                attempts ? std::lround(stats.at("correct").get<double>() / attempts * 100) : 0;
            Add(card, ChordLabel("掌握 " + count("mastered") + "/" + count("total") + " · 尝试 " +
                                     std::to_string(static_cast<std::uint64_t>(attempts)) +
                                     " 次 · 正确率 " + std::to_string(accuracy) + "%",
                                 "caption"));
        }
    }
    if (page == "progress") {
        auto* clear = Button("清空学习进度…", [this] {
            if (!Confirm(parent, "清空并击学习进度？",
                         "课程与键位不会删除，但所有练习次数、正确率和掌握状态会被清空。"))
                return;
            Try([&] {
                progress->Clear(std::time(nullptr));
                Rebuild();
            });
        });
        gtk_widget_set_size_request(clear, -1, 20);
        Add(root, clear);
    }
}
void ChordPane::Impl::Exercises() {
    if (advance) {
        g_source_remove(advance);
        advance = 0;
    }
    advancing = false;
    const int index = gtk_combo_box_get_active(GTK_COMBO_BOX(course_picker));
    exercises.clear();
    exercise_index = streak = 0;
    Try([&] {
        if (index >= 0 && static_cast<std::size_t>(index) < courses.size())
            exercises = Sample(courses[index], 30, progress->Load().at("items"),
                               std::time(nullptr) / 86400);
        RefreshExercise();
    });
}
void ChordPane::Impl::RefreshExercise() {
    if (exercise_index >= exercises.size()) {
        Text(target, "本轮完成");
        Text(hint, "换一个课程，或重新选择当前课程再练一轮。");
        Text(practice_feedback, "");
        Text(score, "");
        StopCapture();
        return;
    }
    Text(target, exercises[exercise_index].output);
    Text(hint, "按下对应并击");
    Text(practice_feedback, "等待输入");
    Text(score, std::to_string(exercise_index + 1) + "/" + std::to_string(exercises.size()) +
                    " · 连对 " + std::to_string(streak));
}
void ChordPane::Impl::Submit(const std::string& value) {
    if (advancing || exercise_index >= exercises.size())
        return;
    Try([&] {
        // A changed applied snapshot retires the exercise before any scoring.
        if (store.Active().profile.Value() != active.profile.Value()) {
            StopCapture();
            throw std::runtime_error("已应用键位发生变化，请重新打开练习页");
        }
        const auto& exercise = exercises[exercise_index];
        const bool correct = Matches(value, exercise.keys);
        progress->Record(exercise.id, correct, std::time(nullptr));
        if (correct) {
            Text(practice_feedback, "正确 · " + Upper(exercise.keys));
            Text(hint, "键位 " + Upper(exercise.keys));
            ++streak;
            ++exercise_index;
            advancing = true;
            advance = g_timeout_add(
                450,
                +[](gpointer data) -> gboolean {
                    auto* self = static_cast<Impl*>(data);
                    self->advance = 0;
                    self->advancing = false;
                    self->RefreshExercise();
                    return G_SOURCE_REMOVE;
                },
                this);
        } else {
            streak = 0;
            Text(practice_feedback, "这次是 " + Upper(value) + "，再试一次");
            Text(hint, "提示：" + Upper(exercise.keys));
            Text(score, std::to_string(exercise_index + 1) + "/" +
                            std::to_string(exercises.size()) + " · 连对 0");
        }
    });
}
ChordPane::ChordPane(GtkWindow* parent, std::string page, bool rendering,
                     std::function<void()> changed, const Json& fixture_status)
    : impl_(std::make_unique<Impl>(parent, std::move(page), rendering, std::move(changed),
                                   fixture_status)) {}
ChordPane::~ChordPane() = default;
GtkWidget* ChordPane::Widget() const { return impl_->root; }
bool ChordPane::CanLeave() { return impl_->CanLeave(); }
} // namespace rimes::desktop
