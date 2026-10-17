#include "http_download.hpp"
#include "official_catalog.hpp"
#include <array>
#include <iostream>

using namespace rimes::plugins;
namespace {
Json View(const PackageView& view) {
    return {{"id", view.id}, {"name", view.name}, {"version", view.version}, {"kind", view.kind},
        {"available", view.available}, {"installed", view.installed}, {"enabled", view.enabled},
        {"bundled", view.bundled}, {"grant", view.grant}, {"error", view.error}};
}
Json Request() {
    std::string input;
    std::array<char, 8192> bytes{};
    while (std::cin) {
        std::cin.read(bytes.data(), bytes.size());
        input.append(bytes.data(), static_cast<std::size_t>(std::cin.gcount()));
        if (input.size() > 2 * 1024 * 1024) throw std::runtime_error("Request too large");
    }
    return Json::parse(input);
}
}
int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: rimes-plugins --list | --request\n"
                     "--request reads one bounded JSON object from stdin.\n"
                     "Operations: list, install, restore-bundled, enable, disable, uninstall. Mutations require id and the current grant.\n";
        return 0;
    }
    try {
        if (argc != 2 || (std::string_view(argv[1]) != "--list" && std::string_view(argv[1]) != "--request"))
            throw std::runtime_error("Invalid invocation");
        const auto request = std::string_view(argv[1]) == "--list" ? Json{{"op", "list"}} : Request();
        PackageStore store(rimes::runtime::UserRoot() / "plugins", OfficialCatalog(), BundledPackages(),
                           CompiledAdapters(), RIMES_PRODUCT_VERSION);
        const std::string op = request.at("op");
        if (op == "list") {
            Json entries = Json::array();
            for (const auto& entry : store.Entries()) entries.push_back(View(entry));
            std::cout << Json{{"ok", true}, {"plugins", entries}}.dump() << '\n';
            return 0;
        }
        const std::string id = request.at("id"), grant = request.at("grant");
        if (op == "install") {
            // Download on this companion process, never the Fcitx thread.
            const auto bytes = rimes::runtime::DownloadOfficialPackage(store.DownloadURL(id));
            store.Install(id, bytes, grant);
        } else if (op == "restore-bundled") store.RestoreBundled(id, grant);
        else if (op == "enable" || op == "disable") store.Enable(id, op == "enable", grant);
        else if (op == "uninstall") store.Uninstall(id, grant);
        else throw std::runtime_error("Unknown operation");
        std::cout << Json{{"ok", true}, {"plugin", View(store.Inspect(id))}}.dump() << '\n';
        return 0;
    } catch (const std::exception&) {
        // Parser errors may embed input fragments; never echo them to logs.
        std::cout << Json{{"ok", false}, {"error", "操作未完成：请刷新插件状态，检查兼容性、权限或网络后重试。"}}.dump() << '\n';
        return 1;
    }
}
