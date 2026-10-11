#include <Windows.h>

#include <cstdlib>
#include <iostream>

#include "CandidateWindow.h"
#include "Guids.h"

namespace {
void Check(bool value, const char* description) {
  if (!value) {
    std::cerr << "FAIL: " << description << '\n';
    std::exit(1);
  }
}
}  // namespace

int main() {
  using namespace rimes::windows::tsf;
  SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  const HWND foreground = GetForegroundWindow();
  CandidateWindow candidate;
  int selected = 0;
  std::size_t expected_index = 0;
  candidate.SetSelect([&](std::size_t index) {
    Check(index == expected_index, "mouse selects the visible logical item");
    ++selected;
  });
  CandidateSnapshot snapshot;
  snapshot.visible = true;
  snapshot.composition = L"nihao";
  snapshot.caret_rect = {100, 100, 100, 124};
  snapshot.items = {{L"1", L"你好", L""}};
  candidate.Update(snapshot);
  HWND popup = FindWindowW(kCandidateWindowClass, nullptr);
  DWORD process = 0;
  if (popup) GetWindowThreadProcessId(popup, &process);
  Check(popup && process == GetCurrentProcessId(), "test owns its isolated popup");
  Check(GetForegroundWindow() == foreground, "candidate does not activate the host");
  const unsigned dpi = GetDpiForWindow(popup);
  const auto px = [dpi](int dip) { return MulDiv(dip, static_cast<int>(dpi), 96); };
  RECT initial_rect{};
  GetWindowRect(popup, &initial_rect);
  Check(initial_rect.bottom - initial_rect.top == px(34),
        "composition does not add a row above candidates");
  snapshot.composition.clear();
  candidate.Update(snapshot);
  RECT no_composition_rect{};
  GetWindowRect(popup, &no_composition_rect);
  Check(EqualRect(&initial_rect, &no_composition_rect),
        "popup geometry is independent of inline composition");
  snapshot.composition = std::wstring(200, L'n');
  candidate.Update(snapshot);
  RECT long_composition_rect{};
  GetWindowRect(popup, &long_composition_rect);
  Check(EqualRect(&initial_rect, &long_composition_rect),
        "long inline composition does not widen the candidate popup");
  HRGN region = CreateRectRgn(0, 0, 0, 0);
  Check(GetWindowRgn(popup, region) != ERROR, "rounded popup has an explicit region");
  Check(!PtInRegion(region, 0, 0), "rounded outer corner stays transparent");
  Check(PtInRegion(region, px(10), px(17)), "candidate occupies the only popup row");
  DeleteObject(region);
  const LPARAM click = MAKELPARAM(px(10), px(17));
  SendMessageW(popup, WM_LBUTTONUP, 0, click);
  Check(selected == 0, "late mouse release cannot select a candidate");
  SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, click);
  SendMessageW(popup, WM_LBUTTONUP, 0, click);
  Check(selected == 1, "owned down/up selects once");
  SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, click);
  snapshot.composition = L"ni";
  candidate.Update(snapshot);
  SendMessageW(popup, WM_LBUTTONUP, 0, click);
  Check(selected == 1, "composition change invalidates the old mouse press");
  SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, click);
  candidate.Hide();
  SendMessageW(popup, WM_LBUTTONUP, 0, click);
  Check(selected == 1, "hidden popup invalidates the old mouse press");
  Check(GetCapture() != popup, "hiding releases mouse capture");
  snapshot.items.assign(9, {L"1", L"候选词内容偏长", L""});
  candidate.Update(snapshot);
  RECT full_page_rect{};
  GetWindowRect(popup, &full_page_rect);
  Check(full_page_rect.bottom - full_page_rect.top == px(34),
        "full horizontal page keeps one native window row");
  snapshot.items.resize(2);
  candidate.Update(snapshot);
  candidate.SetVertical(true);
  RECT vertical_rect{};
  GetWindowRect(popup, &vertical_rect);
  Check(vertical_rect.bottom - vertical_rect.top == px(68),
        "vertical popup contains candidate rows only");
  snapshot.items.clear();
  candidate.Update(snapshot);
  Check(!IsWindowVisible(popup) && !candidate.snapshot().visible,
        "inline composition without candidates does not show a duplicate popup");
  candidate.SetVertical(false);
  snapshot.items.assign(5, {L"1", L"候选", L""});
  snapshot.matrix_rows = {3, 2};
  candidate.Update(snapshot);
  RECT matrix_rect{};
  GetWindowRect(popup, &matrix_rect);
  Check(matrix_rect.bottom - matrix_rect.top == px(71),
        "matrix adds only explicit candidate rows and their spacing");
  expected_index = 3;
  const LPARAM matrix_click = MAKELPARAM(px(10), px(54));
  SendMessageW(popup, WM_LBUTTONDOWN, MK_LBUTTON, matrix_click);
  SendMessageW(popup, WM_LBUTTONUP, 0, matrix_click);
  Check(selected == 2, "second matrix row keeps its flattened mouse index");
  Check(GetForegroundWindow() == foreground, "click ownership preserves no-activation");
  std::cout << "Candidate region, click ownership and no-activation passed\n";
}
