#pragma once

#include <msctf.h>
#include <atomic>
#include <functional>
#include <utility>

#include "CandidateWindow.h"
#include "ModuleState.h"

namespace rimes::windows::tsf {

// One COM object per candidate presentation. Hosts may retain it after End;
// Detach removes its document, text and callbacks before the service retires.
class CandidateUIElement final : public ITfCandidateListUIElementBehavior {
 public:
  enum class Action { kSelect, kFinalize, kAbort };
  using Callback = std::function<HRESULT(Action, UINT)>;
  CandidateUIElement(ITfDocumentMgr* document, CandidateWindow* window,
                     Callback callback)
      : document_(document), window_(window), callback_(std::move(callback)) {
    if (document_) document_->AddRef();
    module::AddObject();
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (id != IID_IUnknown && id != IID_ITfUIElement &&
        id != IID_ITfCandidateListUIElement &&
        id != IID_ITfCandidateListUIElementBehavior) return E_NOINTERFACE;
    *out = static_cast<ITfCandidateListUIElementBehavior*>(this);
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const auto left = --refs_;
    if (!left) delete this;
    return left;
  }
  HRESULT STDMETHODCALLTYPE GetDescription(BSTR* out) override {
    if (!out) return E_POINTER;
    *out = SysAllocString(L"Lingxi candidates");
    return *out ? S_OK : E_OUTOFMEMORY;
  }
  HRESULT STDMETHODCALLTYPE GetGUID(GUID* out) override {
    if (!out) return E_POINTER;
    *out = {0x536b8052, 0xa456, 0x43ae, {0x86, 0x9d, 0xc4, 0x35, 0xfa, 0x47, 0x91, 0x82}};
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Show(BOOL show) override {
    if (!window_) return TF_E_DISCONNECTED;
    show_ = show != FALSE;
    Render();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE IsShown(BOOL* out) override {
    if (!out) return E_POINTER;
    *out = window_ && window_->IsVisible() ? TRUE : FALSE;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetUpdatedFlags(DWORD* out) override {
    if (!out) return E_POINTER;
    *out = flags_;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetDocumentMgr(ITfDocumentMgr** out) override {
    if (!out) return E_POINTER;
    *out = document_;
    if (*out) (*out)->AddRef();
    return document_ ? S_OK : TF_E_DISCONNECTED;
  }
  HRESULT STDMETHODCALLTYPE GetCount(UINT* out) override {
    if (!out) return E_POINTER;
    *out = static_cast<UINT>(snapshot_.items.size());
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetSelection(UINT* out) override {
    if (!out) return E_POINTER;
    *out = snapshot_.highlighted;
    return *out < snapshot_.items.size() ? S_OK : S_FALSE;
  }
  HRESULT STDMETHODCALLTYPE GetString(UINT index, BSTR* out) override {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (index >= snapshot_.items.size()) return E_INVALIDARG;
    const auto& text = snapshot_.items[index].text;
    *out = SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
    return *out ? S_OK : E_OUTOFMEMORY;
  }
  HRESULT STDMETHODCALLTYPE GetPageIndex(UINT* out, UINT size, UINT* count) override {
    if (!count || (!out && size)) return E_POINTER;
    *count = static_cast<UINT>(pages_.size());
    const auto copied = (std::min)(size, *count);
    for (UINT i = 0; i < copied; ++i) out[i] = pages_[i];
    return size >= *count ? S_OK : S_FALSE;
  }
  HRESULT STDMETHODCALLTYPE SetPageIndex(UINT* indexes, UINT count) override {
    if (!window_) return TF_E_DISCONNECTED;
    if (!indexes || !count || count > snapshot_.items.size() || indexes[0] != 0)
      return E_INVALIDARG;
    for (UINT i = 0; i < count; ++i)
      if (indexes[i] >= snapshot_.items.size() || (i && indexes[i] <= indexes[i-1]))
        return E_INVALIDARG;
    try { pages_.assign(indexes, indexes + count); }
    catch (...) { return E_OUTOFMEMORY; }
    flags_ |= TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetCurrentPage(UINT* out) override {
    if (!out) return E_POINTER;
    *out = 0;
    for (UINT i = 0; i < pages_.size(); ++i)
      if (pages_[i] <= snapshot_.highlighted) *out = i;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE SetSelection(UINT index) override {
    if (index >= snapshot_.items.size()) return E_INVALIDARG;
    return Invoke(Action::kSelect, index);
  }
  HRESULT STDMETHODCALLTYPE Finalize() override {
    if (snapshot_.highlighted >= snapshot_.items.size()) return E_FAIL;
    return Invoke(Action::kFinalize, snapshot_.highlighted);
  }
  HRESULT STDMETHODCALLTYPE Abort() override { return Invoke(Action::kAbort, 0); }

  void Update(const CandidateSnapshot& snapshot) {
    const bool strings_changed = snapshot_.items != snapshot.items;
    // Selection-only updates preserve the page breaks chosen by the host.
    if (strings_changed || pages_.empty()) pages_ = {0};
    snapshot_ = snapshot;
    flags_ = TF_CLUIE_SELECTION | TF_CLUIE_CURRENTPAGE;
    if (strings_changed)
      flags_ |= TF_CLUIE_COUNT | TF_CLUIE_STRING | TF_CLUIE_PAGEINDEX;
    Render();
  }
  void InitialFlags() { flags_ = TF_CLUIE_DOCUMENTMGR | TF_CLUIE_COUNT |
      TF_CLUIE_SELECTION | TF_CLUIE_STRING | TF_CLUIE_PAGEINDEX | TF_CLUIE_CURRENTPAGE; }
  bool OwnUIAllowed() const { return show_; }
  const CandidateSnapshot& Snapshot() const { return snapshot_; }
  void Detach() noexcept {
    auto* window = std::exchange(window_, nullptr);
    callback_ = {};
    snapshot_ = {};
    pages_.clear();
    if (document_) std::exchange(document_, nullptr)->Release();
    if (window) window->Hide();
  }

 private:
  ~CandidateUIElement() { Detach(); module::ReleaseObject(); }
  HRESULT Invoke(Action action, UINT index) noexcept {
    AddRef();
    HRESULT result = TF_E_DISCONNECTED;
    try {
      // The callback may retire this element. Copy it before calling out.
      const auto callback = callback_;
      if (callback) result = callback(action, index);
    } catch (...) { result = E_FAIL; }
    Release();
    return result;
  }
  void Render() noexcept {
    if (!window_) return;
    if (show_ && snapshot_.caret_rect.bottom > snapshot_.caret_rect.top)
      window_->Update(snapshot_);
    else window_->Hide();
  }
  std::atomic_ulong refs_{1};
  ITfDocumentMgr* document_;
  CandidateWindow* window_;
  Callback callback_;
  CandidateSnapshot snapshot_;
  std::vector<UINT> pages_;
  DWORD flags_ = 0;
  bool show_ = false;
};

}  // namespace rimes::windows::tsf
