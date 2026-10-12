#include "engine/rime_engine.hpp"
#include "engine/rime_paths.hpp"
#include "engine/rime_user_lock.hpp"
#include "scheme_import.hpp"
#include <algorithm>
#include <chrono>
#include <fcntl.h>
#include <fstream>
#include <gtk/gtk.h>
#include <iostream>
#include <memory>
#include <rime_api.h>
#include <signal.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
namespace fs = std::filesystem;
namespace schemes = rimes::schemes;
fs::path Executable() { return fs::read_symlink("/proc/self/exe"); }
// Compilation never shares the running Fcitx engine or its learned-word data.
void Deploy(const fs::path &staged, const std::vector<std::string> &ids) {
  auto command = std::vector<std::string>{Executable().string(), "--deploy",
                                          staged.string()};
  command.insert(command.end(), ids.begin(), ids.end());
  std::vector<char *> argv;
  for (auto &word : command)
    argv.push_back(word.data());
  argv.push_back(nullptr);
  const auto logs = staged.parent_path().parent_path() / "logs";
  fs::create_directories(logs);
  const auto log = logs / (staged.filename().string() + ".log");
  const int fd =
      open(log.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (fd < 0)
    throw std::runtime_error("无法创建方案导入日志");
  struct Close {
    int fd;
    ~Close() { close(fd); }
  } close_log{fd};
  GPid pid = 0;
  GError *error = nullptr;
  if (!g_spawn_async_with_fds(nullptr, argv.data(), nullptr,
                              G_SPAWN_DO_NOT_REAP_CHILD, nullptr, nullptr, &pid,
                              -1, fd, fd, &error)) {
    std::string message = error ? error->message : "无法启动方案编译";
    g_clear_error(&error);
    throw std::runtime_error(message);
  }
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::minutes(10);
  int status = 0;
  while (waitpid(pid, &status, WNOHANG) == 0) {
    if (std::chrono::steady_clock::now() > deadline) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      g_spawn_close_pid(pid);
      throw std::runtime_error("方案编译超过十分钟，已停止；原方案未改变。");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  g_spawn_close_pid(pid);
  schemes::CheckDeploymentLog(log);
  if (!WIFEXITED(status) || WEXITSTATUS(status))
    throw std::runtime_error("方案编译或加载失败，原方案未改变。请查看方案导入"
                             "日志。运行库可能缺少方案要求的 Lua API 或组件。");
}
int Compile(const fs::path &staged, const std::vector<std::string> &ids) {
  auto logs = staged.parent_path().parent_path() / "logs";
  fs::create_directories(logs);
  auto api = rime_get_api();
  const std::string path = staged.string(), log = logs.string();
  RIME_STRUCT(RimeTraits, traits);
  traits.shared_data_dir = path.c_str();
  traits.user_data_dir = path.c_str();
  traits.log_dir = log.c_str();
  traits.app_name = "rime.rimes.import";
  traits.min_log_level = 2;
  const char *modules[] = {"default", "lua", "deployer", nullptr};
  traits.modules = modules;
  api->setup(&traits);
  api->initialize(&traits);
  struct Finish {
    RimeApi *api;
    ~Finish() { api->finalize(); }
  } finish{api};
  // Older distro librime releases pass the wrong type to schema_update in
  // deploy_schema(). The standard workspace deployment API works across them.
  if (!api->deploy())
    throw std::runtime_error("方案工作区编译失败");
  for (const auto &id : ids) {
    auto session = api->create_session();
    if (!session)
      throw std::runtime_error("无法建立方案测试会话");
    bool ok = api->select_schema(session, id.c_str());
    if (ok) {
      api->set_option(session, "ascii_mode", false);
      // Exercise lazy translators/filters too; their Lua errors may only
      // appear on a key, long after select_schema() reports success.
      for (const auto *sample : {"a", "nihao", "1234567890"}) {
        for (const char *key = sample; *key; ++key)
          api->process_key(session, *key, 0);
        api->clear_composition(session);
      }
    }
    api->destroy_session(session);
    if (!ok)
      throw std::runtime_error("方案无法加载：" + id);
  }
  return 0;
}
schemes::Review Inspect(const fs::path &path, const fs::path &generated) {
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 ::tolower);
  return extension == ".txt" || extension == ".tsv"
             ? schemes::InspectText(path, generated)
             : schemes::Inspect(path);
}
struct Manager {
  schemes::Library library{rimes::linuxime::SchemeLibraryRoot()};
  GtkWidget *window = nullptr, *list = nullptr, *status = nullptr,
            *buttons = nullptr;
  bool busy = false;
  std::vector<std::pair<std::string, std::string>> rows;
  void Message(const std::string &text) {
    gtk_label_set_text(GTK_LABEL(status), text.c_str());
  }
  void Refresh() {
    GList *children = gtk_container_get_children(GTK_CONTAINER(list));
    for (auto *entry = children; entry; entry = entry->next)
      gtk_widget_destroy(GTK_WIDGET(entry->data));
    g_list_free(children);
    rows.clear();
    auto active = library.Active();
    for (const auto &package : library.Packages())
      for (const auto &schema : package.schemes) {
        auto title =
            package.name + " · " + schema.name + " (" + schema.id + ")";
        if (active && active->package.id == package.id &&
            active->schema == schema.id)
          title += "  ✓";
        auto label = gtk_label_new(title.c_str());
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_widget_set_margin_start(label, 12);
        gtk_widget_set_margin_top(label, 12);
        gtk_widget_set_margin_bottom(label, 12);
        gtk_list_box_insert(GTK_LIST_BOX(list), label, -1);
        rows.emplace_back(package.id, schema.id);
      }
    gtk_widget_show_all(list);
  }
  void Choose() {
    auto dialog = gtk_file_chooser_dialog_new(
        "导入 Rime 方案或文本词库", GTK_WINDOW(window),
        GTK_FILE_CHOOSER_ACTION_OPEN, "取消", GTK_RESPONSE_CANCEL, "打开",
        GTK_RESPONSE_ACCEPT, nullptr);
    auto filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Rime ZIP / TXT / TSV");
    for (auto pattern : {"*.zip", "*.ZIP", "*.txt", "*.tsv"})
      gtk_file_filter_add_pattern(filter, pattern);
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT) {
      gtk_widget_destroy(dialog);
      return;
    }
    char *selected = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    fs::path path(selected);
    g_free(selected);
    gtk_widget_destroy(dialog);
    busy = true;
    gtk_widget_set_sensitive(buttons, false);
    Message("正在检查文件…");
    // Copying / ZIP CRC / dictionary headers run off the UI thread.
    std::thread([this, path] {
      struct Result {
        Manager *owner;
        std::optional<schemes::Review> review;
        std::string error;
        fs::path temp;
      };
      auto result = new Result{this, {}, {}, {}};
      try {
        GError *error = nullptr;
        char *directory = g_dir_make_tmp("rimes-schemes-XXXXXX", &error);
        if (!directory) {
          std::string message = error ? error->message : "无法准备暂存目录";
          g_clear_error(&error);
          throw std::runtime_error(message);
        }
        result->temp = directory;
        g_free(directory);
        result->review = Inspect(path, result->temp / "text.zip");
      } catch (const std::exception &e) {
        result->error = e.what();
      }
      g_idle_add(
          [](gpointer data) -> gboolean {
            std::unique_ptr<Result> r(static_cast<Result *>(data));
            auto *self = r->owner;
            if (!r->review) {
              self->Message(r->error);
              self->busy = false;
              gtk_widget_set_sensitive(self->buttons, true);
            } else
              self->Review(*r->review, r->temp);
            if (!r->review) {
              std::error_code ignored;
              fs::remove_all(r->temp, ignored);
            }
            return G_SOURCE_REMOVE;
          },
          result);
    }).detach();
  }
  void Review(const schemes::Review &review, fs::path temporary) {
    auto dialog = gtk_dialog_new_with_buttons(
        "选择要导入的方案", GTK_WINDOW(window), GTK_DIALOG_MODAL, "取消",
        GTK_RESPONSE_CANCEL, "编译并导入", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 560, 440);
    auto area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    auto scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_widget_set_vexpand(scroll, true);
    auto box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_add(GTK_CONTAINER(scroll), box);
    gtk_container_add(GTK_CONTAINER(area), scroll);
    std::vector<GtkWidget *> checks;
    bool recommended = std::any_of(review.schemes.begin(), review.schemes.end(),
                                   [](const auto &s) { return s.recommended; });
    for (const auto &schema : review.schemes) {
      auto check = gtk_check_button_new_with_label(
          (schema.name + " (" + schema.id + ")").c_str());
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check),
                                   schema.recommended ||
                                       (!recommended && checks.empty()));
      gtk_box_pack_start(GTK_BOX(box), check, false, false, 2);
      checks.push_back(check);
    }
    std::string warning = "原始文件保持不变。启用或恢复方案后，请重启 "
                          "Fcitx5；已导入不代表已经启用。";
    for (const auto &item : review.warnings)
      warning += "\n" + item;
    auto label = gtk_label_new(warning.c_str());
    gtk_label_set_line_wrap(GTK_LABEL(label), true);
    gtk_box_pack_start(GTK_BOX(box), label, false, false, 8);
    gtk_widget_show_all(dialog);
    std::vector<std::string> selected;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
      for (std::size_t i = 0; i < checks.size(); ++i)
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checks[i])))
          selected.push_back(review.schemes[i].id);
    gtk_widget_destroy(dialog);
    if (selected.empty()) {
      fs::remove_all(temporary);
      busy = false;
      gtk_widget_set_sensitive(buttons, true);
      Message("未导入，原方案未改变。");
      return;
    }
    Message("正在编译并验证方案，请稍候…");
    std::thread([this, review, selected, temporary] {
      struct Result {
        Manager *owner;
        std::string message;
      };
      auto result = new Result{this, "已导入。请选择方案并启用。"};
      try {
        library.Install(review, selected, Deploy);
      } catch (const std::exception &e) {
        result->message = e.what();
      }
      std::error_code ignored;
      fs::remove_all(temporary, ignored);
      g_idle_add(
          [](gpointer data) -> gboolean {
            std::unique_ptr<Result> result(static_cast<Result *>(data));
            result->owner->Refresh();
            result->owner->Message(result->message);
            result->owner->busy = false;
            gtk_widget_set_sensitive(result->owner->buttons, true);
            return G_SOURCE_REMOVE;
          },
          result);
    }).detach();
  }
  void Change(int action) {
    try {
      if (action == 0)
        library.Deactivate();
      else {
        auto row = gtk_list_box_get_selected_row(GTK_LIST_BOX(list));
        if (!row) {
          Message("请先选择方案。");
          return;
        }
        auto selected =
            rows.at(static_cast<std::size_t>(gtk_list_box_row_get_index(row)));
        if (action == 1)
          library.Activate(selected.first, selected.second);
        else {
          auto dialog = gtk_message_dialog_new(
              GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
              GTK_BUTTONS_OK_CANCEL, "%s",
              "删除整个方案包？学习记录保留。请确认已重启输入法，不再使用该包"
              "。");
          auto response = gtk_dialog_run(GTK_DIALOG(dialog));
          gtk_widget_destroy(dialog);
          if (response != GTK_RESPONSE_OK)
            return;
          rimes::linuxime::RimeUserLock unused;
          std::string error;
          const auto learning = library.Root() / "learning" / selected.first;
          if (fs::exists(learning) && !unused.Acquire(learning, &error))
            throw std::runtime_error(
                "方案仍被输入法使用，请先恢复内置并重启 Fcitx5。");
          library.Remove(selected.first);
        }
      }
      Refresh();
      Message(action == 2
                  ? "已删除方案包，学习记录保留。"
                  : "选择已保存。请重启 Fcitx5 后使用；正在输入的内容未改变。");
    } catch (const std::exception &e) {
      Message(e.what());
    }
  }
  int Run(int argc, char **argv) {
    gtk_init(&argc, &argv);
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "灵犀输入法 · 方案管理");
    gtk_window_set_default_size(GTK_WINDOW(window), 720, 520);
    g_signal_connect(
        window, "delete-event",
        G_CALLBACK(+[](GtkWidget *, GdkEvent *, gpointer p) -> gboolean {
          return static_cast<Manager *>(p)->busy;
        }),
        this);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
    auto box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(box), 18);
    gtk_container_add(GTK_CONTAINER(window), box);
    auto label =
        gtk_label_new("导入同一份 Rime ZIP，或带编码的 UTF-8 TXT / TSV 词库");
    gtk_box_pack_start(GTK_BOX(box), label, false, false, 0);
    auto scroll = gtk_scrolled_window_new(nullptr, nullptr);
    list = gtk_list_box_new();
    gtk_container_add(GTK_CONTAINER(scroll), list);
    gtk_box_pack_start(GTK_BOX(box), scroll, true, true, 0);
    buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(box), buttons, false, false, 0);
    auto import = gtk_button_new_with_label("导入文件…");
    gtk_box_pack_start(GTK_BOX(buttons), import, false, false, 0);
    g_signal_connect(import, "clicked",
                     G_CALLBACK(+[](GtkButton *, gpointer p) {
                       static_cast<Manager *>(p)->Choose();
                     }),
                     this);
    for (const auto &pair : std::vector<std::pair<const char *, int>>{
             {"启用", 1}, {"恢复内置", 0}, {"删除方案包", 2}}) {
      auto button = gtk_button_new_with_label(pair.first);
      gtk_box_pack_start(GTK_BOX(buttons), button, false, false, 0);
      g_object_set_data(G_OBJECT(button), "action",
                        GINT_TO_POINTER(pair.second));
      g_signal_connect(button, "clicked",
                       G_CALLBACK(+[](GtkButton *b, gpointer p) {
                         static_cast<Manager *>(p)->Change(GPOINTER_TO_INT(
                             g_object_get_data(G_OBJECT(b), "action")));
                       }),
                       this);
    }
    status = gtk_label_new(
        "启用或恢复方案后重启 Fcitx5 生效。编译失败不会替换当前方案。");
    gtk_label_set_line_wrap(GTK_LABEL(status), true);
    gtk_box_pack_start(GTK_BOX(box), status, false, false, 0);
    Refresh();
    gtk_widget_show_all(window);
    gtk_main();
    return 0;
  }
};
} // namespace
int main(int argc, char **argv) {
  try {
    if (argc >= 4 && std::string(argv[1]) == "--deploy")
      return Compile(argv[2], {argv + 3, argv + argc});
    if (argc >= 5 && std::string(argv[1]) == "--import") {
      schemes::Library library(fs::absolute(argv[2]));
      auto review = schemes::Inspect(fs::absolute(argv[3]));
      auto package = library.Install(review, {argv + 4, argv + argc}, Deploy);
      library.Activate(package.id, argv[4]);
      std::cout << package.id << ' ' << package.source_digest << '\n';
      return 0;
    }
    if (argc == 5 && std::string(argv[1]) == "--verify") {
      schemes::Library library(fs::absolute(argv[2]));
      auto selected = library.Active();
      if (!selected)
        throw std::runtime_error("No imported scheme is active");
      rimes::linuxime::RimeEngine engine;
      rimes::linuxime::RimeEngineOptions options;
      options.shared_data_dir = selected->resources;
      options.user_data_dir = selected->user;
      options.log_dir = library.Root() / "logs";
      std::string error;
      if (!engine.Start(options, &error))
        throw std::runtime_error(error);
      auto session = engine.CreateSession(&error);
      if (!session || !engine.SelectSchema(session, selected->schema, &error))
        throw std::runtime_error(error);
      engine.SetOption(session, "ascii_mode", false);
      rimes::linuxime::EngineSnapshot snapshot;
      for (const char c : std::string(argv[3]))
        if (!engine.ProcessKey(session, c, 0, &snapshot, &error))
          throw std::runtime_error(error);
      const auto candidate =
          std::find_if(snapshot.candidates.begin(), snapshot.candidates.end(),
                       [&](const auto &c) { return c.text == argv[4]; });
      if (candidate == snapshot.candidates.end())
        throw std::runtime_error("Expected candidate missing");
      // Schemes own their Space/Return bindings. Select the actual candidate
      // through the same engine API as a candidate click instead of assuming
      // Space commits immediately (Bopomofo can open its selection menu).
      const int index = static_cast<int>(candidate - snapshot.candidates.begin());
      if (!engine.SelectCandidate(session, index, &snapshot, &error))
        throw std::runtime_error(error);
      // Fluency-editor schemes keep the selected phrase in composition until
      // explicit confirmation; that is a supported scheme behavior too.
      if (snapshot.commit_text.empty() &&
          !engine.CommitComposition(session, &snapshot, &error))
        throw std::runtime_error(error);
      if (snapshot.commit_text != argv[4])
        throw std::runtime_error("Unexpected commit");
      engine.DestroySession(session);
      engine.Stop();
      std::cout << "PASS native candidate and commit; source SHA256="
                << selected->package.source_digest << '\n';
      return 0;
    }
    Manager manager;
    return manager.Run(argc, argv);
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
