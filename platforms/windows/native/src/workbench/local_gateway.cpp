#include <winsock2.h>
#include <ws2tcpip.h>
#include "local_gateway.hpp"
#include "../peripherals/store.hpp"
#include <wincred.h>
#include <bcrypt.h>
#include <atomic>
#include <charconv>
#include <map>
#include <mutex>
#include <thread>

namespace rimes::windows::workbench {
using core::Json;
namespace {
std::atomic<int> gateway_state{0};
std::atomic<unsigned long long> gateway_revision{0};
std::mutex token_mutex;
constexpr std::size_t kLimit = 256 * 1024;
constexpr wchar_t kCredential[] = L"RIMES.Windows.LocalGateway";
struct Socket {
  SOCKET value = INVALID_SOCKET;
  ~Socket() { Reset(); }
  void Reset() { if (value != INVALID_SOCKET) closesocket(value); value = INVALID_SOCKET; }
};
std::string Token() {
  std::lock_guard lock(token_mutex);
  // The preview never calls this function. No token is stored in settings,
  // logs, client config files, or the source tree.
  PCREDENTIALW credential = nullptr;
  if (CredReadW(kCredential, CRED_TYPE_GENERIC, 0, &credential)) {
    std::string token(reinterpret_cast<char*>(credential->CredentialBlob), credential->CredentialBlobSize);
    CredFree(credential);
    if (token.size() == 64) return token;
    throw std::runtime_error("Invalid gateway credential");
  }
  if (GetLastError() != ERROR_NOT_FOUND) throw std::runtime_error("Gateway credential unavailable");
  unsigned char bytes[32]{};
  if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
    throw std::runtime_error("Random source unavailable");
  std::string token;
  for (auto b : bytes) { token += "0123456789abcdef"[b >> 4]; token += "0123456789abcdef"[b & 15]; }
  CREDENTIALW value{}; value.Type = CRED_TYPE_GENERIC;
  value.TargetName = const_cast<wchar_t*>(kCredential);
  value.CredentialBlob = reinterpret_cast<BYTE*>(token.data());
  value.CredentialBlobSize = static_cast<DWORD>(token.size()); value.Persist = CRED_PERSIST_LOCAL_MACHINE;
  if (!CredWriteW(&value, 0)) throw std::runtime_error("Cannot save gateway credential");
  SecureZeroMemory(bytes, sizeof(bytes));
  return token;
}
bool Equal(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  unsigned difference = 0;
  for (std::size_t i = 0; i < a.size(); ++i) difference |= static_cast<unsigned char>(a[i] ^ b[i]);
  return difference == 0;
}
std::string Lower(std::string s) {
  for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 'a' - 'A');
  return s;
}
bool OriginAllowed(const std::string& origin) {
  auto value = Lower(origin);
  const auto scheme = value.find("://");
  if (scheme == std::string::npos || (value.substr(0, scheme) != "http" && value.substr(0, scheme) != "https")) return false;
  value.erase(0, scheme + 3);
  for (const auto* host : {"localhost", "127.0.0.1", "[::1]"}) {
    const std::string prefix(host);
    if (value == prefix) return true;
    if (!value.starts_with(prefix + ":")) continue;
    const auto port = value.substr(prefix.size() + 1);
    unsigned number = 0;
    const auto parsed = std::from_chars(port.data(), port.data() + port.size(), number);
    return !port.empty() && parsed.ec == std::errc{} && parsed.ptr == port.data() + port.size() && number <= 65535;
  }
  return false;
}
Json ToolList() {
  Json result = Json::array();
  for (const auto* name : {"buffer_push", "buffer_stream_begin", "buffer_stream_append", "buffer_stream_end"}) {
    Json properties = Json::object(), required = Json::array();
    const std::string n(name);
    if (n == "buffer_push" || n == "buffer_stream_begin") {
      properties["title"] = {{"type", "string"}};
      properties["format"] = {{"type", "string"}, {"enum", {"plain", "markdown", "json"}}};
    }
    if (n == "buffer_push") { properties["text"] = {{"type", "string"}}; required.push_back("text"); }
    if (n == "buffer_stream_append" || n == "buffer_stream_end") {
      properties["stream_id"] = {{"type", "string"}}; required.push_back("stream_id");
    }
    if (n == "buffer_stream_append") { properties["delta"] = {{"type", "string"}}; required.push_back("delta"); }
    result.push_back({{"name", n}, {"description", "Send text to Mailbox for user review. Never reads or automatically inserts text."},
        {"inputSchema", {{"type", "object"}, {"properties", properties}, {"required", required}}},
        {"annotations", {{"readOnlyHint", false}, {"destructiveHint", false}, {"openWorldHint", false}}}});
  }
  return result;
}
}
struct LocalGateway::Impl {
  std::function<bool()> enabled;
  unsigned short port;
  std::string fixture_token;
  std::jthread worker;
  struct Stream { Json metadata; std::string text; ULONGLONG updated; };
  std::map<std::string, Stream> streams;
  Impl(std::function<bool()> predicate, unsigned short number, std::string token)
      : enabled(std::move(predicate)), port(number), fixture_token(std::move(token)) {
    worker = std::jthread([this](std::stop_token stop) { Run(stop); });
  }
  ~Impl() { worker.request_stop(); worker.join(); }
  void Submit(const Json& args) {
    if (!enabled()) throw std::runtime_error("Gateway disabled");
    const auto text = args.at("text").get<std::string>();
    const auto format = args.value("format", "plain");
    if (text.empty() || text.size() > kLimit || (format != "plain" && format != "markdown" && format != "json"))
      throw std::runtime_error("Invalid text or format");
    (void)Wide(text);
    peripherals::Store store;
    if (!store.Enabled(peripherals::Module::Inbox)) throw std::runtime_error("Mailbox inbox disabled");
    const auto title = args.value("title", "");
    store.Save({{"module", "inbox"}, {"title", title.empty() ? "本地网关收件" : title},
        {"body", text}, {"format", format}, {"kind", "text"}, {"source", args.value("source", "MCP / HTTP")},
        {"unread", true}, {"review", "pending"}});
    ++gateway_revision;
  }
  Json Call(const Json& params) {
    const auto name = params.at("name").get<std::string>();
    auto args = params.value("arguments", Json::object());
    std::string output;
    if (name == "buffer_push") { Submit(args); output = "accepted into Mailbox for review"; }
    else if (name == "buffer_stream_begin") {
      std::erase_if(streams, [](const auto& item) { return GetTickCount64() - item.second.updated > 900000; });
      if (streams.size() >= 32) throw std::runtime_error("Too many streams");
      const auto id = peripherals::NewId();
      streams.emplace(id, Stream{args, {}, GetTickCount64()});
      output = Json{{"stream_id", id}}.dump();
    } else if (name == "buffer_stream_append" || name == "buffer_stream_end") {
      const auto id = args.at("stream_id").get<std::string>();
      auto found = streams.find(id);
      if (found == streams.end() || GetTickCount64() - found->second.updated > 900000)
        throw std::runtime_error("Unknown or expired stream");
      auto& stream = found->second;
      if (name == "buffer_stream_append") {
        const auto delta = args.at("delta").get<std::string>();
        if (stream.text.size() + delta.size() > kLimit) throw std::runtime_error("Stream limit exceeded");
        stream.text += delta; stream.updated = GetTickCount64(); output = "appended";
      } else {
        stream.metadata["text"] = stream.text; Submit(stream.metadata);
        streams.erase(found); output = "accepted into Mailbox for review";
      }
    } else throw std::runtime_error("Unknown tool");
    return {{"content", {{{"type", "text"}, {"text", output}}}}, {"isError", false}};
  }
  void Serve(SOCKET socket, std::stop_token stop) {
    const auto alive = [&] { return !stop.stop_requested() && enabled(); };
    const auto deadline = GetTickCount64() + 5000;
    std::string bytes;
    auto receive = [&] {
      while (alive() && GetTickCount64() < deadline) {
        fd_set readable; FD_ZERO(&readable); FD_SET(socket, &readable); timeval timeout{0, 100000};
        if (select(0, &readable, nullptr, nullptr, &timeout) <= 0) continue;
        char block[8192]; const int count = recv(socket, block, sizeof(block), 0);
        if (count <= 0) return false;
        bytes.append(block, static_cast<std::size_t>(count)); return true;
      }
      return false;
    };
    auto respond = [&](int status, const Json& body) {
      if (!alive()) return;
      const auto data = status == 202 ? std::string{} : body.dump();
      const auto response = "HTTP/1.1 " + std::to_string(status) + (status < 300 ? " OK\r\n" : " Error\r\n") +
          "Content-Type: application/json\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: " + std::to_string(data.size()) + "\r\n\r\n" + data;
      std::size_t offset = 0;
      while (offset < response.size() && alive()) {
        const auto count = send(socket, response.data() + offset, static_cast<int>(response.size() - offset), 0);
        if (count <= 0) break;
        offset += static_cast<std::size_t>(count);
      }
    };
    try {
      while (bytes.find("\r\n\r\n") == std::string::npos) {
        if (bytes.size() > 32768 || !receive()) return;
      }
      const auto end = bytes.find("\r\n\r\n"), first = bytes.find("\r\n");
      if (end > 32768) { respond(413, {{"error", "Header limit"}}); return; }
      const auto line = bytes.substr(0, first);
      const auto space = line.find(' '), last = line.rfind(' ');
      if (space == last || line.substr(last + 1) != "HTTP/1.1") throw std::runtime_error("request");
      const auto method = line.substr(0, space), path = line.substr(space + 1, last - space - 1);
      std::map<std::string, std::string> headers;
      for (auto pos = first + 2; pos < end;) {
        const auto next = bytes.find("\r\n", pos), colon = bytes.find(':', pos);
        if (colon == std::string::npos || colon >= next) throw std::runtime_error("header");
        auto name = Lower(bytes.substr(pos, colon - pos));
        if (name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") != std::string::npos)
          throw std::runtime_error("header");
        auto value = bytes.substr(colon + 1, next - colon - 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(0, 1);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.pop_back();
        if (!headers.emplace(name, value).second) throw std::runtime_error("duplicate header");
        pos = next + 2;
      }
      if (headers["host"] != "127.0.0.1:" + std::to_string(port) && headers["host"] != "localhost:" + std::to_string(port)) {
        respond(403, {{"error", "Invalid host"}}); return;
      }
      if (headers.contains("origin") && !OriginAllowed(headers.at("origin"))) { respond(403, {{"error", "Invalid origin"}}); return; }
      if (method == "GET" && path == "/v1/health") { respond(200, {{"ok", true}}); return; }
      if (method != "POST" || (path != "/mcp" && path != "/v1/inbound")) { respond(404, {{"error", "Not found"}}); return; }
      auto expected = "Bearer " + (fixture_token.empty() ? Token() : fixture_token);
      const bool authorized = Equal(headers["authorization"], expected);
      peripherals::Wipe(expected); peripherals::Wipe(headers["authorization"]);
      if (!authorized) { respond(401, {{"error", "Unauthorized"}}); return; }
      if (headers.contains("transfer-encoding") || !headers.contains("content-length")) throw std::runtime_error("length");
      const auto length = headers.at("content-length"); std::size_t size = 0;
      const auto parsed = std::from_chars(length.data(), length.data() + length.size(), size);
      if (parsed.ec != std::errc{} || parsed.ptr != length.data() + length.size()) throw std::runtime_error("length");
      if (size > kLimit) { respond(413, {{"error", "Body limit"}}); return; }
      while (bytes.size() < end + 4 + size) if (!receive()) return;
      const auto json = Json::parse(bytes.substr(end + 4, size), [](int depth, Json::parse_event_t, Json&) {
        if (depth > 16) throw std::runtime_error("depth"); return true;
      });
      if (!alive()) return;
      if (path == "/v1/inbound") { Submit(json); respond(200, {{"accepted", true}, {"disposition", "mailbox"}}); return; }
      if (!json.is_object() || json.value("jsonrpc", "") != "2.0") throw std::runtime_error("rpc");
      if (!json.contains("id")) { respond(202, {}); return; }
      const auto rpc = json.at("method").get<std::string>(); Json result;
      if (rpc != "initialize" && headers.contains("mcp-protocol-version")) {
        const auto version = headers.at("mcp-protocol-version");
        if (version != "2024-11-05" && version != "2025-03-26" && version != "2025-06-18")
          throw std::runtime_error("protocol version");
      }
      if (rpc == "initialize") {
        auto version = json.value("params", Json::object()).value("protocolVersion", "2025-06-18");
        if (version != "2024-11-05" && version != "2025-03-26" && version != "2025-06-18") version = "2025-06-18";
        result = {{"protocolVersion", version}, {"capabilities", {{"tools", {{"listChanged", false}}}}},
            {"serverInfo", {{"name", "RIMES"}, {"version", "1"}}}, {"instructions", "Give-only Mailbox ingress; user review required."}};
      } else if (rpc == "ping") result = Json::object();
      else if (rpc == "tools/list") result = {{"tools", ToolList()}};
      else if (rpc == "tools/call") {
        try { result = Call(json.at("params")); }
        catch (...) { result = {{"isError", true}, {"content", {{{"type", "text"}, {"text", "Request rejected: check text, stream, format or Inbox availability."}}}}}; }
      } else { respond(200, {{"jsonrpc", "2.0"}, {"id", json["id"]}, {"error", {{"code", -32601}, {"message", "Method not found"}}}}); return; }
      respond(200, {{"jsonrpc", "2.0"}, {"id", json["id"]}, {"result", result}});
    } catch (...) { respond(400, {{"error", "Invalid request or Inbox unavailable"}}); }
  }
  void Run(std::stop_token stop) {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data)) { gateway_state = -1; return; }
    struct Cleanup { ~Cleanup() { WSACleanup(); } } cleanup;
    Socket listener;
    while (!stop.stop_requested()) {
      if (!enabled()) { listener.Reset(); streams.clear(); gateway_state = 0; Sleep(100); continue; }
      if (listener.value == INVALID_SOCKET) {
        listener.value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        BOOL exclusive = TRUE;
        setsockopt(listener.value, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(port); address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listener.value, reinterpret_cast<sockaddr*>(&address), sizeof(address)) || listen(listener.value, 8)) {
          listener.Reset(); gateway_state = -1; Sleep(100); continue;
        }
        gateway_state = 1;
      }
      fd_set set; FD_ZERO(&set); FD_SET(listener.value, &set); timeval timeout{0, 100000};
      if (select(0, &set, nullptr, nullptr, &timeout) <= 0) continue;
      Socket client; client.value = accept(listener.value, nullptr, nullptr);
      if (client.value == INVALID_SOCKET) continue;
      DWORD send_timeout = 1000; setsockopt(client.value, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&send_timeout), sizeof(send_timeout));
      Serve(client.value, stop);
    }
    gateway_state = 0;
  }
};
LocalGateway::LocalGateway(std::function<bool()> enabled, unsigned short port, std::string token)
    : impl_(std::make_unique<Impl>(std::move(enabled), port, std::move(token))) {}
LocalGateway::~LocalGateway() = default;
unsigned long long GatewayRevision() { return gateway_revision.load(); }
std::wstring GatewayStatus() {
  return gateway_state == 1 ? L"本地网关运行中 · 127.0.0.1:47700"
      : gateway_state == -1 ? L"本地端口不可用，请检查 47700 端口占用。" : L"本地网关已关闭";
}
std::wstring GatewayConfiguration(unsigned client, bool copy, bool supplementary) {
  auto token = copy ? Token() : "<RIMES_GATEWAY_TOKEN>";
  std::string config;
  if (client == 1) config = supplementary ? "$env:RIMES_GATEWAY_TOKEN = '" + token + "'"
      : "[mcp_servers.rimes]\nurl = \"http://127.0.0.1:47700/mcp\"\nbearer_token_env_var = \"RIMES_GATEWAY_TOKEN\"";
  else if (client == 2 && supplementary) config = "claude mcp add --transport http rimes http://127.0.0.1:47700/mcp --header 'Authorization: Bearer " + token + "'";
  else config = Json{{"mcpServers", {{"rimes", {{"type", "http"}, {"url", "http://127.0.0.1:47700/mcp"},
      {"headers", {{"Authorization", "Bearer " + token}}}}}}}}.dump(2);
  peripherals::Wipe(token);
  auto result = Wide(config);
  peripherals::Wipe(config);
  return result;
}
}
