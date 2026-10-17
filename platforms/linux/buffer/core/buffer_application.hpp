#pragma once
#include <filesystem>
#include <string>
#include <string_view>
namespace rimes::buffer {
inline bool IsCompanionApplication(std::string_view program) {
    auto name = std::filesystem::path(std::string(program)).filename().string();
    if (name.size() > 8 && name.compare(name.size() - 8, 8, ".desktop") == 0) name.resize(name.size() - 8);
    for (const auto* component : {"settings", "buffer", "capsule", "mailbox", "schemes"})
        if (name == std::string("rimes-") + component || name == std::string("org.scholay.rimes.") + component) return true;
    return false;
}
}
