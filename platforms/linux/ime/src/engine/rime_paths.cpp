#include "rime_paths.hpp"
#include "scheme_import.hpp"

#include <cstdlib>
#include <system_error>

namespace rimes::linuxime {
namespace {

std::filesystem::path EnvPath(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] != '/') {
        return {};
    }
    return std::filesystem::path(value);
}

std::filesystem::path HomeDir() {
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] != '/') {
        return {};
    }
    return std::filesystem::path(home);
}

std::filesystem::path XdgDir(const char* name, const char* fallback_under_home) {
    if (auto override_path = EnvPath(name); !override_path.empty()) {
        return override_path;
    }
    const auto home = HomeDir();
    if (home.empty()) {
        return {};
    }
    return home / fallback_under_home;
}

}  // namespace

bool LooksLikeSharedData(const std::filesystem::path& path) noexcept {
    std::error_code error;
    return std::filesystem::is_regular_file(path / "default.yaml", error);
}

std::filesystem::path SchemeLibraryRoot() {
    auto data = XdgDir("XDG_DATA_HOME", ".local/share");
    if (data.empty()) throw std::runtime_error("无法确定用户方案目录");
    return data / "rimes" / "schemes";
}

EnginePaths ResolveEnginePaths(const std::filesystem::path& install_shared_dir) {
    EnginePaths paths;
    if (auto shared = EnvPath("RIMES_SHARED_DIR"); !shared.empty()) {
        paths.shared_data_dir = shared;
    } else if (!install_shared_dir.empty() && LooksLikeSharedData(install_shared_dir)) {
        paths.shared_data_dir = install_shared_dir;
    }

    if (auto user = EnvPath("RIMES_USER_DIR"); !user.empty()) {
        paths.user_data_dir = user;
    } else {
        paths.user_data_dir = XdgDir("XDG_DATA_HOME", ".local/share") / "rimes";
    }

    if (auto log = EnvPath("RIMES_LOG_DIR"); !log.empty()) {
        paths.log_dir = log;
    } else {
        const auto state = XdgDir("XDG_STATE_HOME", ".local/state");
        paths.log_dir = state.empty() ? paths.user_data_dir / "log" : state / "rimes" / "log";
    }
    // Explicit test/development roots remain independent of the user's library.
    if (EnvPath("RIMES_SHARED_DIR").empty() && EnvPath("RIMES_USER_DIR").empty()) {
        if (auto selected = rimes::schemes::Library(SchemeLibraryRoot()).Active()) {
            paths.shared_data_dir = selected->resources;
            paths.user_data_dir = selected->user;
            paths.imported_schema = selected->schema;
        }
    }
    return paths;
}

bool EnsureDirectory(const std::filesystem::path& path, std::string* error) noexcept {
    try {
        if (path.empty() || !path.is_absolute()) {
            if (error != nullptr) {
                error->assign("directory must be an absolute path");
            }
            return false;
        }
        std::error_code filesystem_error;
        std::filesystem::create_directories(path, filesystem_error);
        if (filesystem_error) {
            if (error != nullptr) {
                error->assign("could not create directory: " + filesystem_error.message());
            }
            return false;
        }
        return true;
    } catch (...) {
        if (error != nullptr) {
            error->assign("exception while creating a directory");
        }
        return false;
    }
}

}  // namespace rimes::linuxime
