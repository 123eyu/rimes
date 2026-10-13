#include <Windows.h>
#include <sddl.h>
#include <iomanip>
#include <sstream>
#include <vector>
#include "TextService.h"
#include "fake_tsf.hpp"
#include "input_pipe.hpp"
#include "../core/broker_identity.hpp"

// Exit codes identify the failed assertion even without stdout in a sandbox.
int main() {
  using namespace rimes::windows;
  bool sandboxed = false;
  if (!tsf::ReadAppContainer(&sandboxed) || !sandboxed) return 20;
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return 21;
  DWORD bytes = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
  std::vector<std::byte> storage(bytes);
  const bool got_user = GetTokenInformation(token, TokenUser, storage.data(), bytes, &bytes) != FALSE;
  CloseHandle(token);
  if (!got_user) return 22;
  const auto sid = reinterpret_cast<TOKEN_USER*>(storage.data())->User.Sid;
  LPWSTR string_sid = nullptr;
  if (!ConvertSidToStringSidW(sid, &string_sid)) return 23;
  std::uint64_t hash = 14695981039346656037ULL;
  for (const auto c : std::wstring(string_sid)) { hash ^= c; hash *= 1099511628211ULL; }
  LocalFree(string_sid);
  DWORD session = 0;
  ProcessIdToSessionId(GetCurrentProcessId(), &session);
  std::wostringstream endpoint;
  endpoint << L"\\\\.\\pipe\\" << core::kBrokerObjectName << L".v2.session-" << session
           << L".user-" << std::hex << std::setw(16) << std::setfill(L'0') << hash;
  auto open = [](const std::wstring& name, DWORD access) {
    return CreateFileW(name.c_str(), access, 0, nullptr, OPEN_EXISTING,
                       SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
  };
  HANDLE denied = open(endpoint.str(), GENERIC_READ | GENERIC_WRITE);
  if (denied != INVALID_HANDLE_VALUE) { CloseHandle(denied); return 24; }
  if (GetLastError() != ERROR_ACCESS_DENIED) return 25;
  const auto input = endpoint.str() + L".input";
  denied = open(input, WRITE_DAC | WRITE_OWNER);
  if (denied != INVALID_HANDLE_VALUE) { CloseHandle(denied); return 26; }
  if (GetLastError() != ERROR_ACCESS_DENIED) return 27;
  HANDLE pipe = open(input, FILE_READ_DATA | FILE_WRITE_DATA | READ_CONTROL | SYNCHRONIZE);
  if (pipe == INVALID_HANDLE_VALUE) return 1000 + static_cast<int>(GetLastError());
  DWORD server = 0;
  if (!tsf::VerifyInputPipe(pipe, sid, session, &server)) { CloseHandle(pipe); return 29; }
  if (tsf::VerifyInputPipe(pipe, sid, session + 1, &server)) { CloseHandle(pipe); return 30; }
  PSID wrong = nullptr;
  ConvertStringSidToSidW(L"S-1-5-18", &wrong);
  const bool wrong_accepted = tsf::VerifyInputPipe(pipe, wrong, session, &server);
  LocalFree(wrong);
  CloseHandle(pipe);
  if (wrong_accepted) return 31;
  // Packages must not be able to impersonate the Broker by adding a server instance.
  denied = CreateNamedPipeW(input.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE,
                            PIPE_UNLIMITED_INSTANCES, 1024, 1024, 0, nullptr);
  if (denied != INVALID_HANDLE_VALUE) { CloseHandle(denied); return 32; }
  // A package also cannot squat the global endpoint before the Broker starts.
  const auto fresh = input + L".unowned-probe-" + std::to_wstring(GetCurrentProcessId());
  denied = CreateNamedPipeW(fresh.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                            PIPE_TYPE_BYTE, 1, 1024, 1024, 0, nullptr);
  if (denied != INVALID_HANDLE_VALUE) { CloseHandle(denied); return 40; }
  if (GetLastError() != ERROR_ACCESS_DENIED) return 41;

  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 33;
  int result = 0;
  e2e::FakeDocument data;
  data.refuse_caret = true;
  auto* context = new e2e::FakeContext(&data);
  auto* document = new e2e::FakeDocumentMgr(context);
  auto* manager = new e2e::FakeThreadMgr;
  manager->ui_enabled = true;
  manager->SetFocus(document);
  auto client = tsf::CreateBrokerClient();
  auto* raw_client = client.get();
  auto* service = new tsf::TextService(std::move(client));
  service->ActivateEx(manager, 1, TF_TMAE_UIELEMENTENABLEDONLY);
  for (unsigned i = 0; i < 250 && !service->IsBrokerConnected(); ++i) Sleep(20);
  if (!service->IsBrokerConnected()) result = 34;
  BYTE keys[256]{};
  SetKeyboardState(keys);
  auto tap = [&](WPARAM key) {
    BOOL eaten = FALSE;
    service->OnTestKeyDown(context, key, 1, &eaten);
    if (!eaten) return false;
    service->OnKeyDown(context, key, 1, &eaten);
    const bool consumed = eaten != FALSE;
    service->OnTestKeyUp(context, key, static_cast<LPARAM>(1ULL << 31), &eaten);
    service->OnKeyUp(context, key, static_cast<LPARAM>(1ULL << 31), &eaten);
    return consumed;
  };
  if (!result) {
    for (const auto key : std::wstring(L"NIHAO")) if (!tap(key)) { result = 35; break; }
    if (!result && (!data.composing || !manager->ui_element)) result = 36;
    if (!result && (!tap(VK_SPACE) || data.text != L"你好" || data.composing)) result = 37;
    if (!result && (manager->ui_begin != 1 || manager->ui_end != 1)) result = 38;
    if (!result && raw_client->Control({{"op", "open_settings"}})) result = 39;
    // The forbidden command closes the restricted connection, never invoking UI.
  }
  service->Deactivate();
  service->Release();
  manager->Release(); document->Release(); context->Release();
  CoUninitialize();
  return result;
}
