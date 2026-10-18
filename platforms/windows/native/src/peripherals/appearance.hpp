#pragma once

#include "../ui/icons.hpp"
#include "../ui/fluent_icons.hpp"
#include "store.hpp"

namespace rimes::windows::peripherals::appearance {

// Logical points from MailboxWorkspaceUI, MailboxAdaptiveSplitView and
// ClipboardHistoryWindowMetrics. The frame is computed before overlays so
// opening a drawer never resizes the transcript or the pseudo terminal.
struct MailLayout {
  int list_width, main_left;
  bool narrow, list_visible, overlay;
};
inline MailLayout Mail(int width, Module module, bool hidden, bool drawer) {
  const int list = module == Module::Terminal ? 200
                   : module == Module::Inbox  ? 232
                                              : 216;
  const bool narrow = width - 64 < (module == Module::Terminal ? 720 : 760);
  const bool visible = narrow ? drawer : !hidden;
  return {list, 64 + (visible && !narrow ? list : 0), narrow, visible,
          visible && narrow};
}
inline constexpr int kRailWidth = 940, kRailHeight = 208;
inline constexpr int kCardWidth = 206, kCardHeight = 126, kCardGap = 8;
inline constexpr int kManagerWidth = 940, kManagerHeight = 660;
inline constexpr int kEditorWidth = 460, kEditorHeight = 400;

struct Colors {
  std::uint32_t background, sidebar, field, selection, accent, text, secondary,
      border;
};
inline Colors Palette(ui::ThemeId theme, bool mail) {
  const auto& p = ui::Palette(theme);
  return {mail ? p.surface : p.buffer,
          p.surface_secondary,
          p.surface_tertiary,
          p.clipboard_selected,
          p.accent_text,
          p.text_primary,
          p.text_secondary,
          p.border};
}

class Paint {
 public:
  HDC dc;
  unsigned dpi;
  Paint(HDC d, unsigned scale) : dc(d), dpi(scale) {}
  int S(int value) const { return MulDiv(value, dpi, 96); }
  RECT R(int x, int y, int w, int h) const {
    return {S(x), S(y), S(x + w), S(y + h)};
  }
  void Fill(RECT r, std::uint32_t color, int radius = 0) const {
    if (radius)
      ui::FillRoundRect(dc, r, S(radius), ui::ToColorRef(color));
    else
      ui::FillRectColor(dc, r, ui::ToColorRef(color));
  }
  void Stroke(RECT r, std::uint32_t color, int radius = 6) const {
    ui::StrokeRoundRect(dc, r, S(radius), ui::ToColorRef(color));
  }
  void Text(const std::wstring& value, RECT r, int size, std::uint32_t color,
            int weight = FW_NORMAL,
            UINT format = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS,
            bool mono = false) const {
    const auto font = CreateFontW(
        -S(size), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, mono ? L"Cascadia Mono" : L"Microsoft YaHei UI");
    auto old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ui::ToColorRef(color));
    DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &r,
              format | DT_NOPREFIX);
    SelectObject(dc, old);
    DeleteObject(font);
  }
};

enum class Glyph {
  None,
  Plus,
  Edit,
  Sidebar,
  Inspector,
  Back,
  Close,
  Gear,
  More,
  Copy,
  Archive,
  Send,
  Stop,
  Folder,
  Eye,
  Camera,
  Temporary,
  Notes,
  Resources,
  Password,
  Terminal,
  Chat,
  Inbox
};
inline Glyph ModuleGlyph(Module m) {
  constexpr Glyph icons[] = {Glyph::Temporary, Glyph::Camera,   Glyph::Notes,
                             Glyph::Resources, Glyph::Password, Glyph::Terminal,
                             Glyph::Chat,      Glyph::Inbox};
  return icons[static_cast<unsigned>(m)];
}
inline ui::FluentIcon FluentGlyph(Glyph glyph) {
  using ui::FluentIcon;
  switch (glyph) {
    case Glyph::Plus: return FluentIcon::kAdd;
    case Glyph::Edit: return FluentIcon::kEdit;
    case Glyph::Sidebar: return FluentIcon::kPanelLeft;
    case Glyph::Inspector: return FluentIcon::kPanelRight;
    case Glyph::Back: return FluentIcon::kChevronLeft;
    case Glyph::Close: return FluentIcon::kClose;
    case Glyph::Gear: return FluentIcon::kSettings;
    case Glyph::More: return FluentIcon::kMore;
    case Glyph::Copy: return FluentIcon::kCopy;
    case Glyph::Archive: return FluentIcon::kCapsule;
    case Glyph::Send: return FluentIcon::kSend;
    case Glyph::Stop: return FluentIcon::kStop;
    case Glyph::Folder: case Glyph::Resources: return FluentIcon::kFolder;
    case Glyph::Eye: return FluentIcon::kEye;
    case Glyph::Camera: return FluentIcon::kCamera;
    case Glyph::Temporary: return FluentIcon::kClipboard;
    case Glyph::Notes: return FluentIcon::kNotes;
    case Glyph::Password: return FluentIcon::kKey;
    case Glyph::Terminal: return FluentIcon::kTerminal;
    case Glyph::Chat: return FluentIcon::kChat;
    case Glyph::Inbox: return FluentIcon::kInbox;
    default: return FluentIcon::kInfo;
  }
}
inline void Icon(const Paint& p, Glyph glyph, RECT r, std::uint32_t color) {
  if (glyph != Glyph::None)
    ui::DrawFluentIcon(p.dc, FluentGlyph(glyph), r, ui::ToColorRef(color));
}
}  // namespace rimes::windows::peripherals::appearance
