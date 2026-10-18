#pragma once
#include <functional>
#include <memory>
#include <string>

namespace rimes::windows::workbench {
// Local, give-only ingress. It can create a review item, never read private
// content or obtain the TSF lease needed to deliver into another application.
class LocalGateway {
 public:
  explicit LocalGateway(std::function<bool()> enabled, unsigned short port = 47700,
                        std::string fixture_token = {});
  ~LocalGateway();
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
std::wstring GatewayConfiguration(unsigned client, bool copy = false,
                                  bool supplementary = false);
std::wstring GatewayStatus();
unsigned long long GatewayRevision();
}
