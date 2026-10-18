#pragma once
#include <Windows.h>
#include <msctf.h>
#include <ctfutb.h>
#include <ctffunc.h>
#include <olectl.h>
#include <atomic>
#include <functional>
#include <optional>
#include <utility>
#include "Guids.h"
#include "ModuleState.h"
#include "../ui/product_icon.h"

namespace rimes::windows::tsf {
// The shell owns presentation; this item only publishes authoritative Rime mode.
// Detach clears the callback before releasing TSF's reference, since hosts may
// retain a language-bar item beyond TextService::Deactivate.
class InputModeIndicator final : public ITfLangBarItemButton, public ITfSource {
 public:
  explicit InputModeIndicator(std::function<void()> toggle) : toggle_(std::move(toggle)) {
    module::AddObject();
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (id == IID_IUnknown || id == IID_ITfLangBarItem || id == IID_ITfLangBarItemButton)
      *out = static_cast<ITfLangBarItemButton*>(this);
    else if (id == IID_ITfSource) *out = static_cast<ITfSource*>(this);
    else return E_NOINTERFACE;
    AddRef(); return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const auto count = --refs_; if (!count) delete this; return count;
  }
  HRESULT Attach(ITfThreadMgr* manager) {
    auto hr = manager->QueryInterface(IID_ITfLangBarItemMgr, reinterpret_cast<void**>(&manager_));
    if (FAILED(hr)) return hr;
    hr = manager_->AddItem(this);
    if (FAILED(hr)) std::exchange(manager_, nullptr)->Release();
    return hr;
  }
  void Detach() {
    toggle_ = {};
    Update(std::nullopt);
    if (manager_) {
      auto* manager = std::exchange(manager_, nullptr);
      manager->RemoveItem(this); manager->Release();
    }
    if (sink_) std::exchange(sink_, nullptr)->Release();
  }
  void Update(std::optional<bool> ascii) {
    if (mode_ == ascii) return;
    mode_ = ascii;
    if (sink_) sink_->OnUpdate(TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_TOOLTIP | TF_LBI_STATUS);
  }
  HRESULT STDMETHODCALLTYPE GetInfo(TF_LANGBARITEMINFO* out) override {
    if (!out) return E_POINTER;
    *out = {};
    out->clsidService = kTextServiceClsid;
    out->guidItem = GUID_LBI_INPUTMODE;
    out->dwStyle = TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_SHOWNINTRAY;
    wcscpy_s(out->szDescription, L"中英文切换");
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetStatus(DWORD* out) override {
    if (!out) return E_POINTER;
    *out = mode_.has_value() ? 0 : TF_LBI_STATUS_DISABLED; return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Show(BOOL) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE GetTooltipString(BSTR* out) override {
    return String(out, !mode_ ? L"输入法暂不可用" : *mode_
        ? L"英文输入 · 按 Shift 或点击切换中文" : L"中文输入 · 按 Shift 或点击切换英文");
  }
  HRESULT STDMETHODCALLTYPE OnClick(TfLBIClick click, POINT, const RECT*) override {
    if (click != TF_LBI_CLK_LEFT) return S_OK;
    if (!mode_ || !toggle_) return TF_E_DISCONNECTED;
    try { toggle_(); return S_OK; } catch (...) { return E_FAIL; }
  }
  HRESULT STDMETHODCALLTYPE InitMenu(ITfMenu*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnMenuSelect(UINT) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE GetIcon(HICON* out) override {
    if (!out) return E_POINTER;
    const auto instance = module::Instance() ? module::Instance() : GetModuleHandleW(nullptr);
    const int resource = !mode_ ? IDI_RIMES_MODE_DISABLED : *mode_ ? IDI_RIMES_MODE_EN : IDI_RIMES_MODE_ZH;
    *out = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(resource), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    return *out ? S_OK : E_FAIL; // Caller destroys its owned icon.
  }
  HRESULT STDMETHODCALLTYPE GetText(BSTR* out) override {
    return String(out, !mode_ ? L"—" : *mode_ ? L"英" : L"中");
  }
  HRESULT STDMETHODCALLTYPE AdviseSink(REFIID id, IUnknown* unknown, DWORD* cookie) override {
    if (!unknown || !cookie) return E_POINTER;
    *cookie = TF_INVALID_COOKIE;
    if (id != IID_ITfLangBarItemSink) return CONNECT_E_CANNOTCONNECT;
    if (sink_) return CONNECT_E_ADVISELIMIT;
    const auto hr = unknown->QueryInterface(id, reinterpret_cast<void**>(&sink_));
    if (SUCCEEDED(hr)) *cookie = 1;
    return hr;
  }
  HRESULT STDMETHODCALLTYPE UnadviseSink(DWORD cookie) override {
    if (cookie != 1 || !sink_) return CONNECT_E_NOCONNECTION;
    std::exchange(sink_, nullptr)->Release(); return S_OK;
  }
 private:
  ~InputModeIndicator() { Detach(); module::ReleaseObject(); }
  static HRESULT String(BSTR* out, const wchar_t* value) {
    if (!out) return E_POINTER;
    *out = SysAllocString(value); return *out ? S_OK : E_OUTOFMEMORY;
  }
  std::atomic_ulong refs_{1};
  ITfLangBarItemMgr* manager_ = nullptr;
  ITfLangBarItemSink* sink_ = nullptr;
  std::optional<bool> mode_;
  std::function<void()> toggle_;
};
}
