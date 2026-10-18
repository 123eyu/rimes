#include <iostream>
#include <deque>

#include "TextService.h"
#include "fake_tsf.hpp"

namespace {
using namespace rimes::windows;

// Script only the engine replies. The production TextService still owns key
// prediction, scope checks, focus lifetime and all document/composition edits.
class FirstKeyBroker final : public tsf::BrokerClient {
 public:
  bool connected = true, ascii = false;
  unsigned input_calls = 0;
  std::uint64_t context = 0;
  std::wstring preedit;
  HWND notification = nullptr;
  bool capturing = false, delivery_allowed = true;
  unsigned transitions = 0, guards = 0;
  std::optional<bool> acknowledgement;
  std::deque<core::Json> notifications;
  void Notify(core::Json message) {
    message["context"] = context; message["connectionGeneration"] = 0;
    notifications.push_back(std::move(message));
    SendMessageW(notification, tsf::kBrokerNotification, 0, 0);
  }
  void BeginConnect() noexcept override {}
  void Disconnect() noexcept override {}
  bool SetContext(std::uint64_t value) noexcept override {
    if (value != context) preedit.clear();
    context = value;
    return connected;
  }
  void SetNotificationWindow(HWND window) noexcept override { notification = window; }
  std::optional<core::Json> TakeNotification() override {
    if (notifications.empty()) return {};
    auto value = std::move(notifications.front()); notifications.pop_front(); return value;
  }
  bool Control(core::Json value) noexcept override {
    const auto op = value.value("op", "");
    if (op == "buffer_transition") ++transitions;
    if (op == "delivery_guard") { ++guards; return delivery_allowed; }
    if (op == "ack") acknowledgement = value.value("accepted", false);
    return true;
  }
  bool Capturing() const noexcept override { return capturing; }
  bool IsConnected() const noexcept override { return connected; }
  std::uint64_t ConnectionGeneration() const noexcept override { return 0; }
  std::optional<bool> AsciiMode() const noexcept override { return ascii; }
  tsf::BrokerKeyResult HandleKey(const tsf::BrokerKeyEvent& event,
                               tsf::BrokerInputState* state) noexcept override {
    using enum tsf::BrokerKeyResult;
    if (!connected) return kUnavailable;
    if (ascii || (event.virtual_key != 'Y' && event.virtual_key != 'O' &&
                  event.virtual_key != 'U' && event.virtual_key != VK_SPACE))
      return kPassThrough;
    if (event.phase != tsf::BrokerKeyPhase::kKeyDown) return kConsumed;
    ++input_calls;
    if (!state) return kUnavailable;
    *state = {};
    state->has_snapshot = true;
    if (event.virtual_key == VK_SPACE) {
      state->commit_text = preedit == L"you" ? L"有" : L"错误";
      preedit.clear();
    } else {
      preedit.push_back(static_cast<wchar_t>(event.virtual_key + 'a' - 'A'));
      state->composition = preedit;
      state->caret_utf16 = static_cast<std::uint32_t>(preedit.size());
      state->composing = true;
    }
    return kConsumed;
  }
};
}

int main() {
  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
  int failures = 0;
  const auto expect = [&](bool ok, const char* name) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << name << '\n'; }
  };
  e2e::FakeDocument data;
  auto* context = new e2e::FakeContext(&data);
  auto* document = new e2e::FakeDocumentMgr(context);
  auto* manager = new e2e::FakeThreadMgr;
  manager->SetFocus(document);
  auto broker = std::make_unique<FirstKeyBroker>();
  auto* probe = broker.get();
  auto* service = new tsf::TextService(std::move(broker));
  expect(SUCCEEDED(service->Activate(manager, 1)), "activate first-key fixture");

  // A host doing its key probe under another lock cannot grant a synchronous
  // scope read. Only an accepted probe leads to OnKeyDown/OnKeyUp, as in TSF.
  // The host releases that lock before dispatching the real physical event.
  const auto press = [&](WPARAM key) {
    context->refuse_read_edits = true;
    const auto reads = context->read_requests;
    const auto writes = context->write_requests;
    const auto calls = probe->input_calls;
    BOOL eaten = FALSE;
    service->OnTestKeyDown(context, key, 1, &eaten);
    expect(eaten, "test callback retains the first Chinese key without an edit lock");
    expect(context->read_requests == reads && context->write_requests == writes &&
               probe->input_calls == calls,
           "test callback never edits, requests a text lock or advances the engine");
    context->refuse_read_edits = false;
    if (eaten) {
      service->OnKeyDown(context, key, 1, &eaten);
      expect(eaten, "real physical key is consumed after scope validation");
    }
    context->refuse_read_edits = true;
    service->OnTestKeyUp(context, key, 1, &eaten);
    context->refuse_read_edits = false;
    if (eaten) service->OnKeyUp(context, key, 1, &eaten);
  };
  for (int focus = 0; focus < 2; ++focus) {
    service->OnSetFocus(TRUE);
    const auto generation = probe->context;
    press('Y');
    expect(data.composing && data.composition == L"y",
           "first letter immediately establishes marked text");
    expect(probe->context == generation,
           "test callbacks do not retire the current context");
    press('O'); press('U'); press(VK_SPACE);
    expect(!data.composing && data.text == (focus == 0 ? L"有" : L"有有"),
           "first word commits Chinese exactly once on each focus entry");
    service->OnSetFocus(FALSE);
  }

  BOOL eaten = TRUE;
  probe->ascii = true;
  service->OnTestKeyDown(context, 'Y', 1, &eaten);
  expect(!eaten, "ASCII mode keeps the first letter with the host");
  probe->ascii = false;
  probe->connected = false;
  service->OnTestKeyDown(context, 'Y', 1, &eaten);
  expect(!eaten, "unavailable engine does not swallow a first letter");
  probe->connected = true;
  context->read_only = true;
  const auto calls = probe->input_calls;
  service->OnTestKeyDown(context, 'Y', 1, &eaten);
  expect(!eaten, "read-only host is rejected even during prediction");
  service->OnKeyDown(context, 'Y', 1, &eaten);
  expect(!eaten && probe->input_calls == calls,
         "read-only host never exposes keys to the engine");
  context->read_only = false;
  service->OnTestKeyDown(context, 'Y', 1, &eaten);
  expect(eaten, "editable host predicts Chinese input");
  context->read_only = true;
  service->OnKeyDown(context, 'Y', 1, &eaten);
  expect(!eaten && probe->input_calls == calls,
         "host protection changing after prediction is checked again");
  context->read_only = false;
  context->refuse_read_edits = true;
  service->OnKeyDown(context, 'Y', 1, &eaten);
  expect(!eaten && probe->input_calls == calls,
         "real event still requires a granted scope read before forwarding input");
  context->refuse_read_edits = false;
  service->OnKeyDown(context, 'Y', 1, &eaten);
  expect(eaten && probe->input_calls == calls + 1 && data.composition == L"y",
         "failed real key is never replayed when the host becomes available");

  // Own a disposable foreground host, never a real application document. The
  // production notification handler, scope guard and async edit session run.
  const HWND previous_foreground = GetForegroundWindow();
  HWND frame = CreateWindowExW(0, L"STATIC", L"RIMES lifecycle fixture", WS_POPUP,
      700, 420, 240, 80, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  ShowWindow(frame, SW_SHOW); SetForegroundWindow(frame);
  expect(GetForegroundWindow() == frame, "lifecycle fixture owns its foreground host");
  service->OnSetFocus(TRUE);
  probe->capturing = true;
  probe->Notify({{"kind", "capture"}, {"enabled", true}, {"openingRevision", 7}, {"inputRoute", 1}});
  const auto original_text = data.text;
  const auto transition = [&](std::uint64_t opening = 7) {
    probe->Notify({{"kind", "buffer_transition"}, {"openingRevision", opening}, {"request", 1}, {"close", false}});
  };
  context->read_only = true; transition(); context->read_only = false;
  expect(probe->transitions == 0, "protected host cannot settle Buffer composition through a UI action");
  transition(6);
  expect(probe->transitions == 0, "a previous opening cannot request composition settlement");
  transition();
  expect(probe->transitions == 1 && data.text == original_text && !data.composing,
         "settling a plugin switch retires the TSF placeholder without inserting into the host");
  context->defer_edits = true;
  probe->Notify({{"kind", "deliver"}, {"request", 11}, {"text", "old"}, {"guard", true}});
  expect(!context->delayed_edits.empty(), "delivery waits for the host's async edit lock");
  probe->delivery_allowed = false; context->DrainEdits();
  expect(probe->guards == 1 && probe->acknowledgement == false && data.text == original_text,
         "closing Buffer before an async edit executes rejects the obsolete delivery");
  probe->delivery_allowed = true; context->defer_edits = false;
  probe->Notify({{"kind", "deliver"}, {"request", 12}, {"text", "new"}, {"guard", true}});
  expect(probe->guards == 2 && probe->acknowledgement == true && data.text == original_text + L"new",
         "the current exact delivery still commits once after revalidation");
  DestroyWindow(frame);
  if (previous_foreground) SetForegroundWindow(previous_foreground);

  service->Deactivate();
  service->Release();
  manager->Release();
  document->Release();
  context->Release();
  CoUninitialize();
  if (!failures) std::cout << "First-key ownership and host scope regressions passed\n";
  return failures ? 1 : 0;
}
