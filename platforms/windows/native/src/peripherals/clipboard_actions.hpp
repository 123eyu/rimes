#pragma once

#include "media.hpp"
#include "../ui/theme.hpp"

namespace rimes::windows::peripherals {
struct PasteTarget {
  HWND foreground = nullptr, focus = nullptr;
  DWORD process = 0, thread = 0;
  bool operator==(const PasteTarget&) const = default;
  explicit operator bool() const { return foreground && focus && process && thread; }
};
PasteTarget CurrentPasteTarget();
enum class PasteResult { Sent, Waiting, TargetChanged, ClipboardChanged, Blocked };
// The fixture replaces these OS boundaries; it never sends input to a real app.
struct PastePlatform {
  std::function<PasteTarget()> target = CurrentPasteTarget;
  std::function<DWORD()> sequence = GetClipboardSequenceNumber;
  std::function<bool()> interactive = InteractiveDesktop;
  std::function<bool()> keys_released;
  std::function<UINT(UINT, INPUT*, int)> send = SendInput;
};
PasteResult PasteClipboardImage(PasteTarget, DWORD sequence, PastePlatform = {});
struct ClipboardActions {
  std::function<bool(HWND, const std::string&, bool)> text;
  std::function<bool(HWND, Store&, const Json&)> asset;
  std::function<PasteTarget()> target = CurrentPasteTarget;
  std::function<PasteResult(PasteTarget, DWORD)> paste =
      [](PasteTarget target, DWORD sequence) { return PasteClipboardImage(target, sequence); };
  std::function<ULONGLONG()> now = GetTickCount64;
};
class ClipboardFeedback {
 public:
  ~ClipboardFeedback();
  void Show(const std::wstring&, ui::ThemeId, HWND anchor);
  void Hide();
 private:
  HWND hwnd_ = nullptr;
  static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
};
}  // namespace rimes::windows::peripherals
