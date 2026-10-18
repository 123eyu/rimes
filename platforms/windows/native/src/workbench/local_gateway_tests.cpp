#include "local_gateway.hpp"
#include "../peripherals/store.hpp"
#include <winhttp.h>
#include <atomic>
#include <iostream>
using namespace rimes::windows;
void Check(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
struct Reply { DWORD status = 0; core::Json body; };
Reply Request(unsigned short port, const wchar_t* route, const std::string& body,
              const std::wstring& headers = L"Authorization: Bearer synthetic-gateway-token\r\n") {
  const auto session = WinHttpOpen(L"RIMES isolated gateway tests", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
  WinHttpSetTimeouts(session, 500, 500, 500, 500);
  const auto connection = WinHttpConnect(session, L"127.0.0.1", port, 0);
  const auto request = WinHttpOpenRequest(connection, body.empty() ? L"GET" : L"POST", route, nullptr, nullptr, nullptr, 0);
  Reply reply;
  if (WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(headers.size()),
      const_cast<char*>(body.data()), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) &&
      WinHttpReceiveResponse(request, nullptr)) {
    DWORD size = sizeof(reply.status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &reply.status, &size, nullptr);
    std::string bytes; char block[8192]; DWORD read = 0;
    while (WinHttpReadData(request, block, sizeof(block), &read) && read) bytes.append(block, read);
    if (!bytes.empty()) reply.body = core::Json::parse(bytes);
  }
  WinHttpCloseHandle(request); WinHttpCloseHandle(connection); WinHttpCloseHandle(session);
  return reply;
}
int main() {
  const auto root = std::filesystem::temp_directory_path() / (L"rimes-gateway-test-" + std::to_wstring(GetCurrentProcessId()));
  try {
    Check(std::filesystem::create_directory(root), "new isolated fixture directory");
    SetEnvironmentVariableW(L"LOCALAPPDATA", root.c_str());
    const auto port = static_cast<unsigned short>(49152 + GetCurrentProcessId() % 15000);
    std::atomic<bool> enabled{true};
    {
      workbench::LocalGateway server([&] { return enabled.load(); }, port, "synthetic-gateway-token");
      bool ready = false;
      for (int i = 0; i < 30 && !ready; ++i) { Sleep(30); ready = Request(port, L"/v1/health", "").status == 200; }
      Check(ready, "loopback listener starts");
      Check(Request(port, L"/v1/inbound", R"({"text":"do not store"})", L"").status == 401, "unauthenticated mutation rejected");
      Check(Request(port, L"/v1/inbound", R"({"text":"do not store"})",
          L"Authorization: Bearer synthetic-gateway-token\r\nOrigin: https://evil.invalid\r\n").status == 403, "foreign browser origin rejected");
      auto response = Request(port, L"/v1/inbound", R"({"text":"review me","format":"markdown"})");
      Check(response.status == 200 && response.body["disposition"] == "mailbox", "HTTP stages review item");
      peripherals::Store store;
      Check(store.List(peripherals::Module::Inbox).size() == 1, "one accepted inbox item");
      auto call = [&](const std::string& name, const core::Json& args) {
        return Request(port, L"/mcp", core::Json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
            {"params", {{"name", name}, {"arguments", args}}}}.dump());
      };
      auto begin = call("buffer_stream_begin", {{"title", "stream test"}});
      auto id = core::Json::parse(begin.body["result"]["content"][0]["text"].get<std::string>())["stream_id"];
      Check(!call("buffer_stream_append", {{"stream_id", id}, {"delta", "stream text"}}).body["result"]["isError"].get<bool>(), "MCP append");
      Check(store.List(peripherals::Module::Inbox).size() == 1, "incomplete stream cannot be accepted");
      Check(!call("buffer_stream_end", {{"stream_id", id}}).body["result"]["isError"].get<bool>(), "MCP finish");
      Check(call("buffer_stream_end", {{"stream_id", id}}).body["result"]["isError"].get<bool>(), "duplicate finish cannot create duplicate item");
      Check(store.List(peripherals::Module::Inbox).size() == 2, "one complete streamed item");
      store.Enable(peripherals::Module::Inbox, false);
      Check(call("buffer_push", {{"text", "disabled"}}).body["result"]["isError"].get<bool>(), "disabled inbox rejects ingress");
      enabled = false; Sleep(250);
      Check(Request(port, L"/v1/health", "").status == 0, "disable closes listener");
    }
    for (unsigned client = 0; client < 4; ++client)
      Check(workbench::GatewayConfiguration(client).find(L"synthetic-gateway-token") == std::wstring::npos,
            "configuration previews cannot expose token");
    std::filesystem::remove_all(root);
    std::cout << "Gateway HTTP/MCP, authentication, origin, stream and disable tests passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
