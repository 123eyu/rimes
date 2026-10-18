#pragma once
#include "provider.hpp"
#include <string_view>

namespace rimes::windows::workbench {
struct ConnectionProbeResult {
  bool success = false;
  std::string message;
  std::vector<std::string> models;
};
ConnectionProbeResult ProbeConnection(const ConnectionProfile&, const std::wstring& key);
bool ValidConnectionURL(const std::string& url, bool documentation = false);
bool HasConnectionSecret(const ConnectionProfile&);
// Only an official OAuth destination from the bounded CLI stream is exposed.
std::string CodexAuthorizationURL(std::string_view output);
struct ConnectorUiServices {
  std::function<bool(const ConnectionProfile&)> has_secret;
  std::function<ConnectionProbeResult(const ConnectionProfile&, const std::wstring&)> probe;
  std::function<bool(const ConnectionProfile&, const std::function<bool()>&,
      const std::function<void(const std::string&)>&, std::string*)> login;
  std::function<bool(HWND, const std::wstring&)> confirm_delete;
  std::function<bool(const std::wstring&)> open_url, copy_private;
};
ConnectorUiServices NativeConnectorUiServices();
}
