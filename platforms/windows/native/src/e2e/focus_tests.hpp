#pragma once

// In-process fake host; no desktop input, installed IME or user data involved.
namespace {
class FocusProbeBroker final : public rimes::windows::tsf::BrokerClient {
 public:
  std::uint64_t context = 0, connection = 1;
  bool connected = true, capturing = false;
  bool consume = false, send_chord = false;
  unsigned key_calls = 0;
  HWND notification_window = nullptr;
  void BeginConnect() noexcept override {}
  void Disconnect() noexcept override { context = 0; }
  bool SetContext(std::uint64_t value) noexcept override {
    if (!connected) return false;
    if (context != value) capturing = false;
    context = value;
    return true;
  }
  void SetNotificationWindow(HWND value) noexcept override {
    notification_window = value;
  }
  std::optional<rimes::windows::core::Json> TakeNotification() override {
    return {};
  }
  bool Control(rimes::windows::core::Json) noexcept override { return true; }
  bool Capturing() const noexcept override { return capturing; }
  bool IsBufferSendKey(WPARAM key) const noexcept override { return key == 'J' && send_chord; }
  std::uint64_t ConnectionGeneration() const noexcept override {
    return connection;
  }
  bool IsConnected() const noexcept override { return connected; }
  rimes::windows::tsf::BrokerKeyResult HandleKey(
      const rimes::windows::tsf::BrokerKeyEvent&,
      rimes::windows::tsf::BrokerInputState*) noexcept override {
    ++key_calls;
    return consume ? rimes::windows::tsf::BrokerKeyResult::kConsumed
                   : rimes::windows::tsf::BrokerKeyResult::kPassThrough;
  }
};

class SelectionEditRecord final : public ITfEditRecord {
 public:
  bool changed = true;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
    if (!result) return E_POINTER;
    *result = nullptr;
    if (iid != IID_IUnknown && iid != IID_ITfEditRecord) return E_NOINTERFACE;
    *result = static_cast<ITfEditRecord*>(this);
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
  ULONG STDMETHODCALLTYPE Release() override { return 1; }
  HRESULT STDMETHODCALLTYPE GetSelectionStatus(BOOL* value) override {
    if (!value) return E_POINTER;
    *value = changed ? TRUE : FALSE;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetTextAndPropertyUpdates(
      DWORD, const GUID**, ULONG, IEnumTfRanges**) override { return E_NOTIMPL; }
};

void DrainFocusNotifications(HWND window) {
  MSG message{};
  while (PeekMessageW(&message, window, 0, 0, PM_REMOVE))
    DispatchMessageW(&message);
}

void CheckFocusRestoration() {
  using namespace rimes::windows::e2e;
  using rimes::windows::tsf::TextService;
  FakeDocument document;
  auto* context = new FakeContext(&document);
  auto* manager = new FakeDocumentMgr(context);
  auto* thread = new FakeThreadMgr();
  thread->SetFocus(manager);
  auto broker = std::make_unique<FocusProbeBroker>();
  auto* probe = broker.get();
  auto* service = new TextService(std::move(broker));
  Expect(SUCCEEDED(service->Activate(thread, 1)), "focus probe activates");
  service->OnSetFocus(TRUE);
  const auto first = probe->context;
  Expect(first != 0, "foreground entry publishes target before any key");
  service->OnSetFocus(FALSE);
  Expect(probe->context == 0, "foreground loss revokes old target");
  service->OnSetFocus(TRUE);
  Expect(probe->context > first,
         "same document return publishes a fresh target without typing");
  const auto resumed = probe->context;
  service->OnSetFocus(TRUE);
  Expect(probe->context == resumed, "duplicate foreground event is stable");
  service->OnSetFocus(FALSE);
  probe->connected = false;
  service->OnSetFocus(TRUE);
  Expect(probe->context == 0, "disconnected foreground has no broker target");
  probe->connected = true;
  ++probe->connection;
  service->OnSetFocus(TRUE);
  Expect(probe->context > resumed,
         "connection-ready focus refresh binds without replaying a key");

  probe->capturing = probe->consume = probe->send_chord = true;
  BOOL eaten = FALSE;
  service->OnKeyDown(context, 'J', 1, &eaten);
  Expect(eaten, "custom send key is owned on its first physical press");
  const auto pressed_calls = probe->key_calls;
  probe->send_chord = false;  // Modifiers released or binding edited mid-press.
  service->OnTestKeyDown(context, 'J', LPARAM(1ULL << 30), &eaten);
  Expect(eaten && probe->key_calls == pressed_calls, "owned custom repeat stays local after modifier change");
  service->OnKeyDown(context, 'J', LPARAM(1ULL << 30), &eaten);
  Expect(eaten && probe->key_calls == pressed_calls, "repeat cannot settle composition or deliver twice");
  service->OnTestKeyUp(context, 'J', 0, &eaten);
  Expect(eaten && probe->key_calls == pressed_calls, "custom send test-up has no Broker mutation");
  service->OnKeyUp(context, 'J', 0, &eaten);
  Expect(eaten && probe->key_calls == pressed_calls + 1, "matching physical release reaches the Broker once");
  service->OnKeyDown(context, 'J', 1, &eaten);
  const auto typing_calls = probe->key_calls;
  service->OnKeyDown(context, 'J', LPARAM(1ULL << 30), &eaten);
  Expect(eaten && probe->key_calls == typing_calls + 1, "ordinary repeated J remains typing, not a send gesture");
  probe->capturing = probe->consume = false;

  SelectionEditRecord record;
  const auto before_selection = probe->context;
  probe->capturing = true;
  record.changed = false;
  service->OnEndEdit(context, 0, &record);
  Expect(probe->context == before_selection && probe->capturing,
         "edit with unchanged selection keeps capture");
  record.changed = true;
  service->OnEndEdit(context, 0, &record);
  Expect(probe->context == 0 && !probe->capturing,
         "same-context selection change immediately revokes capture");
  DrainFocusNotifications(probe->notification_window);
  Expect(probe->context > before_selection && !probe->capturing,
         "selection refresh publishes a fresh target without auto capture");
  probe->capturing = true;
  service->OnEndEdit(context, 0, &record);
  thread->thread_focus = false;
  DrainFocusNotifications(probe->notification_window);
  Expect(probe->context == 0,
         "late selection refresh cannot revive a background thread");
  thread->thread_focus = true;
  service->OnSetFocus(TRUE);
  probe->capturing = true;
  service->OnEndEdit(context, 0, &record);
  context->read_only = true;
  DrainFocusNotifications(probe->notification_window);
  Expect(probe->context == 0, "queued refresh rejects a now read-only field");
  context->read_only = false;
  thread->SetFocus(nullptr);
  service->OnSetFocus(TRUE);
  Expect(probe->context == 0, "empty focus cannot revive saved target");
  service->Deactivate();
  service->Release();

  thread->SetFocus(manager);
  broker = std::make_unique<FocusProbeBroker>();
  probe = broker.get();
  service = new TextService(std::move(broker));
  service->ActivateEx(thread, 1, TF_TMAE_SECUREMODE);
  service->OnSetFocus(TRUE);
  Expect(probe->context == 0, "secure activation never publishes a target");
  service->Deactivate();
  service->Release();
  thread->Release();
  manager->Release();
  context->Release();
}
}  // namespace
