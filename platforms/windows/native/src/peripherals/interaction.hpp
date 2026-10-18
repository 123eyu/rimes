#pragma once
#include <Windows.h>

namespace rimes::windows::peripherals {
// Installed only while the quick rail is visible. Never swallows a click or
// performs storage/input work inside the low-level callback.
class OutsideClick {
 public:
  static constexpr UINT kDismiss = WM_APP + 187;
  ~OutsideClick() { Stop(); }
  bool Start(HWND window) {
    Stop();
    if (active_) active_->Stop();
    window_ = window;
    ++generation_;
    active_ = this;
    hook_ = SetWindowsHookExW(WH_MOUSE_LL, Hook, GetModuleHandleW(nullptr), 0);
    return hook_ != nullptr;
  }
  void Stop() {
    if (hook_) UnhookWindowsHookEx(hook_);
    hook_ = nullptr; window_ = nullptr;
    if (active_ == this) active_ = nullptr;
  }
  bool Owns(HWND hit) const {
    if (!window_ || !hit) return false;
    if (hit == window_ || IsChild(window_, hit) || GetAncestor(hit, GA_ROOTOWNER) == window_) return true;
    wchar_t name[32]{};
    GetClassNameW(hit, name, 32);
    // Native popup menus are separate top-level windows on our UI thread.
    return GetWindowThreadProcessId(hit, nullptr) == GetWindowThreadProcessId(window_, nullptr) &&
        (wcscmp(name, L"#32768") == 0 || wcscmp(name, L"ComboLBox") == 0);
  }
  void Observe(HWND hit) const {
    if (window_ && IsWindowVisible(window_) && !Owns(hit))
      PostMessageW(window_, kDismiss, generation_, 0);
  }
  bool Current(WPARAM generation) const { return window_ && generation == generation_; }
 private:
  inline static thread_local OutsideClick* active_ = nullptr;
  HWND window_ = nullptr;
  HHOOK hook_ = nullptr;
  WPARAM generation_ = 0;
  static LRESULT CALLBACK Hook(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION && active_ && (message == WM_LBUTTONDOWN ||
        message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN)) {
      const auto* event = reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
      active_->Observe(WindowFromPoint(event->pt));
    }
    return CallNextHookEx(nullptr, code, message, data);
  }
};
}  // namespace rimes::windows::peripherals
