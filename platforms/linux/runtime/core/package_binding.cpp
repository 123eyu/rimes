#include "package_binding.hpp"

namespace rimes::plugins {
PackageBinding::PackageBinding(PackageStore& store, Registry& registry)
    : store_(store), registry_(registry) {
    for (const auto& view : store_.Entries())
        if (view.available && registry_.Find(view.id)) grants_.emplace(view.id, "");
    Refresh();
}
PackageBinding::~PackageBinding() { Revoke(); }
bool PackageBinding::Manages(std::string_view id) const { return grants_.count(id); }
void PackageBinding::Refresh() {
    if (!active_) return;
    for (auto& [id, grant] : grants_) {
        const auto view = store_.Inspect(id);
        if (grant != view.grant) {
            // An update can end in the same enabled state as before. Stop the
            // old adapter before starting the new one, not just its callbacks.
            registry_.ApplyState(id, false, false);
            grant = view.grant;
        }
        registry_.ApplyState(id, view.installed, view.enabled);
    }
}
void PackageBinding::Revoke() noexcept {
    active_ = false;
    for (const auto& [id, grant] : grants_) {
        (void)grant;
        registry_.ApplyState(id, false, false);
    }
}
bool PackageBinding::Accepts(const Lease& lease) {
    const auto found = grants_.find(lease.plugin_id);
    if (!active_ || found == grants_.end()) return false;
    const auto current = store_.Inspect(lease.plugin_id);
    if (!current.enabled || current.grant != found->second) {
        Refresh();
        return false;
    }
    return registry_.Accepts(lease);
}
} // namespace rimes::plugins
