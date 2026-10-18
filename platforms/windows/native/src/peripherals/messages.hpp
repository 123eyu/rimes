#pragma once
#include <functional>
#include <memory>

#include "appearance.hpp"

namespace rimes::windows::peripherals {
struct MessageSpan {
  std::wstring text, link;
  bool bold = false, italic = false, code = false, quote = false;
  int heading = 0;
};
// Display-only Markdown. The original bytes remain the copy/save authority.
std::vector<MessageSpan> FormatMessage(const std::string& source,
                                       bool markdown);
enum class MessageAction { Copy, Stage, Save, OpenImage, SaveImage, Ignore };
struct MessageItem {
  std::string id, role, body, format, time;
  std::filesystem::path image;
  bool streaming = false, review = false;
};
class MessageView {
 public:
  MessageView(HWND parent, int id,
              std::function<void(const MessageItem&, MessageAction)> action);
  ~MessageView();
  HWND Handle() const;
  void Update(const std::string& conversation, std::vector<MessageItem> items,
              ui::ThemeId theme, unsigned dpi);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace rimes::windows::peripherals
