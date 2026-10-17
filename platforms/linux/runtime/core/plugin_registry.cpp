#include "plugin_registry.hpp"

#include <algorithm>
#include <random>
#include <stdexcept>
#include <utility>

namespace rimes::plugins {
namespace {
std::string NewLifetime() {
    std::random_device random;
    constexpr char digits[] = "0123456789abcdef";
    std::string id;
    for (int word = 0; word != 4; ++word) {
        auto value = random();
        for (int nibble = 0; nibble != 8; ++nibble) {
            id += digits[value & 15];
            value >>= 4;
        }
    }
    return id;
}
}

std::string_view KindName(Kind kind) {
    switch (kind) {
    case Kind::Buffer: return "buffer";
    case Kind::Capsule: return "capsule";
    case Kind::Mailbox: return "mailbox";
    case Kind::Extension: return "extension";
    }
    throw std::invalid_argument("Unknown plugin kind");
}

Registry::Registry() : lifetime_(NewLifetime()) {}
Registry::~Registry() { Shutdown(); }

bool Registry::ValidId(std::string_view id) {
    return id.size() > 8 && id.size() <= 128 && id.substr(0, 8) == "builtin." &&
        id.find("..") == std::string_view::npos && id.back() != '.' &&
        std::all_of(id.begin(), id.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
        });
}

void Registry::Register(Descriptor descriptor, Lifecycle lifecycle) {
    if (stopped_ || !ValidId(descriptor.id) || descriptor.name.empty() ||
        descriptor.version.empty() || descriptor.capabilities.empty()) {
        throw std::invalid_argument("Invalid compiled plugin descriptor");
    }
    (void)KindName(descriptor.kind);
    const auto id = descriptor.id;
    if (entries_.count(id)) throw std::invalid_argument("Duplicate compiled plugin");
    entries_.emplace(id, Entry{State{std::move(descriptor), false, false, 1}, std::move(lifecycle)});
}

void Registry::Revoke(Entry& entry) noexcept {
    auto& state = entry.state;
    ++state.generation;
    if (selected_buffer_ == state.descriptor.id) selected_buffer_.clear();
    const bool was_enabled = state.enabled;
    state.enabled = false; // revoke before callbacks can observe the state
    if (was_enabled && entry.lifecycle.stop) {
        try { entry.lifecycle.stop(); } catch (...) { /* already revoked */ }
    }
}

bool Registry::ApplyState(std::string_view id, bool installed, bool enabled) {
    auto found = entries_.find(id);
    if (stopped_ || found == entries_.end() || (enabled && !installed)) return false;
    auto& entry = found->second;
    if (entry.state.installed == installed && entry.state.enabled == enabled) return true;
    Revoke(entry);
    entry.state.installed = installed;
    if (enabled) {
        try {
            if (entry.lifecycle.start) entry.lifecycle.start();
            entry.state.enabled = true;
        } catch (...) {
            // A partially started adapter must unwind observers/processes too.
            if (entry.lifecycle.stop) { try { entry.lifecycle.stop(); } catch (...) {} }
            return false;
        }
    }
    return true;
}

bool Registry::SelectBuffer(std::string_view id) {
    if (stopped_) return false;
    if (id == selected_buffer_) return true;
    auto next = entries_.find(id);
    if (!id.empty() && (next == entries_.end() || !next->second.state.enabled ||
                       next->second.state.descriptor.kind != Kind::Buffer)) return false;
    if (auto old = entries_.find(selected_buffer_); old != entries_.end()) ++old->second.state.generation;
    selected_buffer_ = std::string(id);
    if (next != entries_.end()) ++next->second.state.generation;
    return true;
}

void Registry::Invalidate(std::string_view id) {
    if (auto entry = entries_.find(id); entry != entries_.end()) ++entry->second.state.generation;
}

void Registry::Shutdown() noexcept {
    if (stopped_) return;
    stopped_ = true;
    for (auto& [id, entry] : entries_) { (void)id; Revoke(entry); }
}

std::vector<State> Registry::Entries() const {
    std::vector<State> states;
    for (const auto& [id, entry] : entries_) { (void)id; states.push_back(entry.state); }
    return states;
}

const State* Registry::Find(std::string_view id) const {
    const auto entry = entries_.find(id);
    return entry == entries_.end() ? nullptr : &entry->second.state;
}

std::optional<Lease> Registry::Authorize(std::string_view id, std::string_view capability) const {
    const auto* state = Find(id);
    if (stopped_ || !state || !state->enabled || !state->installed ||
        !state->descriptor.capabilities.count(std::string(capability)) ||
        (state->descriptor.kind == Kind::Buffer && selected_buffer_ != id)) return std::nullopt;
    return Lease{lifetime_, state->generation, std::string(id), std::string(capability)};
}

bool Registry::Accepts(const Lease& lease) const {
    auto current = Authorize(lease.plugin_id, lease.capability);
    return current && current->registry == lease.registry && current->generation == lease.generation;
}

} // namespace rimes::plugins
