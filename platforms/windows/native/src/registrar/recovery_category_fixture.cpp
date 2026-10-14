#include <Windows.h>
#include <msctf.h>
#include <string_view>

#include "tsf/Guids.h"

#ifndef RIMES_RECOVERY_TEST_IDENTITY
#error This fixture must never use the production text-service identity.
#endif

// Remove one capability from the isolated recovery-test identity. The real
// registrar must detect the damaged shared state and repair it on registration.
int wmain(int argc, wchar_t** argv) {
  if (argc != 2) return 2;
  const std::wstring_view name(argv[1]);
  const GUID* category = name == L"uiElement"
      ? &GUID_TFCAT_TIPCAP_UIELEMENTENABLED
      : name == L"immersive" ? &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT : nullptr;
  if (!category) return 2;
  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 3;
  ITfCategoryMgr* manager = nullptr;
  auto result = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
      CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
      reinterpret_cast<void**>(&manager));
  if (SUCCEEDED(result)) {
    result = manager->UnregisterCategory(
        rimes::windows::tsf::kTextServiceClsid, *category,
        rimes::windows::tsf::kTextServiceClsid);
    manager->Release();
  }
  CoUninitialize();
  return SUCCEEDED(result) ? 0 : 4;
}
