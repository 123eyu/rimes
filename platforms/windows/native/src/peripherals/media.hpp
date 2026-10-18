#pragma once
#include <functional>
#include "store.hpp"
namespace rimes::windows::peripherals {
struct ClipboardContent {
  bool opened = false;
  std::string text, extension, bytes;
  std::vector<std::filesystem::path> files;
};
bool InteractiveDesktop();
ClipboardContent ReadClipboard(HWND owner);
// Keep observation independent of Capsule's window lifetime. A supplied source
// also lets the background collection fixture avoid the user's clipboard.
struct ClipboardSource {
  std::function<DWORD()> sequence = GetClipboardSequenceNumber;
  std::function<bool()> interactive = InteractiveDesktop;
  std::function<ClipboardContent(HWND)> read = ReadClipboard;
};
HBITMAP Thumbnail(const std::filesystem::path& path, unsigned maximum = 1200);
}  // namespace rimes::windows::peripherals
