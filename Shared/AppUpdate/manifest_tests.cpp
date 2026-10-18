#include "manifest.hpp"
#include <iostream>
#include <functional>

int main() {
    using namespace rimes::updates;
    int count = 0;
    auto require = [&](bool result) { ++count; if (!result) throw std::runtime_error("Update contract regression"); };
    auto rejects = [&](const std::function<void()>& call) { bool threw = false; try { call(); } catch (...) { threw = true; } require(threw); };
    nlohmann::json manifest = {{"schemaVersion", 1}, {"version", "1.2.10"}, {"platforms", {{"windows", {
        {"url", "https://github.com/scholay/xsime/releases/download/v1.2.10/RIMES-Windows-1.2.10.exe"},
        {"sha256", std::string(64, 'a')}, {"size", 100u}, {"architecture", "x64-x86"}}}}}};
    const auto asset = Parse(manifest.dump(), "windows");
    require(Version(asset.version) > Version("1.2.9"));
    require(Version("1.2.9") < Version("1.3.0"));
    for (const auto* version : {"1.2", "01.2.3", "1.2.3-preview.1", "1.2.3/evil", "9999999999999.0.0", "-1.0.0"})
        rejects([&] { (void)Version(version); });
    Verify(asset, 100, std::string(64, 'a'));
    rejects([&] { Verify(asset, 101, std::string(64, 'a')); });
    rejects([&] { Verify(asset, 100, std::string(64, 'b')); });
    for (const auto* url : {"http://github.com/scholay/xsime/releases/download/v1.2.10/RIMES-Windows-1.2.10.exe", "https://github.com/attacker/xsime/releases/download/v1.2.10/RIMES-Windows-1.2.10.exe", "https://github.com/scholay/xsime/releases/download/v1.2.9/RIMES-Windows-1.2.9.exe", "https://github.com/scholay/xsime/releases/download/v1.2.10/RIMES-Windows-1.2.10.exe?redirect=1"}) {
        auto changed = manifest; changed["platforms"]["windows"]["url"] = url;
        rejects([&] { (void)Parse(changed.dump(), "windows"); });
    }
    for (const auto size : {0ll, -1ll, 1024ll * 1024 * 1024}) {
        auto changed = manifest; changed["platforms"]["windows"]["size"] = size;
        rejects([&] { (void)Parse(changed.dump(), "windows"); });
    }
    manifest["schemaVersion"] = 2;
    rejects([&] { (void)Parse(manifest.dump(), "windows"); });
    std::cout << "Application update contract checks: " << count << '\n';
}
