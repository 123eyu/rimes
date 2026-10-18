#pragma once
#include <sapi.h>
#include <wrl/client.h>
#include "translation_languages.hpp"

#pragma comment(lib, "sapi.lib")
namespace rimes::windows::workbench {
// Owned by the existing UI STA. SAPI queues locally; no synthesized key/input
// events and no interpretation of translation text as XML or a filename.
class TranslationSpeech {
 public:
  ~TranslationSpeech() { Stop(); }
  void Stop() {
    if (voice_) voice_->Speak(nullptr, SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
  }
  bool Speak(const std::wstring& text, const std::string& language) {
    Stop();
    const auto id = TranslationLanguageID(language);
    const wchar_t* locale = nullptr;
    for (const auto& item : kTranslationLanguages) if (id == item.id) locale = item.locale;
    if (!locale || text.empty()) return false;
    if (!voice_ && FAILED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(&voice_)))) return false;
    Microsoft::WRL::ComPtr<ISpObjectTokenCategory> category;
    Microsoft::WRL::ComPtr<IEnumSpObjectTokens> tokens;
    Microsoft::WRL::ComPtr<ISpObjectToken> token;
    wchar_t filter[32]{};
    swprintf_s(filter, L"Language=%x", LANGIDFROMLCID(LocaleNameToLCID(locale, 0)));
    if (FAILED(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&category))) ||
        FAILED(category->SetId(SPCAT_VOICES, FALSE)) ||
        FAILED(category->EnumTokens(filter, nullptr, &tokens)) ||
        tokens->Next(1, &token, nullptr) != S_OK || FAILED(voice_->SetVoice(token.Get()))) return false;
    return SUCCEEDED(voice_->Speak(text.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK | SPF_IS_NOT_XML, nullptr));
  }
 private:
  Microsoft::WRL::ComPtr<ISpVoice> voice_;
};
}  // namespace rimes::windows::workbench
