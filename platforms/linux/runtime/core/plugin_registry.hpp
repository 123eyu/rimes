#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace rimes::plugins {

enum class Kind { Buffer, Capsule, Mailbox, Extension };
std::string_view KindName(Kind kind);

struct Descriptor {
    std::string id;
    std::string name;
    std::string version;
    Kind kind;
    std::set<std::string> capabilities;
};

struct State {
    Descriptor descriptor;
    bool installed = false;
    bool enabled = false;
    std::uint64_t generation = 0;
};

// An asynchronous result is valid only for the exact registry lifetime and
// grant that issued it. Source revision and focused IC are checked separately
// by the workbench, immediately before a native commit.
struct Lease {
    std::string registry;
    std::uint64_t generation = 0;
    std::string plugin_id;
    std::string capability;
};

struct Lifecycle {
    std::function<void()> start;
    std::function<void()> stop;
};

// Owned and mutated on the Fcitx event thread. Worker threads receive immutable
// leases; they must post results back rather than touching this registry.
// Register is exclusively for compiled native adapters, never downloaded code.
class Registry {
public:
    Registry();
    ~Registry();
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;

    void Register(Descriptor descriptor, Lifecycle lifecycle = {});
    bool ApplyState(std::string_view id, bool installed, bool enabled);
    bool SelectBuffer(std::string_view id); // empty means Default
    void Invalidate(std::string_view id);
    void Shutdown() noexcept;

    std::vector<State> Entries() const;
    const State* Find(std::string_view id) const;
    const std::string& SelectedBuffer() const { return selected_buffer_; }
    std::optional<Lease> Authorize(std::string_view id, std::string_view capability) const;
    bool Accepts(const Lease& lease) const;

private:
    struct Entry { State state; Lifecycle lifecycle; };
    void Revoke(Entry& entry) noexcept;
    static bool ValidId(std::string_view id);
    std::map<std::string, Entry, std::less<>> entries_;
    std::string selected_buffer_;
    std::string lifetime_;
    bool stopped_ = false;
};

} // namespace rimes::plugins
