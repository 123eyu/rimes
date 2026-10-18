#include "chord_editor.hpp"
#include "chord_learning.hpp"
#include "chord_store.hpp"
#include <fstream>
#include <iostream>
#include <unistd.h>

using namespace rimes::desktop;
using namespace rimes::chord;
namespace fs = std::filesystem;
void Check(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}
GtkWidget* Find(GtkWidget* widget, const char* title) {
    const auto* name = atk_object_get_name(gtk_widget_get_accessible(widget));
    if (name && std::string(name) == title)
        return widget;
    if (GTK_IS_CONTAINER(widget)) {
        auto* children = gtk_container_get_children(GTK_CONTAINER(widget));
        GtkWidget* found = nullptr;
        for (auto* item = children; item && !found; item = item->next)
            found = Find(GTK_WIDGET(item->data), title);
        g_list_free(children);
        if (found)
            return found;
    }
    return nullptr;
}
bool SaveRender(GtkWidget* root, const std::string& path) {
    while (gtk_events_pending())
        gtk_main_iteration();
    gtk_container_check_resize(GTK_CONTAINER(root));
    Json frames = Json::array();
    std::function<void(GtkWidget*)> visit = [&](GtkWidget* widget) {
        int x = 0, y = 0;
        gtk_widget_translate_coordinates(widget, root, 0, 0, &x, &y);
        Json item = {{"type", G_OBJECT_TYPE_NAME(widget)},
                     {"x", x},
                     {"y", y},
                     {"width", gtk_widget_get_allocated_width(widget)},
                     {"height", gtk_widget_get_allocated_height(widget)}};
        auto* classes = gtk_style_context_list_classes(gtk_widget_get_style_context(widget));
        item["classes"] = Json::array();
        for (auto* entry = classes; entry; entry = entry->next)
            item["classes"].push_back(static_cast<const char*>(entry->data));
        g_list_free(classes);
        if (GTK_IS_LABEL(widget))
            item["text"] = gtk_label_get_text(GTK_LABEL(widget));
        frames.push_back(item);
        if (GTK_IS_CONTAINER(widget)) {
            auto* children = gtk_container_get_children(GTK_CONTAINER(widget));
            for (auto* child = children; child; child = child->next)
                visit(GTK_WIDGET(child->data));
            g_list_free(children);
        }
    };
    visit(root);
    std::ofstream(path + ".geometry.json") << frames.dump(2) << '\n';
    return Render(root, path);
}
int main(int argc, char** argv) {
    char temporary[] = "/tmp/xsime-chord-ui-XXXXXX";
    if (!mkdtemp(temporary))
        return 1;
    const fs::path root(temporary);
    setenv("RIMES_USER_DIR", temporary, 1);
    setenv("NO_AT_BRIDGE", "1", 1);
    try {
        gtk_init(&argc, &argv);
        g_object_set(gtk_settings_get_default(), "gtk-enable-animations", FALSE, nullptr);
        Theme("night");
        Store store(root);
        const auto original = store.Active().profile;
        auto copy = store.Import(original.Value().dump());
        copy.name = "我的飞耀方案";
        store.Save(copy, "");
        auto* window = Class(gtk_offscreen_window_new(), "chord-surface");
        gtk_window_set_default_size(GTK_WINDOW(window), 698, 1);
        auto* dialog_parent = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_widget_show(dialog_parent);
        {
            ChordPane pane(GTK_WINDOW(dialog_parent), "keymap", true);
            gtk_widget_set_margin_start(pane.Widget(), 24);
            gtk_widget_set_margin_end(pane.Widget(), 24);
            gtk_widget_set_margin_bottom(pane.Widget(), 24);
            gtk_container_add(GTK_CONTAINER(window), pane.Widget());
            gtk_widget_show_all(window);
            auto get = [&](const char* name) {
                auto* value = Find(pane.Widget(), name);
                Check(value, name);
                return value;
            };
            gtk_combo_box_set_active(GTK_COMBO_BOX(get("键位方案")), 3);
            while (gtk_events_pending())
                gtk_main_iteration();
            Check(std::string(gtk_entry_get_text(GTK_ENTRY(get("键位名称")))) == copy.name,
                  "profile picker failed");
            auto* table = GTK_TREE_VIEW(get("键位映射表"));
            const auto mappings = copy.mappings.size();
            Check(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(table), nullptr) ==
                      static_cast<int>(mappings),
                  "mapping table incomplete");
            GtkTreeIter iter;
            auto* model = gtk_tree_view_get_model(table);
            bool selected = false;
            for (bool valid = gtk_tree_model_get_iter_first(model, &iter); valid;
                 valid = gtk_tree_model_iter_next(model, &iter)) {
                gchar* keys = nullptr;
                gtk_tree_model_get(model, &iter, 0, &keys, -1);
                const bool match = keys && std::string(keys) == "D + V + K + M";
                g_free(keys);
                if (!match)
                    continue;
                gtk_tree_selection_select_iter(gtk_tree_view_get_selection(table), &iter);
                auto* path = gtk_tree_model_get_path(model, &iter);
                gtk_tree_view_scroll_to_cell(table, path, nullptr, true, 0, 0);
                gtk_tree_path_free(path);
                selected = true;
                break;
            }
            Check(selected, "native DVKM mapping missing");
            gtk_button_clicked(GTK_BUTTON(get("试译和弦")));
            Check(Find(pane.Widget(), "DVKM → nong · 完整音节"),
                  "local tryout did not use selected profile");
            Check(pane.CanLeave() && store.Active().profile.Value() == original.Value(),
                  "preview dirtied or activated a profile");
            if (argc == 2) {
                fs::create_directories(argv[1]);
                for (const auto* theme : {"night", "day", "quiet", "rasta"}) {
                    Theme(theme);
                    while (gtk_events_pending())
                        gtk_main_iteration();
                    GtkTreeIter selected_iter;
                    GtkTreeModel* selected_model = nullptr;
                    if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(table),
                                                        &selected_model, &selected_iter)) {
                        auto* path = gtk_tree_model_get_path(selected_model, &selected_iter);
                        gtk_tree_view_scroll_to_cell(table, path, nullptr, true, 0, 0);
                        gtk_tree_path_free(path);
                    }
                    while (gtk_events_pending())
                        gtk_main_iteration();
                    Check(SaveRender(window,
                                     (fs::path(argv[1]) / (std::string("keymap-") + theme + ".png"))
                                         .string()),
                          "keymap render failed");
                }
            }
            gtk_entry_set_text(GTK_ENTRY(get("键位名称")), "已保存但未应用");
            gtk_button_clicked(GTK_BUTTON(get("保存草稿")));
            Check(store.Draft(copy.id).name == "已保存但未应用" &&
                      store.Active().profile.Value() == original.Value(),
                  "draft modified active snapshot");
            gtk_entry_set_text(GTK_ENTRY(get("和弦键输入")), "dvkm");
            gtk_entry_set_text(GTK_ENTRY(get("输出拼音输入")), "long");
            g_idle_add(
                +[](gpointer) -> gboolean {
                    auto* windows = gtk_window_list_toplevels();
                    for (auto* item = windows; item; item = item->next)
                        if (GTK_IS_MESSAGE_DIALOG(item->data))
                            gtk_dialog_response(GTK_DIALOG(item->data), GTK_RESPONSE_CANCEL);
                    g_list_free(windows);
                    return G_SOURCE_REMOVE;
                },
                nullptr);
            gtk_tree_model_get_iter_first(model, &iter);
            gtk_tree_selection_select_iter(gtk_tree_view_get_selection(table), &iter);
            Check(
                gtk_tree_selection_get_selected(gtk_tree_view_get_selection(table), nullptr, &iter),
                "cancelled row change lost the previous selection");
            gchar* still_selected = nullptr;
            gtk_tree_model_get(model, &iter, 0, &still_selected, -1);
            const bool unchanged = still_selected && std::string(still_selected) == "D + V + K + M";
            g_free(still_selected);
            Check(unchanged &&
                      std::string(gtk_entry_get_text(GTK_ENTRY(get("输出拼音输入")))) == "long",
                  "cancelled row change lost or mismatched pending edits");
            gtk_button_clicked(GTK_BUTTON(get("添加或更新映射")));
            gtk_button_clicked(GTK_BUTTON(get("保存草稿")));
            Check(store.Draft(copy.id).mappings.size() == mappings,
                  "selected mapping was appended instead of updated");
            Check(store.Draft(copy.id).Find("dvkm")->output == "long",
                  "updated mapping was not saved");
            Check(pane.CanLeave(), "saved draft stayed dirty");
            gtk_entry_set_text(GTK_ENTRY(get("搜索键位或输出拼音")), "  LONG  ");
            Check(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(table), nullptr) == 2,
                  "search did not normalize output case and whitespace");
            gtk_entry_set_text(GTK_ENTRY(get("键位名称")), "这次修改应当放弃");
            g_idle_add(
                +[](gpointer) -> gboolean {
                    auto* windows = gtk_window_list_toplevels();
                    for (auto* item = windows; item; item = item->next)
                        if (GTK_IS_MESSAGE_DIALOG(item->data))
                            gtk_dialog_response(GTK_DIALOG(item->data), 2);
                    g_list_free(windows);
                    return G_SOURCE_REMOVE;
                },
                nullptr);
            gtk_button_clicked(GTK_BUTTON(get("复制方案")));
            while (gtk_events_pending())
                gtk_main_iteration();
            Check(std::string(gtk_entry_get_text(GTK_ENTRY(get("键位名称")))) ==
                      "已保存但未应用 副本",
                  "discarded edits leaked into a copied profile");
        }
        for (const auto* page : {"settings", "lessons", "practice", "progress"}) {
            ChordPane pane(GTK_WINDOW(window), page, true, {},
                           {{"ok", true}, {"enabled", false}, {"available", false}});
            gtk_widget_set_margin_start(pane.Widget(), 24);
            gtk_widget_set_margin_end(pane.Widget(), 24);
            gtk_widget_set_margin_bottom(pane.Widget(), 22);
            gtk_container_add(GTK_CONTAINER(window), pane.Widget());
            gtk_window_resize(GTK_WINDOW(window), 698, 1);
            gtk_widget_show_all(window);
            Check(pane.CanLeave(), "read-only page blocked navigation");
            if (argc == 2)
                for (const auto* theme : {"night", "day", "quiet", "rasta"}) {
                    Theme(theme);
                    while (gtk_events_pending())
                        gtk_main_iteration();
                    Check(SaveRender(window, (fs::path(argv[1]) /
                                              (std::string(page) + "-" + theme + ".png"))
                                                 .string()),
                          "page render failed");
                }
            if (std::string(page) == "practice") {
                const auto courses = Curriculum(original);
                const auto course =
                    *std::find_if(courses.begin(), courses.end(),
                                  [](const auto& item) { return item.key_count == 2; });
                const auto exercise =
                    Sample(course, 30, Json::object(), std::time(nullptr) / 86400).front();
                auto* region = Find(pane.Widget(), "并击练习区域");
                Check(region, "capture widget missing");
                auto* start = Find(pane.Widget(), "开始练习");
                Check(start, "practice start missing");
                gtk_button_clicked(GTK_BUTTON(start));
                auto key = [&](char value, GdkEventType type) {
                    GdkEventKey event{};
                    event.type = type;
                    event.keyval = value;
                    event.hardware_keycode = value;
                    gboolean handled = false;
                    g_signal_emit_by_name(
                        region, type == GDK_KEY_PRESS ? "key-press-event" : "key-release-event",
                        &event, &handled);
                };
                for (char value : exercise.keys)
                    key(value, GDK_KEY_PRESS);
                for (char value : exercise.keys)
                    key(value, GDK_KEY_RELEASE);
                ProgressStore progress(root, original.SchemaID());
                const auto recorded = progress.Load();
                Check(recorded.at("items").at(exercise.id).at("correctAttempts") == 1,
                      "actual practice event did not record correct answer");
                for (char value : exercise.keys)
                    key(value, GDK_KEY_PRESS);
                for (char value : exercise.keys)
                    key(value, GDK_KEY_RELEASE);
                Check(progress.Load() == recorded,
                      "success feedback scored the invisible next question");
                GdkEventFocus focus{};
                focus.type = GDK_FOCUS_CHANGE;
                gboolean handled = false;
                g_signal_emit_by_name(region, "focus-out-event", &focus, &handled);
                for (char value : exercise.keys)
                    key(value, GDK_KEY_PRESS);
                for (char value : exercise.keys)
                    key(value, GDK_KEY_RELEASE);
                Check(progress.Load() == recorded, "unfocused capture recorded input");
                (void)progress.Clear(0);
            }
        }
        // A saved, unapplied key-zone draft must not alter the active course's
        // physical key capture. Otherwise valid answers silently stop scoring.
        store.Activate(copy);
        const auto active_courses = Curriculum(copy);
        const auto course = *std::find_if(active_courses.begin(), active_courses.end(),
                                          [](const auto& item) { return item.key_count == 2; });
        const auto exercise =
            Sample(course, 30, Json::object(), std::time(nullptr) / 86400).front();
        auto draft = copy;
        draft.left = std::string(1, copy.left.at(copy.left.find_first_not_of(exercise.keys)));
        draft.right = std::string(1, copy.right.at(copy.right.find_first_not_of(exercise.keys)));
        draft.mappings = {{draft.left + draft.right, "a", true}};
        store.Save(draft, store.Revision(copy.id));
        {
            ChordPane pane(GTK_WINDOW(window), "practice", true);
            gtk_container_add(GTK_CONTAINER(window), pane.Widget());
            gtk_widget_show_all(window);
            auto* region = Find(pane.Widget(), "并击练习区域");
            Check(region, "active practice capture missing");
            gtk_button_clicked(GTK_BUTTON(Find(pane.Widget(), "开始练习")));
            for (auto type : {GDK_KEY_PRESS, GDK_KEY_RELEASE})
                for (char value : exercise.keys) {
                    GdkEventKey event{};
                    event.type = type;
                    event.keyval = value;
                    event.hardware_keycode = value;
                    gboolean handled = false;
                    g_signal_emit_by_name(
                        region, type == GDK_KEY_PRESS ? "key-press-event" : "key-release-event",
                        &event, &handled);
                }
            ProgressStore progress(root, copy.SchemaID());
            Check(progress.Load().at("items").at(exercise.id).at("correctAttempts") == 1,
                  "unapplied draft altered active learning key zones");
        }
        gtk_widget_destroy(window);
        gtk_widget_destroy(dialog_parent);
        fs::remove_all(root);
        std::cout << "PASS: actual keymap controls, draft/apply isolation, tryout and five "
                     "settings pages\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        fs::remove_all(root);
        return 1;
    }
}
