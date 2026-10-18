#pragma once
#include <memory>

#include "../ui/theme.hpp"
#include "../workbench/runtime.hpp"
#include "store.hpp"
#include "media.hpp"
#include "clipboard_actions.hpp"

namespace rimes::windows::peripherals {
class Windows {
 public:
  explicit Windows(
      workbench::Runtime&, std::filesystem::path root = DefaultRoot(),
      workbench::Runtime::APIGenerator generate = workbench::GenerateText,
      ClipboardSource clipboard = {}, ClipboardActions actions = {});
  ~Windows();
  void Open(Module);
  void Open(bool mailbox = false);
  void Toggle(bool mailbox);
  void Manage(bool mailbox = false);
  void Tick();
  void Protect();
  void Resume();
  void PreviewTheme(std::optional<ui::ThemeId>);
  void ClipboardChanged();
  bool CollectClipboard() const;
  void SetCollectClipboard(bool);
  void CaptureArea();
  bool HandleMessage(MSG*);
  HWND Handle(bool mailbox) const;
  std::vector<workbench::PluginView> Plugins() const;
  bool SetPluginEnabled(const std::string& id, bool enabled,
                        std::string* error);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace rimes::windows::peripherals
