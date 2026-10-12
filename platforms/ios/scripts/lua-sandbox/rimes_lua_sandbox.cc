// RIMES mobile Lua capability boundary. MIT, Copyright 2026 scholay.
#include "rimes_lua_sandbox.h"
#include <rime_api.h>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <system_error>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace fs = std::filesystem;
namespace {
constexpr size_t kMemoryLimit = 32 * 1024 * 1024;
// The supplied scheme scans 410,710 source rows once for i-prefix exclusions;
// 10M instructions interrupted valid work at ~0.43s on the host. Keep the
// one-second wall limit while allowing that bounded source indexing to finish.
constexpr long long kInstructionLimit = 50000000;
struct Memory { size_t bytes = 0; };
struct Budget {
    unsigned depth = 0;
    long long instructions = 0;
    bool expired = false;
    std::chrono::steady_clock::time_point deadline;
};
thread_local Budget budget;
thread_local std::string lastError;

bool inside(const fs::path &path, const fs::path &root) {
    if (root.empty()) return false;
    auto p = path.begin();
    for (auto r = root.begin(); r != root.end(); ++r, ++p)
        if (p == path.end() || *p != *r) return false;
    return true;
}
fs::path resolvedPath(const fs::path &path) {
    std::error_code error;
    // weakly_canonical alone leaves dangling symlinks unresolved: a subsequent
    // fopen("w") could follow one and create a file outside the checked root.
    for (auto prefix = path; !prefix.empty();) {
        const auto status = fs::symlink_status(prefix, error);
        if (!error && fs::is_symlink(status)) {
            fs::canonical(prefix, error);
            if (error) return {};
        }
        const auto parent = prefix.parent_path();
        if (parent == prefix) break;
        prefix = parent;
    }
    error.clear();
    auto result = fs::weakly_canonical(path, error);
    return error ? fs::path() : result;
}
void *allocate(void *ud, void *pointer, size_t oldSize, size_t newSize) {
    auto memory = static_cast<Memory *>(ud);
    if (!pointer) oldSize = 0;
    if (!newSize) { std::free(pointer); memory->bytes -= oldSize; return nullptr; }
    if (newSize > kMemoryLimit || memory->bytes - oldSize > kMemoryLimit - newSize) return nullptr;
    auto next = std::realloc(pointer, newSize);
    if (next) memory->bytes = memory->bytes - oldSize + newSize;
    return next;
}
void hook(lua_State *L, lua_Debug *) {
    budget.instructions += 1000;
    rimes_lua_check_budget(L);
}
int denied(lua_State *L) { return luaL_error(L, "RIMES: this capability is unavailable in imported Lua"); }
void removeFields(lua_State *L, const char *table, std::initializer_list<const char *> fields) {
    lua_getglobal(L, table);
    for (auto field : fields) { lua_pushnil(L); lua_setfield(L, -2, field); }
    lua_pop(L, 1);
}
void denyFields(lua_State *L, const char *table, std::initializer_list<const char *> fields) {
    lua_getglobal(L, table);
    for (auto field : fields) { lua_pushcfunction(L, denied); lua_setfield(L, -2, field); }
    lua_pop(L, 1);
}
}

std::string rimes_lua_path(const std::string &name, bool write, bool sharedFallback) {
    if (name.empty() || name.find('\0') != std::string::npos) return {};
    const auto api = rime_get_api();
    const char *userValue = api->get_user_data_dir(), *sharedValue = api->get_shared_data_dir();
    if (!userValue || !*userValue) return {};
    const auto user = resolvedPath(userValue);
    const auto shared = sharedValue && *sharedValue ? resolvedPath(sharedValue) : fs::path();
    const fs::path requested(name);
    const auto target = resolvedPath(requested.is_absolute() ? requested : user / requested);
    if (target.empty() || (!inside(target, user) && (write || !inside(target, shared)))) return {};
    // Rime resolves resources from the user's data before the shared distribution.
    // Third-party Lua often names only user_data_dir; mirror that read behavior
    // without copying large source dictionaries into the extension on first open.
    if (!write && sharedFallback && inside(target, user) && !shared.empty()) {
        std::error_code error;
        if (!fs::exists(target, error) && !error) {
            const auto fallback = resolvedPath(shared / target.lexically_relative(user));
            if (!fallback.empty() && inside(fallback, shared) && fs::exists(fallback, error) && !error)
                return fallback.string();
        }
    }
    return target.string();
}

bool rimes_lua_resource_name(const std::string &name) {
    if (name.empty() || name.find("..") != std::string::npos) return false;
    return name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.") == std::string::npos;
}

bool rimes_lua_userdb_name(const std::string &name, const std::string &dbClass) {
    if (name.empty() || name.size() > 256 || name.front() == '/' || name.back() == '/' ||
        (dbClass != "userdb" && dbClass != "plain_userdb")) return false;
    // Native Rime appends the database class to this relative resource name.
    // Permit namespaces used by upstream schemes, never arbitrary file paths.
    size_t start = 0;
    for (size_t end = 0; end <= name.size(); ++end) {
        if (end != name.size() && name[end] != '/') continue;
        const auto part = name.substr(start, end - start);
        if (part == "." || !rimes_lua_resource_name(part)) return false;
        start = end + 1;
    }
    const auto filename = name + (dbClass == "userdb" ? ".userdb" : ".userdb.txt");
    const auto target = rimes_lua_path(filename, true, false);
    if (target.empty()) return false;
    std::error_code error;
    fs::create_directories(fs::path(target).parent_path(), error);
    if (error) return false;
    // Recheck after creating the parent; existing and dangling symlinks must
    // remain inside the per-package user directory, not the shared source tree.
    return !rimes_lua_path(filename, true, false).empty();
}

extern "C" FILE *rimes_lua_fopen(const char *name, const char *mode) {
    if (!name || !mode) { errno = EACCES; return nullptr; }
    const bool write = std::string(mode).find_first_of("wa+") != std::string::npos;
    const auto path = rimes_lua_path(name, write);
    if (path.empty()) { errno = EACCES; return nullptr; }
    return std::fopen(path.c_str(), mode);
}
extern "C" FILE *rimes_lua_fopen_direct(const char *name, const char *mode) {
    if (!name || !mode) { errno = EACCES; return nullptr; }
    const bool write = std::string(mode).find_first_of("wa+") != std::string::npos;
    const auto path = rimes_lua_path(name, write, false);
    if (path.empty()) { errno = EACCES; return nullptr; }
    return std::fopen(path.c_str(), mode);
}
extern "C" FILE *rimes_lua_freopen(const char *name, const char *mode, FILE *stream) {
    const auto path = name ? rimes_lua_path(name, std::string(mode).find_first_of("wa+") != std::string::npos) : "";
    if (path.empty()) { errno = EACCES; return nullptr; }
    return std::freopen(path.c_str(), mode, stream);
}
extern "C" int rimes_lua_remove(const char *name) {
    const auto path = name ? rimes_lua_path(name, true) : "";
    if (path.empty()) { errno = EACCES; return -1; }
    return std::remove(path.c_str());
}
extern "C" int rimes_lua_rename(const char *from, const char *to) {
    const auto source = from ? rimes_lua_path(from, true) : "", target = to ? rimes_lua_path(to, true) : "";
    if (source.empty() || target.empty()) { errno = EACCES; return -1; }
    return std::rename(source.c_str(), target.c_str());
}
extern "C" void rimes_lua_check_budget(lua_State *L) {
    if (!budget.depth) return;
    budget.expired = budget.expired || budget.instructions >= kInstructionLimit
        || std::chrono::steady_clock::now() >= budget.deadline;
    if (budget.expired) luaL_error(L, "RIMES: Lua execution budget exceeded");
}
extern "C" void rimes_lua_begin(lua_State *L) {
    if (budget.depth++ == 0) {
        budget.instructions = 0; budget.expired = false;
        budget.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    }
    lua_sethook(L, hook, LUA_MASKCOUNT, 1000);
}
extern "C" void rimes_lua_end() { if (budget.depth) --budget.depth; }
extern "C" const char *rimes_lua_last_error() { return lastError.c_str(); }
extern "C" void rimes_lua_clear_error() { lastError.clear(); }
extern "C" void rimes_lua_record_error(const char *message) {
    if (lastError.empty()) lastError = message ? message : "Unknown Lua error";
}
extern "C" lua_State *rimes_lua_newstate() {
    auto memory = new Memory;
    auto L = lua_newstate(allocate, memory);
    if (!L) delete memory;
    return L;
}
extern "C" void rimes_lua_close(lua_State *L) {
    if (!L) return;
    void *memory = nullptr; lua_getallocf(L, &memory);
    lua_close(L); delete static_cast<Memory *>(memory);
}

extern "C" void rimes_lua_install(lua_State *L) {
    // Safe source-location diagnostics are needed by the supplied scheme. Registry,
    // hooks and upvalue manipulation would expose the original restricted functions.
    lua_getglobal(L, "debug");
    lua_newtable(L);
    for (auto field : {"getinfo", "traceback"}) {
        lua_getfield(L, -2, field); lua_setfield(L, -2, field);
    }
    lua_pushvalue(L, -1); lua_setglobal(L, "debug");
    lua_getglobal(L, "package"); lua_getfield(L, -1, "loaded");
    lua_pushvalue(L, -3); lua_setfield(L, -2, "debug");
    lua_pop(L, 4);
    denyFields(L, "os", {"execute", "exit", "getenv", "setlocale", "tmpname"});
    denyFields(L, "io", {"popen", "tmpfile", "input", "output", "read", "write"});
    removeFields(L, "io", {"stdin", "stdout", "stderr"});
    denyFields(L, "package", {"loadlib"});
    lua_getglobal(L, "package");
    lua_pushliteral(L, ""); lua_setfield(L, -2, "cpath");
    lua_getfield(L, -1, "searchers");
    lua_pushnil(L); lua_rawseti(L, -2, 3);
    lua_pushnil(L); lua_rawseti(L, -2, 4);
    lua_pop(L, 2);
    lua_pushcfunction(L, denied); lua_setglobal(L, "print");
}
