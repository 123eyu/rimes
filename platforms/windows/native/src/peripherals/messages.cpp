#include "messages.hpp"

#include <commctrl.h>
#include <richedit.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <sstream>

#include "media.hpp"

namespace rimes::windows::peripherals {
namespace {
COLORREF Color(std::uint32_t c) {
  return RGB(c >> 16, (c >> 8) & 255, c & 255);
}
bool SafeLink(const std::wstring& s) {
  return (s.starts_with(L"https://") || s.starts_with(L"http://") ||
          s.starts_with(L"mailto:")) &&
         s.find_first_of(L"\r\n\t\"\0", 0, 5) == std::wstring::npos;
}
void Inline(std::vector<MessageSpan>& out, const std::wstring& s,
            MessageSpan style, unsigned depth = 0) {
  if (depth > 8) {
    style.text = s;
    out.push_back(style);
    return;
  }
  std::wstring plain;
  auto flush = [&] {
    if (!plain.empty()) {
      auto p = style;
      p.text = std::move(plain);
      out.push_back(p);
      plain.clear();
    }
  };
  for (std::size_t i = 0; i < s.size();) {
    if (s[i] == L'\\' && i + 1 < s.size()) {
      plain += s[i + 1];
      i += 2;
      continue;
    }
    if (s[i] == L'[') {
      const auto mid = s.find(L"](", i + 1),
                 end = mid == s.npos ? s.npos : s.find(L')', mid + 2);
      if (end != s.npos) {
        const auto url = s.substr(mid + 2, end - mid - 2);
        if (SafeLink(url)) {
          flush();
          auto link = style;
          link.link = url;
          Inline(out, s.substr(i + 1, mid - i - 1), link, depth + 1);
          i = end + 1;
          continue;
        }
      }
    }
    std::wstring mark;
    if (s.compare(i, 2, L"**") == 0 || s.compare(i, 2, L"__") == 0)
      mark = s.substr(i, 2);
    else if (s[i] == L'*' || s[i] == L'_' || s[i] == L'`')
      mark = s.substr(i, 1);
    if (!mark.empty()) {
      const auto end = s.find(mark, i + mark.size());
      if (end != s.npos && end > i + mark.size()) {
        flush();
        auto nested = style;
        if (mark == L"`")
          nested.code = true;
        else if (mark.size() == 2)
          nested.bold = true;
        else
          nested.italic = true;
        auto text = s.substr(i + mark.size(), end - i - mark.size());
        if (nested.code) {
          nested.text = text;
          out.push_back(nested);
        } else
          Inline(out, text, nested, depth + 1);
        i = end + mark.size();
        continue;
      }
    }
    plain += s[i++];
  }
  flush();
}
}  // namespace
std::vector<MessageSpan> FormatMessage(const std::string& source,
                                       bool markdown) {
  std::vector<MessageSpan> result;
  auto wide = workbench::Wide(source);
  if (!markdown) {
    result.push_back({wide});
    return result;
  }
  std::wistringstream lines(wide);
  std::wstring line, fence;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == L'\r') line.pop_back();
    auto trimmed =
        line.substr((std::min)(line.find_first_not_of(L" \t"), line.size()));
    if (trimmed.starts_with(L"```") || trimmed.starts_with(L"~~~")) {
      if (fence.empty())
        fence = trimmed.substr(0, 3);
      else if (trimmed.starts_with(fence))
        fence.clear();
      continue;
    }
    MessageSpan style;
    if (!fence.empty()) {
      style.text = line + L"\r";
      style.code = true;
      result.push_back(style);
      continue;
    }
    auto heading = trimmed.find_first_not_of(L'#');
    if (heading > 0 && heading <= 6 && heading < trimmed.size() &&
        trimmed[heading] == L' ') {
      style.heading = static_cast<int>(heading);
      style.bold = true;
      line = trimmed.substr(heading + 1);
    } else if (trimmed.starts_with(L">")) {
      style.quote = true;
      line = L"│ " + trimmed.substr(1);
    } else if (trimmed.starts_with(L"- [ ] ") || trimmed.starts_with(L"* [ ] "))
      line = L"☐ " + trimmed.substr(6);
    else if (trimmed.starts_with(L"- [x] ") || trimmed.starts_with(L"- [X] "))
      line = L"☑ " + trimmed.substr(6);
    else if (trimmed.starts_with(L"- ") || trimmed.starts_with(L"* ") ||
             trimmed.starts_with(L"+ "))
      line = L"• " + trimmed.substr(2);
    else if (trimmed == L"---" || trimmed == L"***" || trimmed == L"___")
      line = L"────────────────────";
    if (trimmed.find(L'|') != trimmed.npos) {
      const bool delimiter =
          trimmed.find(L'-') != trimmed.npos &&
          trimmed.find_first_not_of(L" |:-\t") == trimmed.npos;
      if (delimiter) {
        for (std::size_t i = result.empty() ? 0 : result.size() - 1; i > 0;) {
          --i;
          if (result[i].text == L"\r") break;
          result[i].bold = true;
        }
        continue;
      }
      // Native paragraph tab stops form columns; they remain selectable text.
      line = trimmed;
      if (line.starts_with(L"|")) line.erase(0, 1);
      if (line.ends_with(L"|")) line.pop_back();
      std::replace(line.begin(), line.end(), L'|', L'\t');
    }
    Inline(result, line, style);
    style.text = L"\r";
    result.push_back(style);
  }
  return result;
}

struct MessageView::Impl {
  struct Panel {
    Impl* view;
    MessageItem item;
    HWND hwnd = nullptr, body = nullptr;
    std::vector<HWND> buttons;
    std::vector<std::pair<CHARRANGE, std::wstring>> links;
    HBITMAP image = nullptr;
    int text_height = 20, height = 100, image_height = 0, image_width = 0;
    std::string rendered;
    ~Panel() {
      if (hwnd) DestroyWindow(hwnd);
      if (image) DeleteObject(image);
    }
  };
  HWND hwnd = nullptr;
  std::vector<std::unique_ptr<Panel>> panels;
  std::function<void(const MessageItem&, MessageAction)> action;
  std::string conversation;
  ui::ThemeId theme = ui::ThemeId::kNight;
  unsigned dpi = 96;
  int scroll = 0, total = 0, wheel = 0;
  bool layout = false;
  HFONT font = nullptr;
  int S(int x) const { return MulDiv(x, dpi, 96); }
  static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
  static LRESULT CALLBACK PanelProc(HWND, UINT, WPARAM, LPARAM);
  static LRESULT CALLBACK BodyProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR,
                                   DWORD_PTR);
  void Layout();
  void Scroll(int to);
  void Format(Panel& p);
  void Draw(Panel& p, HDC dc);
  void Print(HDC dc);
};
void MessageView::Impl::Format(Panel& p) {
  CHARRANGE selection{};
  SendMessageW(p.body, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
  const auto colors = appearance::Palette(theme, true);
  const auto bg = p.item.role == "user" ? colors.sidebar : colors.background;
  SendMessageW(p.body, WM_SETREDRAW, FALSE, 0);
  SendMessageW(p.body, EM_SETBKGNDCOLOR, 0, Color(bg));
  SetWindowTextW(p.body, L"");
  p.links.clear();
  std::string source = p.item.body;
  if (p.item.format == "json") {
    auto json = Json::parse(source, nullptr, false);
    if (!json.is_discarded()) source = json.dump(2);
  }
  LONG offset = 0;
  for (auto span : FormatMessage(source, p.item.format == "markdown")) {
    CHARRANGE end{-1, -1};
    SendMessageW(p.body, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&end));
    CHARFORMAT2W f{sizeof(f)};
    f.dwMask = CFM_COLOR | CFM_SIZE | CFM_BOLD | CFM_ITALIC | CFM_FACE |
               CFM_BACKCOLOR | CFM_LINK;
    f.dwEffects = (span.bold ? CFE_BOLD : 0) | (span.italic ? CFE_ITALIC : 0) |
                  (!span.link.empty() ? CFE_LINK : 0);
    f.yHeight = span.heading ? (span.heading == 1 ? 210 : 195)
                : span.code  ? 173
                             : 180;
    f.crTextColor = Color(!span.link.empty() ? colors.accent
                          : span.quote       ? colors.secondary
                                             : colors.text);
    f.crBackColor = Color(span.code ? colors.selection : bg);
    wcscpy_s(f.szFaceName,
             span.code ? L"Cascadia Mono" : L"Microsoft YaHei UI");
    SendMessageW(p.body, EM_REPLACESEL, FALSE,
                 reinterpret_cast<LPARAM>(span.text.c_str()));
    CHARRANGE position{};
    SendMessageW(p.body, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&position));
    auto length = position.cpMax;
    CHARRANGE range{offset, length};
    SendMessageW(p.body, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
    SendMessageW(p.body, EM_SETCHARFORMAT, SCF_SELECTION,
                 reinterpret_cast<LPARAM>(&f));
    if (!span.link.empty()) p.links.push_back({{offset, length}, span.link});
    offset = length;
  }
  CHARRANGE all{0, -1};
  SendMessageW(p.body, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&all));
  PARAFORMAT2 pf{sizeof(pf)};
  pf.dwMask = PFM_SPACEAFTER | PFM_LINESPACING | PFM_TABSTOPS;
  pf.dySpaceAfter = 60;
  pf.bLineSpacingRule = 4;
  pf.dyLineSpacing = 270;
  pf.cTabCount = 12;
  for (int i = 0; i < 12; ++i) pf.rgxTabs[i] = (i + 1) * 1500;
  SendMessageW(p.body, EM_SETPARAFORMAT, 0, reinterpret_cast<LPARAM>(&pf));
  SendMessageW(p.body, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection));
  SendMessageW(p.body, WM_SETREDRAW, TRUE, 0);
  p.rendered = p.item.body;
}
void MessageView::Impl::Layout() {
  if (layout) return;
  layout = true;
  RECT r{};
  GetClientRect(hwnd, &r);
  const int width = (std::max)(S(80), static_cast<int>(r.right));
  int y = 0;
  for (auto& p : panels) {
    SetWindowPos(p->body, nullptr, S(16), S(44), width - S(32), p->text_height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    RECT text{0, 0, width - S(32), 1000000};
    SendMessageW(p->body, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&text));
    GETTEXTLENGTHEX length{GTL_NUMCHARS | GTL_PRECISE, 1200};
    const auto count = SendMessageW(p->body, EM_GETTEXTLENGTHEX,
                                    reinterpret_cast<WPARAM>(&length), 0);
    POINTL first{}, last{};
    SendMessageW(p->body, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&first), 0);
    SendMessageW(p->body, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&last),
                 (std::max)(LRESULT{0}, count - 1));
    p->text_height =
        std::clamp(static_cast<int>(last.y - first.y) + S(22), S(22), 1000000);
    SetWindowPos(p->body, nullptr, S(16), S(44), width - S(32), p->text_height,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    p->image_height = p->image_width = 0;
    if (p->image) {
      BITMAP b{};
      GetObjectW(p->image, sizeof(b), &b);
      p->image_width =
          (std::min)({width - S(32), S(300), static_cast<int>(b.bmWidth)});
      p->image_height = MulDiv(p->image_width, b.bmHeight, b.bmWidth);
      if (p->image_height > S(240)) {
        p->image_width = MulDiv(p->image_width, S(240), p->image_height);
        p->image_height = S(240);
      }
    }
    const int actions_y = S(44) + p->text_height +
                          (p->image ? p->image_height + S(12) : 0) + S(12);
    int x = S(16), row = 0;
    for (auto button : p->buttons) {
      const int bw =
          S(GetDlgCtrlID(button) == 100 + static_cast<int>(MessageAction::Stage)
                ? 98
                : 78);
      if (x + bw > width - S(16)) {
        x = S(16);
        row += S(32);
      }
      SetWindowPos(button, nullptr, x, actions_y + row, bw, S(28),
                   SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
      x += bw + S(6);
    }
    p->height = actions_y + (p->buttons.empty() ? 0 : row + S(28)) + S(16);
    SetWindowPos(p->hwnd, nullptr, 0, y - scroll, width, p->height,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    y += p->height + S(12);
  }
  total = y;
  SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS};
  si.nMin = 0;
  si.nMax = (std::max)(0, total - 1);
  si.nPage = r.bottom;
  si.nPos = scroll;
  SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
  auto position = GetScrollPos(hwnd, SB_VERT);
  if (position != scroll) {
    scroll = position;
    layout = false;
    Layout();
    return;
  }
  layout = false;
  InvalidateRect(hwnd, nullptr, TRUE);
}
void MessageView::Impl::Scroll(int to) {
  RECT r{};
  GetClientRect(hwnd, &r);
  const auto next =
      std::clamp(to, 0, (std::max)(0, total - static_cast<int>(r.bottom)));
  if (next == scroll) return;
  scroll = next;
  Layout();
}
void MessageView::Impl::Draw(Panel& p, HDC dc) {
  appearance::Paint paint(dc, dpi);
  auto c = appearance::Palette(theme, true);
  RECT r{};
  GetClientRect(p.hwnd, &r);
  paint.Fill(r, p.item.role == "user" ? c.sidebar : c.background);
  const auto role = p.item.role == "user"    ? L"你"
                    : p.item.role == "note"  ? L"本地备注"
                    : p.item.role == "inbox" ? L"收件"
                    : p.item.role == "empty" ? L"Mailbox"
                                             : L"助手";
  paint.Text(role, paint.R(16, 12, 100, 22), 12, c.text, FW_SEMIBOLD);
  auto meta =
      workbench::Wide(p.item.time + (p.item.streaming ? " · 生成中" : ""));
  auto right = r;
  right.left = S(110);
  right.right -= S(16);
  right.top = S(12);
  right.bottom = S(34);
  paint.Text(meta, right, 10, c.secondary, FW_NORMAL,
             DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
  if (p.image) {
    auto source = CreateCompatibleDC(dc);
    auto old = SelectObject(source, p.image);
    BITMAP b{};
    GetObjectW(p.image, sizeof(b), &b);
    SetStretchBltMode(dc, HALFTONE);
    StretchBlt(dc, S(16), S(56) + p.text_height, p.image_width, p.image_height,
               source, 0, 0, b.bmWidth, b.bmHeight, SRCCOPY);
    SelectObject(source, old);
    DeleteDC(source);
  }
}
void MessageView::Impl::Print(HDC dc) {
  auto c = appearance::Palette(theme, true);
  RECT r{};
  GetClientRect(hwnd, &r);
  appearance::Paint(dc, dpi).Fill(r, c.background);
  int y = -scroll;
  for (auto& p : panels) {
    if (y + p->height > 0 && y < r.bottom) {
      const auto saved = SaveDC(dc);
      IntersectClipRect(dc, 0, 0, r.right, r.bottom);
      OffsetViewportOrgEx(dc, 0, y, nullptr);
      SendMessageW(p->hwnd, WM_PRINT, reinterpret_cast<WPARAM>(dc),
                   PRF_CLIENT | PRF_CHILDREN);
      RestoreDC(dc, saved);
    }
    y += p->height + S(12);
  }
}
LRESULT CALLBACK MessageView::Impl::BodyProc(HWND hwnd, UINT msg, WPARAM wp,
                                             LPARAM lp, UINT_PTR,
                                             DWORD_PTR data) {
  auto* panel = reinterpret_cast<Panel*>(data);
  if (msg == WM_MOUSEWHEEL) return SendMessageW(panel->view->hwnd, msg, wp, lp);
  if (msg == WM_MOUSEMOVE && GetDlgCtrlID(hwnd) >= 100) {
    TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
    TrackMouseEvent(&track);
    InvalidateRect(hwnd, nullptr, TRUE);
  }
  if (msg == WM_MOUSELEAVE) InvalidateRect(hwnd, nullptr, TRUE);
  if (msg == WM_SETCURSOR && GetDlgCtrlID(hwnd) >= 100) {
    SetCursor(LoadCursorW(nullptr, IDC_HAND));
    return TRUE;
  }
  if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, BodyProc, 1);
  return DefSubclassProc(hwnd, msg, wp, lp);
}
LRESULT CALLBACK MessageView::Impl::PanelProc(HWND hwnd, UINT msg, WPARAM wp,
                                              LPARAM lp) {
  auto* p = reinterpret_cast<Panel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    p = static_cast<Panel*>(
        reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p));
  }
  if (!p) return DefWindowProcW(hwnd, msg, wp, lp);
  auto& v = *p->view;
  if (msg == WM_ERASEBKGND) return 1;
  if (msg == WM_PAINT || msg == WM_PRINTCLIENT) {
    PAINTSTRUCT ps{};
    auto dc =
        msg == WM_PAINT ? BeginPaint(hwnd, &ps) : reinterpret_cast<HDC>(wp);
    v.Draw(*p, dc);
    if (msg == WM_PAINT) EndPaint(hwnd, &ps);
    return 0;
  }
  if (msg == WM_MOUSEWHEEL) return SendMessageW(v.hwnd, msg, wp, lp);
  if (msg == WM_COMMAND && HIWORD(wp) == BN_CLICKED && LOWORD(wp) >= 100) {
    v.action(p->item, static_cast<MessageAction>(LOWORD(wp) - 100));
    return 0;
  }
  if (msg == WM_DRAWITEM) {
    auto* d = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
    auto c = appearance::Palette(v.theme, true);
    appearance::Paint paint(d->hDC, v.dpi);
    paint.Fill(d->rcItem, p->item.role == "user" ? c.sidebar : c.background);
    POINT cursor{};
    GetCursorPos(&cursor);
    ScreenToClient(d->hwndItem, &cursor);
    const bool hover = PtInRect(&d->rcItem, cursor) != FALSE;
    paint.Fill(d->rcItem,
               d->itemState & ODS_SELECTED || hover ? c.selection : c.field, 6);
    paint.Stroke(d->rcItem, d->itemState & ODS_FOCUS ? c.accent : c.border, 6);
    wchar_t label[64]{};
    GetWindowTextW(d->hwndItem, label, 64);
    paint.Text(label, d->rcItem, 11, c.text, FW_NORMAL,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return TRUE;
  }
  if (msg == WM_NOTIFY) {
    auto* n = reinterpret_cast<NMHDR*>(lp);
    if (n->code == EN_REQUESTRESIZE) {
      p->text_height = reinterpret_cast<REQRESIZE*>(lp)->rc.bottom;
      return 0;
    }
    if (n->code == EN_LINK) {
      auto* link = reinterpret_cast<ENLINK*>(lp);
      if (link->msg == WM_LBUTTONUP)
        for (const auto& [range, url] : p->links)
          if (range.cpMin <= link->chrg.cpMin &&
              range.cpMax >= link->chrg.cpMax && SafeLink(url)) {
            ShellExecuteW(hwnd, L"open", url.c_str(), nullptr, nullptr,
                          SW_SHOWNORMAL);
            break;
          }
      return 1;
    }
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}
LRESULT CALLBACK MessageView::Impl::Proc(HWND hwnd, UINT msg, WPARAM wp,
                                         LPARAM lp) {
  auto* v = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    v = static_cast<Impl*>(
        reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    v->hwnd = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(v));
  }
  if (!v) return DefWindowProcW(hwnd, msg, wp, lp);
  if (msg == WM_SIZE) {
    v->Layout();
    return 0;
  }
  if (msg == WM_ERASEBKGND) return 1;
  if (msg == WM_PRINT || msg == WM_PRINTCLIENT) {
    v->Print(reinterpret_cast<HDC>(wp));
    return 0;
  }
  if (msg == WM_PAINT) {
    PAINTSTRUCT ps{};
    auto dc = BeginPaint(hwnd, &ps);
    RECT r{};
    GetClientRect(hwnd, &r);
    appearance::Paint(dc, v->dpi)
        .Fill(r, appearance::Palette(v->theme, true).background);
    EndPaint(hwnd, &ps);
    return 0;
  }
  if (msg == WM_MOUSEWHEEL) {
    v->wheel += GET_WHEEL_DELTA_WPARAM(wp);
    const int lines = v->wheel / WHEEL_DELTA;
    v->wheel %= WHEEL_DELTA;
    v->Scroll(v->scroll - lines * v->S(54));
    return 0;
  }
  if (msg == WM_VSCROLL) {
    SCROLLINFO si{sizeof(si), SIF_ALL};
    GetScrollInfo(hwnd, SB_VERT, &si);
    int to = v->scroll;
    switch (LOWORD(wp)) {
      case SB_LINEUP:
        to -= v->S(18);
        break;
      case SB_LINEDOWN:
        to += v->S(18);
        break;
      case SB_PAGEUP:
        to -= si.nPage;
        break;
      case SB_PAGEDOWN:
        to += si.nPage;
        break;
      case SB_THUMBTRACK:
      case SB_THUMBPOSITION:
        to = si.nTrackPos;
        break;
      case SB_TOP:
        to = 0;
        break;
      case SB_BOTTOM:
        to = v->total;
        break;
    }
    v->Scroll(to);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}
MessageView::MessageView(
    HWND parent, int id,
    std::function<void(const MessageItem&, MessageAction)> action)
    : impl_(std::make_unique<Impl>()) {
  impl_->action = std::move(action);
  WNDCLASSW wc{};
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpfnWndProc = Impl::Proc;
  wc.lpszClassName = L"RIMES.MessageView";
  RegisterClassW(&wc);
  wc.lpfnWndProc = Impl::PanelProc;
  wc.lpszClassName = L"RIMES.MessagePanel";
  RegisterClassW(&wc);
  CreateWindowExW(WS_EX_CONTROLPARENT, L"RIMES.MessageView", L"消息",
                  WS_CHILD | WS_CLIPCHILDREN | WS_VSCROLL, 0, 0, 1, 1, parent,
                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                  wc.hInstance, impl_.get());
}
MessageView::~MessageView() {
  impl_->panels.clear();
  if (impl_->hwnd) DestroyWindow(impl_->hwnd);
  if (impl_->font) DeleteObject(impl_->font);
}
HWND MessageView::Handle() const { return impl_->hwnd; }
void MessageView::Update(const std::string& conversation,
                         std::vector<MessageItem> items, ui::ThemeId theme,
                         unsigned dpi) {
  auto& v = *impl_;
  RECT r{};
  GetClientRect(v.hwnd, &r);
  bool selection = false;
  for (auto& p : v.panels) {
    CHARRANGE range{};
    SendMessageW(p->body, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&range));
    selection |= range.cpMin != range.cpMax;
  }
  const bool same = conversation == v.conversation;
  const bool follow =
      !same || (!selection && v.scroll + r.bottom >= v.total - v.S(90));
  const bool changed_style = theme != v.theme || dpi != v.dpi;
  v.theme = theme;
  v.dpi = dpi;
  if (!v.font || changed_style) {
    if (v.font) DeleteObject(v.font);
    v.font =
        CreateFontW(-v.S(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
  }
  if (!same) {
    v.panels.clear();
    v.scroll = 0;
  }
  v.conversation = conversation;
  for (std::size_t i = 0; i < items.size(); ++i) {
    bool fresh = i >= v.panels.size() || v.panels[i]->item.id != items[i].id ||
                 v.panels[i]->item.review != items[i].review ||
                 v.panels[i]->item.streaming != items[i].streaming;
    if (fresh) {
      if (i < v.panels.size()) v.panels.resize(i);
      auto p = std::make_unique<Impl::Panel>();
      p->view = &v;
      p->item = items[i];
      p->hwnd = CreateWindowExW(WS_EX_CONTROLPARENT, L"RIMES.MessagePanel",
                                L"消息", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 1, 1,
                                v.hwnd, reinterpret_cast<HMENU>(i + 1000),
                                GetModuleHandleW(nullptr), p.get());
      p->body = CreateWindowExW(
          0, MSFTEDIT_CLASS, L"",
          WS_CHILD | WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_NOHIDESEL, 0,
          0, 1, 1, p->hwnd, reinterpret_cast<HMENU>(1),
          GetModuleHandleW(nullptr), nullptr);
      SendMessageW(p->body, EM_EXLIMITTEXT, 0, 256 * 1024);
      SendMessageW(p->body, EM_SETEVENTMASK, 0, ENM_LINK);
      SetWindowSubclass(p->body, Impl::BodyProc, 1,
                        reinterpret_cast<DWORD_PTR>(p.get()));
      if (!p->item.image.empty()) p->image = Thumbnail(p->item.image, 900);
      const std::array<std::pair<MessageAction, const wchar_t*>, 6> actions{
          {{MessageAction::Copy, L"复制"},
           {MessageAction::Stage, L"加入 Buffer"},
           {MessageAction::Save, L"存入 Capsule"},
           {MessageAction::OpenImage, L"打开图片"},
           {MessageAction::SaveImage, L"保存图片"},
           {MessageAction::Ignore, L"忽略"}}};
      if (!p->item.streaming &&
          (p->item.role == "assistant" || p->item.role == "inbox"))
        for (auto [act, label] : actions) {
          if ((act == MessageAction::OpenImage ||
               act == MessageAction::SaveImage) &&
              !p->image)
            continue;
          if (act == MessageAction::Ignore && !p->item.review) continue;
          if ((act == MessageAction::Stage || act == MessageAction::Save ||
               act == MessageAction::Copy) &&
              p->item.body.empty())
            continue;
          auto button = CreateWindowExW(
              0, L"BUTTON", label, WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 0, 0,
              1, 1, p->hwnd,
              reinterpret_cast<HMENU>(100 + static_cast<INT_PTR>(act)),
              GetModuleHandleW(nullptr), nullptr);
          p->buttons.push_back(button);
          SetWindowSubclass(button, Impl::BodyProc, 1,
                            reinterpret_cast<DWORD_PTR>(p.get()));
        }
      v.panels.push_back(std::move(p));
    }
    auto& p = *v.panels[i];
    p.item = std::move(items[i]);
    if (fresh || changed_style || p.rendered != p.item.body) v.Format(p);
    for (auto b : p.buttons)
      SendMessageW(b, WM_SETFONT, reinterpret_cast<WPARAM>(v.font), TRUE);
  }
  if (v.panels.size() > items.size()) v.panels.resize(items.size());
  v.Layout();
  if (follow)
    v.Scroll(same || (!items.empty() && items.front().role != "inbox") ? v.total
                                                                       : 0);
  RedrawWindow(v.hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}
}  // namespace rimes::windows::peripherals
