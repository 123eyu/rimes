#include <Windows.h>

#include <d2d1.h>
#include <dwrite.h>

#include <memory>
#include <string>

#include "../tsf/CandidateWindow.h"
#include "../tsf/ModuleState.h"
#include "../ui/buffer_layout.hpp"
#include "../ui/buffer_paint.hpp"
#include "../ui/theme.hpp"
#include "../workbench/settings_ui.hpp"
#include "export.hpp"

namespace {

constexpr UINT kPaintTimer = 1;

struct PreviewState {
  rimes::windows::ui::ThemeId theme = rimes::windows::ui::ThemeId::kNight;
  int scenario = 0;
  unsigned font_size = 16;
  unsigned dpi_sim = 96;
  ID2D1Factory* factory = nullptr;
  IDWriteFactory* write = nullptr;
  ID2D1HwndRenderTarget* render = nullptr;
  ID2D1SolidColorBrush* brush = nullptr;
  IDWriteTextFormat* body = nullptr;
  IDWriteTextFormat* label = nullptr;
  rimes::windows::tsf::CandidateWindow candidate;
  std::unique_ptr<rimes::windows::workbench::SettingsUiHost> settings;
  rimes::windows::workbench::Settings fixture_settings{};
  HWND window = nullptr;
};

const wchar_t* ScenarioName(int scenario) {
  switch (scenario) {
    case 0:
      return L"input";
    case 1:
      return L"generation";
    case 2:
      return L"waiting";
    case 3:
      return L"translation";
    default:
      return L"error";
  }
}

rimes::windows::ui::BufferPaintState MakeBuffer(PreviewState* self) {
  using namespace rimes::windows::ui;
  BufferPaintState state;
  state.theme = self->theme;
  state.bound = true;
  state.capturing = true;
  state.mode = BufferMode::kInput;
  state.empty_hint = L"等待输入";
  switch (self->scenario) {
    case 0:
      state.source_blocks = {{L"你好。", false}, {L"世界", false}};
      state.preedit = L"shijie";
      state.status = L"已绑定";
      break;
    case 1:
      state.mode = BufferMode::kGenerate;
      state.source_blocks = {{L"Explain DIP scaling.", false}};
      state.result_blocks = {
          {L"DIP keeps layout stable across monitors.", false}};
      state.status = L"结果就绪";
      break;
    case 2:
      state.mode = BufferMode::kGenerate;
      state.busy = true;
      state.source_blocks = {{L"Summarize the theme tokens.", false}};
      state.preview = L"Themes share semantic…";
      state.status = L"接收中…";
      break;
    case 3:
      state.mode = BufferMode::kTranslate;
      state.translate = true;
      state.source_blocks = {{L"紧凑候选条", false}};
      state.result_blocks = {{L"compact candidate strip", false}};
      state.status = L"翻译就绪";
      break;
    default:
      state.source_blocks = {{L"Keep source on failure.", false}};
      state.status = L"API 失败，原文保留";
      break;
  }
  return state;
}

void UpdateCandidate(PreviewState* self) {
  using namespace rimes::windows::tsf;
  if (self->settings && self->settings->IsOpen()) {
    self->candidate.Hide();
    return;
  }
  CandidateSnapshot snap;
  snap.visible = true;
  snap.highlighted = 1;
  snap.composition = L"nihao";
  POINT caret{24, 320};
  ClientToScreen(self->window, &caret);
  snap.caret_rect = {caret.x, caret.y, caret.x, caret.y + 24};
  snap.items = {{L"1", L"你好", L"greeting"}, {L"2", L"泥壕", L""},
                {L"3", L"倪浩", L"name"},     {L"4", L"逆号", L""},
                {L"5", L"匿好", L""},         {L"6", L"昵毫", L""},
                {L"7", L"尼皓", L""},         {L"8", L"你号", L""},
                {L"9", L"拟好", L""}};
  self->candidate.SetFont(self->font_size);
  self->candidate.SetTheme(self->theme);
  self->candidate.Update(snap);
}

void ResizeForDpi(PreviewState* self);
void OpenSettingsFixture(PreviewState* self);

void PreviewAction(PreviewState* self, int index) {
  if (index == 0) {
    self->theme = static_cast<rimes::windows::ui::ThemeId>(
        (static_cast<int>(self->theme) + 1) % 4);
  } else if (index == 1) self->scenario = (self->scenario + 1) % 5;
  else if (index == 2) {
    self->dpi_sim = self->dpi_sim == 96 ? 144 :
                    self->dpi_sim == 144 ? 192 : 96;
    ResizeForDpi(self);
  } else if (index == 3) OpenSettingsFixture(self);
  else if (index == 4) {
    self->font_size = self->font_size == 16 ? 24 :
                      self->font_size == 24 ? 40 : 16;
  }
  UpdateCandidate(self);
  InvalidateRect(self->window, nullptr, FALSE);
}

void EnsureRender(PreviewState* self, int width_px, int height_px) {
  if (self->render) {
    self->render->Resize(D2D1::SizeU(width_px, height_px));
    self->render->SetDpi(static_cast<float>(self->dpi_sim),
                         static_cast<float>(self->dpi_sim));
    return;
  }
  self->factory->CreateHwndRenderTarget(
      D2D1::RenderTargetProperties(),
      D2D1::HwndRenderTargetProperties(self->window,
                                       D2D1::SizeU(width_px, height_px)),
      &self->render);
  if (self->render) {
    self->render->SetDpi(static_cast<float>(self->dpi_sim),
                         static_cast<float>(self->dpi_sim));
    self->render->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &self->brush);
  }
}

void Paint(PreviewState* self) {
  PAINTSTRUCT ps{};
  BeginPaint(self->window, &ps);
  RECT client{};
  GetClientRect(self->window, &client);
  EnsureRender(self, client.right, client.bottom);
  if (self->render && self->brush && self->body && self->label) {
    auto buffer = MakeBuffer(self);
    auto layout = rimes::windows::ui::LayoutBuffer(buffer, 760);
    self->render->BeginDraw();
    self->render->Clear(rimes::windows::ui::ColorF(
        rimes::windows::ui::Palette(self->theme).settings_background));
    // Translate into DIP space so 760-DIP Buffer stays fully visible under DPI
    // simulation (canvas host size scales with dpi_sim).
    self->render->SetTransform(D2D1::Matrix3x2F::Translation(24, 160));
    rimes::windows::ui::DrawBufferWorkbench(
        rimes::windows::ui::BufferPaintContext{
            self->render, self->write, self->body, self->label, self->brush,
            static_cast<float>(self->dpi_sim)},
        buffer, layout);
    self->render->SetTransform(D2D1::Matrix3x2F::Identity());
    self->brush->SetColor(rimes::windows::ui::ColorF(
        rimes::windows::ui::Palette(self->theme).text_primary));
    const auto tip =
        std::wstring(L"1-4 theme  5-9 scenario  0 DPI  S settings  Esc quit  | ") +
        ScenarioName(self->scenario) + L" @ " +
        std::to_wstring(self->dpi_sim) + L" DPI";
    self->render->DrawTextW(
        tip.c_str(), static_cast<UINT32>(tip.size()), self->label,
        D2D1::RectF(24, 24, 900, 48), self->brush,
        D2D1_DRAW_TEXT_OPTIONS_NONE);
    const wchar_t* actions[] = {L"下一主题", L"下一状态", L"DPI", L"打开设置", L"候选字号"};
    for (int i = 0; i < 5; ++i) {
      const float x = 24 + static_cast<float>(i) * 110;
      self->brush->SetColor(rimes::windows::ui::ColorF(
          rimes::windows::ui::Palette(self->theme).surface_secondary));
      self->render->FillRoundedRectangle(D2D1::RoundedRect(
          D2D1::RectF(x, 58, x + 100, 86), 6, 6), self->brush);
      self->brush->SetColor(rimes::windows::ui::ColorF(
          rimes::windows::ui::Palette(self->theme).text_primary));
      self->render->DrawTextW(actions[i], static_cast<UINT32>(wcslen(actions[i])),
          self->label, D2D1::RectF(x + 8, 64, x + 92, 82), self->brush);
    }
    self->render->EndDraw();
  }
  EndPaint(self->window, &ps);
}

void OpenSettingsFixture(PreviewState* self) {
  using namespace rimes::windows::workbench;
  if (!self->settings) {
    SettingsUiCallbacks cb;
    cb.load = [self] {
      self->fixture_settings.font_size = self->font_size;
      return self->fixture_settings;
    };
    cb.save = [self](Settings value, const std::wstring& key, bool,
                     std::string* error) {
      (void)key;
      self->fixture_settings = value;
      self->font_size = value.font_size;
      if (error) error->clear();
      return true;
    };
    cb.load_theme = [self] { return self->theme; };
    cb.on_theme_preview = [self](rimes::windows::ui::ThemeId theme) {
      self->theme = theme;
      UpdateCandidate(self);
      InvalidateRect(self->window, nullptr, FALSE);
    };
    cb.about_text =
        L"RimesVisualPreview fixture\nNo Runtime / secrets / network.";
    self->settings = std::make_unique<SettingsUiHost>(std::move(cb));
  }
  self->settings->Open(self->window);
}

void ResizeForDpi(PreviewState* self) {
  // Keep a 980x720 DIP canvas so 760-DIP Buffer + chrome remain fully visible.
  const int w = MulDiv(980, static_cast<int>(self->dpi_sim), 96);
  const int h = MulDiv(720, static_cast<int>(self->dpi_sim), 96);
  RECT outer{0, 0, w, h};
  AdjustWindowRectEx(&outer, WS_OVERLAPPEDWINDOW, FALSE, 0);
  SetWindowPos(self->window, nullptr, 0, 0, outer.right - outer.left,
               outer.bottom - outer.top, SWP_NOMOVE | SWP_NOZORDER);
  if (self->render) {
    self->render->Release();
    self->render = nullptr;
  }
  if (self->brush) {
    self->brush->Release();
    self->brush = nullptr;
  }
}

LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  auto* self =
      reinterpret_cast<PreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    self = static_cast<PreviewState*>(
        reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->window = hwnd;
  }
  if (!self) return DefWindowProcW(hwnd, msg, wparam, lparam);
  switch (msg) {
    case WM_PAINT:
      Paint(self);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_TIMER:
      UpdateCandidate(self);
      return 0;
    case WM_LBUTTONUP: {
      const float x = static_cast<float>(static_cast<short>(LOWORD(lparam))) *
                      96.0f / static_cast<float>(self->dpi_sim);
      const float y = static_cast<float>(static_cast<short>(HIWORD(lparam))) *
                      96.0f / static_cast<float>(self->dpi_sim);
      if (y >= 58 && y < 86 && x >= 24) {
        const int index = static_cast<int>((x - 24) / 110);
        if (index < 5 && x < 24 + static_cast<float>(index) * 110 + 100)
          PreviewAction(self, index);
      }
      return 0;
    }
    case WM_KEYDOWN:
      if (wparam >= '1' && wparam <= '4') {
        self->theme = static_cast<rimes::windows::ui::ThemeId>(wparam - '1');
        UpdateCandidate(self);
        InvalidateRect(hwnd, nullptr, FALSE);
      } else if (wparam >= '5' && wparam <= '9') {
        self->scenario = static_cast<int>(wparam - '5');
        InvalidateRect(hwnd, nullptr, FALSE);
      } else if (wparam == '0') {
        self->dpi_sim =
            self->dpi_sim == 96 ? 144 : (self->dpi_sim == 144 ? 192 : 96);
        ResizeForDpi(self);
        InvalidateRect(hwnd, nullptr, FALSE);
      } else if (wparam == 'S' || wparam == 's') {
        OpenSettingsFixture(self);
      } else if (wparam == VK_ESCAPE) {
        DestroyWindow(hwnd);
      }
      return 0;
    case WM_SIZE:
      if (self->render)
        self->render->Resize(D2D1::SizeU(LOWORD(lparam), HIWORD(lparam)));
      return 0;
    case WM_DESTROY:
      KillTimer(hwnd, kPaintTimer);
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command, int show) {
  const std::wstring arguments(command ? command : L"");
  if (arguments.starts_with(L"--export=")) {
    auto directory = arguments.substr(9);
    if (directory.size() > 1 && directory.front() == L'"' && directory.back() == L'"') directory = directory.substr(1, directory.size() - 2);
    return parity_render::Export(directory);
  }
  // Developer visual fixture: production Buffer painter + SettingsUiHost +
  // production CandidateWindow. No IME registration, Broker, clipboard,
  // secrets, network, or real user settings.json writes.
  SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  rimes::windows::tsf::module::SetInstance(instance);
  PreviewState state;
  state.fixture_settings.font_size = 16;
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                               &state.factory)) ||
      FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                 __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(&state.write)))) {
    return 1;
  }
  state.write->CreateTextFormat(L"Microsoft YaHei UI", nullptr,
                                DWRITE_FONT_WEIGHT_NORMAL,
                                DWRITE_FONT_STYLE_NORMAL,
                                DWRITE_FONT_STRETCH_NORMAL, 12, L"zh-CN",
                                &state.body);
  state.write->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                DWRITE_FONT_STYLE_NORMAL,
                                DWRITE_FONT_STRETCH_NORMAL, 10, L"zh-CN",
                                &state.label);
  if (state.body) state.body->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
  if (state.label) state.label->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

  WNDCLASSW wc{};
  wc.lpfnWndProc = Proc;
  wc.hInstance = instance;
  wc.lpszClassName = L"Rimes.VisualPreview";
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  RegisterClassW(&wc);
  HWND window =
      CreateWindowExW(0, wc.lpszClassName, L"RimesVisualPreview — 墨竹",
                      WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1000,
                      760, nullptr, nullptr, instance, &state);
  if (!window) return 1;
  ShowWindow(window, show);
  UpdateCandidate(&state);
  SetTimer(window, kPaintTimer, 500, nullptr);
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (state.settings && state.settings->HandleDialogMessage(&msg)) continue;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  state.candidate.Hide();
  state.settings.reset();
  if (state.body) state.body->Release();
  if (state.label) state.label->Release();
  if (state.brush) state.brush->Release();
  if (state.render) state.render->Release();
  if (state.write) state.write->Release();
  if (state.factory) state.factory->Release();
  return static_cast<int>(msg.wParam);
}
