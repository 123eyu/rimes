#include "practice_editor.hpp"
#include "typing_practice_core.hpp"
#include <cstring>

namespace rimes::desktop {
void ConfigurePracticeInput() {
    // Fcitx's optional GTK snooper consumes preedit keys before widget handlers.
    // Use GTK's regular widget/IM-context path in this process only.
    // https://github.com/fcitx/fcitx5-gtk/blob/5.1.1/gtk3/fcitximcontext.cpp
    g_setenv("FCITX_DISABLE_SNOOPER", "1", TRUE);
}
PracticeEditor::PracticeEditor()
    : view_(Class(gtk_text_view_new(), "practice-editor")),
      buffer_(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view_))) {
    g_object_ref_sink(view_);
    g_object_ref(buffer_);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view_), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view_), 19);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view_), 19);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view_), 12);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(view_), 12);
    gtk_text_view_set_input_purpose(GTK_TEXT_VIEW(view_), GTK_INPUT_PURPOSE_FREE_FORM);
    gtk_text_view_set_input_hints(
        GTK_TEXT_VIEW(view_),
        static_cast<GtkInputHints>(GTK_INPUT_HINT_NO_SPELLCHECK | GTK_INPUT_HINT_NO_EMOJI));
    atk_object_set_name(gtk_widget_get_accessible(view_), "跟打输入，首键计时，组字不判错");
    Enable(false);
    g_signal_connect(view_, "drag-motion",
                     G_CALLBACK(+[](GtkWidget *, GdkDragContext *drag, gint, gint, guint time,
                                    gpointer) -> gboolean {
                         gdk_drag_status(drag, static_cast<GdkDragAction>(0), time);
                         return TRUE;
                     }),
                     this);
    g_signal_connect(view_, "drag-drop",
                     G_CALLBACK(+[](GtkWidget *, GdkDragContext *, gint, gint, guint,
                                    gpointer data) -> gboolean {
                         static_cast<PracticeEditor *>(data)->Reject();
                         return TRUE;
                     }),
                     this);
    g_signal_connect(view_, "drag-data-received",
                     G_CALLBACK(+[](GtkWidget *widget, GdkDragContext *, gint, gint,
                                    GtkSelectionData *, guint, guint, gpointer data) {
                         g_signal_stop_emission_by_name(widget, "drag-data-received");
                         static_cast<PracticeEditor *>(data)->Reject();
                     }),
                     this);
    g_signal_connect(view_, "key-press-event",
                     G_CALLBACK(+[](GtkWidget *, GdkEventKey *e, gpointer data) -> gboolean {
                         auto *s = static_cast<PracticeEditor *>(data);
                         if (!s->enabled_)
                             return TRUE;
                         const auto modifiers = GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK |
                                                GDK_META_MASK | GDK_HYPER_MASK;
                         const bool shortcut = (e->state & modifiers) != 0;
                         if ((shortcut && (e->keyval == GDK_KEY_v || e->keyval == GDK_KEY_V)) ||
                             ((e->state & GDK_SHIFT_MASK) && e->keyval == GDK_KEY_Insert)) {
                             s->Reject();
                             return TRUE;
                         }
                         if (shortcut || e->is_modifier)
                             return FALSE;
                         const auto unicode = gdk_keyval_to_unicode(e->keyval);
                         const bool printable =
                             (unicode >= 32 && !g_unichar_iscntrl(unicode) &&
                              !(unicode >= 0xf700 && unicode <= 0xf8ff)) ||
                             (e->keyval >= GDK_KEY_dead_grave && e->keyval <= GDK_KEY_dead_greek);
                         const bool backspace = e->keyval == GDK_KEY_BackSpace;
                         if (!printable && !backspace && e->keyval != GDK_KEY_Return &&
                             e->keyval != GDK_KEY_KP_Enter && e->keyval != GDK_KEY_Tab &&
                             e->keyval != GDK_KEY_ISO_Left_Tab && e->keyval != GDK_KEY_Delete)
                             return FALSE;
                         const auto stamp = std::make_pair(e->time, e->hardware_keycode);
                         // Fcitx can forward an unhandled event. That is the original physical
                         // press, not a second key. Genuine auto-repeat has a new timestamp.
                         if (s->seen_.insert(stamp).second) {
                             if (s->seen_.size() > 256)
                                 s->seen_.erase(s->seen_.begin());
                             const bool repeat = !s->down_.insert(e->hardware_keycode).second;
                             if (s->key)
                                 s->key(e->time, e->hardware_keycode, repeat, backspace,
                                        s->composing_);
                         }
                         return FALSE;
                     }),
                     this);
    g_signal_connect(view_, "key-release-event",
                     G_CALLBACK(+[](GtkWidget *, GdkEventKey *e, gpointer data) -> gboolean {
                         static_cast<PracticeEditor *>(data)->down_.erase(e->hardware_keycode);
                         return FALSE;
                     }),
                     this);
    g_signal_connect(view_, "preedit-changed",
                     G_CALLBACK(+[](GtkTextView *, const gchar *text, gpointer data) {
                         auto *s = static_cast<PracticeEditor *>(data);
                         s->composing_ = text && *text;
                         if (s->enabled_ && s->composing_ && s->composition)
                             s->composition();
                         if (!s->composing_)
                             s->Publish();
                     }),
                     this);
    g_signal_connect(buffer_, "begin-user-action", G_CALLBACK(+[](GtkTextBuffer *, gpointer data) {
                         auto *s = static_cast<PracticeEditor *>(data);
                         if (!s->depth_) {
                             s->before_action_ = s->Text();
                             s->rejected_action_ = false;
                         }
                         ++s->depth_;
                     }),
                     this);
    g_signal_connect(buffer_, "end-user-action", G_CALLBACK(+[](GtkTextBuffer *, gpointer data) {
                         auto *s = static_cast<PracticeEditor *>(data);
                         if (s->depth_)
                             --s->depth_;
                         if (!s->depth_ && s->rejected_action_) {
                             s->suppressed_ = true;
                             gtk_text_buffer_set_text(s->buffer_, s->before_action_.c_str(),
                                                      s->before_action_.size());
                             s->suppressed_ = false;
                             s->dirty_ = false;
                             s->insertion_.reset();
                             s->rejected_action_ = false;
                         }
                         s->Publish();
                     }),
                     this);
    g_signal_connect(
        buffer_, "insert-text",
        G_CALLBACK(+[](GtkTextBuffer *b, GtkTextIter *at, gchar *text, gint size, gpointer data) {
            auto *s = static_cast<PracticeEditor *>(data);
            if (s->suppressed_)
                return;
            try {
                if (!s->enabled_ || size < 0 || size > rimes::practice::kMaximumTextBytes ||
                    !g_utf8_validate(text, size, nullptr))
                    throw std::runtime_error("invalid insert");
                auto candidate = s->Text();
                const int scalar = gtk_text_iter_get_offset(at);
                const auto offset =
                    g_utf8_offset_to_pointer(candidate.c_str(), scalar) - candidate.c_str();
                candidate.insert(offset, text, size);
                (void)rimes::practice::Characters(candidate);
                s->insertion_start_ = scalar;
            } catch (...) {
                g_signal_stop_emission_by_name(b, "insert-text");
                if (s->depth_)
                    s->rejected_action_ = true;
                s->Reject();
            }
        }),
        this);
    g_signal_connect_after(
        buffer_, "insert-text",
        G_CALLBACK(+[](GtkTextBuffer *, GtkTextIter *at, gchar *, gint, gpointer data) {
            auto *s = static_cast<PracticeEditor *>(data);
            if (s->suppressed_)
                return;
            try {
                s->insertion_ = rimes::practice::InsertionRange(s->Text(), s->insertion_start_,
                                                                gtk_text_iter_get_offset(at) -
                                                                    s->insertion_start_);
            } catch (...) {
                s->Reject();
            }
            s->dirty_ = true;
            s->Publish();
        }),
        this);
    g_signal_connect(buffer_, "delete-range",
                     G_CALLBACK(+[](GtkTextBuffer *b, GtkTextIter *, GtkTextIter *, gpointer data) {
                         auto *s = static_cast<PracticeEditor *>(data);
                         if (!s->suppressed_ && !s->enabled_)
                             g_signal_stop_emission_by_name(b, "delete-range");
                     }),
                     this);
    g_signal_connect_after(
        buffer_, "delete-range",
        G_CALLBACK(+[](GtkTextBuffer *, GtkTextIter *, GtkTextIter *, gpointer data) {
            auto *s = static_cast<PracticeEditor *>(data);
            if (s->suppressed_)
                return;
            s->dirty_ = true;
            s->Publish();
        }),
        this);
    g_signal_connect(view_, "paste-clipboard", G_CALLBACK(+[](GtkTextView *v, gpointer data) {
                         g_signal_stop_emission_by_name(v, "paste-clipboard");
                         static_cast<PracticeEditor *>(data)->Reject();
                     }),
                     this);
    g_signal_connect(view_, "button-press-event",
                     G_CALLBACK(+[](GtkWidget *, GdkEventButton *e, gpointer data) -> gboolean {
                         if (e->button != 2)
                             return FALSE;
                         static_cast<PracticeEditor *>(data)->Reject();
                         return TRUE;
                     }),
                     this);
    g_signal_connect(view_, "focus-out-event",
                     G_CALLBACK(+[](GtkWidget *, GdkEvent *, gpointer data) -> gboolean {
                         auto *s = static_cast<PracticeEditor *>(data);
                         if (s->enabled_ && s->before_blur)
                             s->before_blur();
                         return FALSE;
                     }),
                     this);
    g_signal_connect_after(view_, "focus-out-event",
                           G_CALLBACK(+[](GtkWidget *, GdkEvent *, gpointer data) -> gboolean {
                               auto *s = static_cast<PracticeEditor *>(data);
                               s->down_.clear();
                               if (s->enabled_ && s->blur)
                                   s->blur();
                               return FALSE;
                           }),
                           this);
}
PracticeEditor::~PracticeEditor() {
    g_signal_handlers_disconnect_by_data(view_, this);
    g_signal_handlers_disconnect_by_data(buffer_, this);
    gtk_widget_destroy(view_);
    g_object_unref(view_);
    g_object_unref(buffer_);
}
std::string PracticeEditor::Text() const {
    GtkTextIter a, b;
    gtk_text_buffer_get_bounds(buffer_, &a, &b);
    auto *text = gtk_text_buffer_get_text(buffer_, &a, &b, FALSE);
    std::string result(text);
    g_free(text);
    return result;
}
void PracticeEditor::Enable(bool enabled) {
    enabled_ = enabled;
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view_), enabled);
}
void PracticeEditor::PrepareInputContext() {
    // GTK does not focus its IM context for a read-only text view. Keep our
    // key/insert guards closed while letting GTK establish the native IC.
    enabled_ = false;
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view_), TRUE);
}
void PracticeEditor::Reset(const std::string &text) {
    suppressed_ = true;
    gtk_text_view_reset_im_context(GTK_TEXT_VIEW(view_));
    gtk_text_buffer_set_text(buffer_, text.c_str(), text.size());
    suppressed_ = false;
    dirty_ = composing_ = false;
    insertion_.reset();
    down_.clear();
    seen_.clear();
    depth_ = 0;
}
void PracticeEditor::Reject() {
    if (enabled_ && assistance)
        assistance();
}
void PracticeEditor::Publish() {
    if (suppressed_ || !enabled_ || depth_ || composing_ || !dirty_)
        return;
    const auto range = insertion_;
    insertion_.reset();
    dirty_ = false;
    if (commit)
        commit(Text(), range);
}
} // namespace rimes::desktop
