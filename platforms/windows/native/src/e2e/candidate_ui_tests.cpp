#include <iostream>
#include "TextService.h"
#include "fake_tsf.hpp"

namespace rimes::windows::tsf {
struct CandidateUIRegressionProbe {
  static InputModeIndicator* Mode(TextService& service) {
    service.input_mode_ = new InputModeIndicator([&service] { service.ToggleInputMode(); });
    return service.input_mode_;
  }
  static HRESULT Apply(TextService& service, ITfContext* context,
                       const BrokerInputState& state) {
    service.BindContext(context);
    return service.ApplyDocumentState(context, state);
  }
};
}

namespace {
class StubBroker final : public rimes::windows::tsf::BrokerClient {
 public:
  bool ascii = false;
  std::optional<bool> AsciiMode() const noexcept override { return ascii; }
  using State = rimes::windows::tsf::BrokerInputState;
  void BeginConnect() noexcept override {}
  void Disconnect() noexcept override {}
  bool SetContext(std::uint64_t) noexcept override { return true; }
  void SetNotificationWindow(HWND) noexcept override {}
  std::optional<rimes::windows::core::Json> TakeNotification() override { return {}; }
  bool Control(rimes::windows::core::Json) noexcept override { return true; }
  bool Capturing() const noexcept override { return false; }
  std::uint64_t ConnectionGeneration() const noexcept override { return 0; }
  bool IsConnected() const noexcept override { return true; }
  rimes::windows::tsf::BrokerKeyResult HandleKey(
      const rimes::windows::tsf::BrokerKeyEvent& event, State* state) noexcept override {
    using namespace rimes::windows::tsf;
    if (event.phase == BrokerKeyPhase::kInputModeToggle ||
        (event.phase == BrokerKeyPhase::kKeyUp && event.virtual_key == VK_SHIFT)) {
      ascii = !ascii;
      if (state) { *state = {}; state->has_snapshot = true; }
      return BrokerKeyResult::kPassThrough;
    }
    if (event.phase != BrokerKeyPhase::kKeyDown || event.virtual_key != '2')
      return BrokerKeyResult::kPassThrough;
    *state = {};
    state->has_snapshot = true;
    state->revision = 2;
    state->commit_text = L"拟好";
    return BrokerKeyResult::kConsumed;
  }
};
}

int main() {
  using namespace rimes::windows;
  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
  int failures = 0;
  auto expect = [&](bool ok, const char* name) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << name << '\n'; }
  };
  e2e::FakeDocument data;
  data.refuse_caret = true; // Windows Runtime edits need not expose an HWND/caret.
  auto* context = new e2e::FakeContext(&data);
  auto* document = new e2e::FakeDocumentMgr(context);
  auto* manager = new e2e::FakeThreadMgr;
  manager->ui_enabled = true;
  manager->SetFocus(document);
  auto* service = new tsf::TextService(std::make_unique<StubBroker>());
  expect(SUCCEEDED(service->ActivateEx(manager, 1, TF_TMAE_UIELEMENTENABLEDONLY)), "activate");
  auto* mode = tsf::CandidateUIRegressionProbe::Mode(*service);
  mode->AddRef(); // A shell may keep its item after the text service ends.
  TF_LANGBARITEMINFO info{};
  expect(mode->GetInfo(&info) == S_OK && info.guidItem == GUID_LBI_INPUTMODE &&
      (info.dwStyle & TF_LBI_STYLE_SHOWNINTRAY), "native input indicator identity");
  auto mode_text = [&] {
    BSTR text = nullptr;
    mode->GetText(&text);
    std::wstring result(text ? text : L""); SysFreeString(text); return result;
  };
  DWORD mode_status = 0;
  mode->GetStatus(&mode_status);
  expect(mode_status & TF_LBI_STATUS_DISABLED, "unknown mode starts disabled");
  tsf::CandidateUIRegressionProbe::Apply(*service, context, {});
  expect(mode_text() == L"中", "mode is obtained from the authoritative broker session");
  BOOL eaten = FALSE;
  service->OnKeyUp(context, VK_SHIFT, 0, &eaten);
  expect(mode_text() == L"英" && !eaten, "Shift updates indicator without eating host modifier");
  mode->OnClick(TF_LBI_CLK_LEFT, {}, nullptr);
  expect(mode_text() == L"中" && data.text.empty(), "indicator click changes mode through a validated edit without inserting text");
  tsf::BrokerInputState state;
  state.has_snapshot = state.composing = state.candidates_visible = true;
  state.composition = L"nihao";
  state.revision = 1;
  state.highlighted_candidate = 0;
  state.candidates = {{0, L"你好", L"", L"1"}, {1, L"拟好", L"", L"2"}};
  expect(SUCCEEDED(tsf::CandidateUIRegressionProbe::Apply(*service, context, state)), "preedit");
  expect(data.composition == L"nihao" && manager->ui_begin == 1 && manager->ui_update == 1,
         "host gets candidates without Win32 caret");
  expect(manager->ui_first_flags == (TF_CLUIE_DOCUMENTMGR | TF_CLUIE_COUNT | TF_CLUIE_STRING |
      TF_CLUIE_SELECTION | TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE), "initial flags");
  ITfCandidateListUIElementBehavior* element = nullptr;
  if (manager->ui_element) manager->ui_element->QueryInterface(IID_ITfCandidateListUIElementBehavior,
                                              reinterpret_cast<void**>(&element));
  expect(element != nullptr, "candidate behavior interface");
  if (element) {
    UINT count = 0, selected = 0;
    BOOL visible = TRUE;
    element->GetCount(&count);
    element->IsShown(&visible);
    expect(count == 2 && !visible, "host suppresses custom window");
    BSTR text = nullptr;
    expect(element->GetString(0, &text) == S_OK && std::wstring(text) == L"你好", "candidate string");
    SysFreeString(text);
    text = nullptr;
    expect(element->GetString(9, &text) == E_INVALIDARG && !text, "invalid candidate rejected");
    UINT pages[]{0, 1}, returned[2]{};
    expect(element->SetPageIndex(pages, 2) == S_OK, "host pagination");
    expect(element->SetSelection(1) == S_OK, "host changes highlight");
    element->GetSelection(&selected);
    element->GetPageIndex(returned, 2, &count);
    expect(selected == 1 && count == 2 && returned[1] == 1, "selection preserves host pagination");
    expect(element->Finalize() == S_OK && data.text == L"拟好" && !data.composing,
           "host finalization commits once through edit session");
    expect(manager->ui_end == 1 && !manager->ui_element, "end after commit");
    element->GetCount(&count);
    expect(count == 0 && element->Show(TRUE) == TF_E_DISCONNECTED &&
           element->Abort() == TF_E_DISCONNECTED, "retained element is inert and cleared");
    element->Release();
  }
  tsf::CandidateUIRegressionProbe::Apply(*service, context, state);
  service->OnSetFocus(FALSE);
  expect(!manager->ui_element && !data.composing, "focus loss ends host UI");
  manager->ui_fail_begin = true;
  tsf::CandidateUIRegressionProbe::Apply(*service, context, state);
  expect(!manager->ui_element, "failed Begin is not drawn");
  manager->ui_fail_begin = false;
  service->OnSetFocus(FALSE);
  manager->on_ui_begin = [&] { service->OnSetFocus(FALSE); };
  tsf::CandidateUIRegressionProbe::Apply(*service, context, state);
  expect(!manager->ui_element, "reentrant focus loss during Begin does not resurrect UI");
  manager->on_ui_begin = {};
  tsf::CandidateUIRegressionProbe::Apply(*service, context, state);
  manager->on_ui_update = [&] { service->OnSetFocus(FALSE); };
  tsf::CandidateUIRegressionProbe::Apply(*service, context, state);
  expect(!manager->ui_element && !data.composing,
         "reentrant focus loss during Update retires the element");
  manager->on_ui_update = {};
  service->Deactivate();
  expect(mode_text() == L"—" && mode->OnClick(TF_LBI_CLK_LEFT, {}, nullptr) == TF_E_DISCONNECTED,
      "retained indicator clears state and cannot call a retired service");
  mode->Release();
  service->Release();
  manager->Release();
  document->Release();
  context->Release();
  CoUninitialize();
  if (!failures) std::cout << "UI-less candidate lifecycle, selection, pagination, stale/reentrant callbacks passed\n";
  return failures ? 1 : 0;
}
