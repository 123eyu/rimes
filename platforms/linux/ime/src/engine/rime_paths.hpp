#pragma once

#include <filesystem>
#include <string>

namespace rimes::linuxime {

struct EnginePaths {
    std::filesystem::path shared_data_dir;
    std::filesystem::path user_data_dir;
    std::filesystem::path log_dir;
    std::string imported_schema;
    std::string imported_package;
};

// Resolves isolated RIMES directories. Environment overrides win:
//   RIMES_SHARED_DIR, RIMES_USER_DIR, RIMES_LOG_DIR
// User data defaults to $XDG_DATA_HOME/rimes — never ~/.local/share/fcitx5/rime
// — so stock fcitx5-rime and this frontend do not share LevelDB locks.
EnginePaths ResolveEnginePaths(const std::filesystem::path& install_shared_dir = {});

std::filesystem::path SchemeLibraryRoot();

bool EnsureDirectory(const std::filesystem::path& path, std::string* error) noexcept;

// True when the directory looks like a staged RIMES SharedSupport root.
bool LooksLikeSharedData(const std::filesystem::path& path) noexcept;

}  // namespace rimes::linuxime
