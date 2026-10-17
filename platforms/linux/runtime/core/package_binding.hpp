#pragma once
#include "plugin_store.hpp"

namespace rimes::plugins {
// The event-thread bridge between persisted package receipts and live adapters.
// Filesystem events refresh it eagerly; accepting a worker result also checks
// the receipt synchronously so event ordering cannot restore a revoked grant.
class PackageBinding {
public:
    PackageBinding(PackageStore& store, Registry& registry);
    ~PackageBinding();
    void Refresh();
    void Revoke() noexcept;
    bool Accepts(const Lease& lease);
    bool Manages(std::string_view id) const;
private:
    PackageStore& store_;
    Registry& registry_;
    std::map<std::string, std::string, std::less<>> grants_;
    bool active_ = true;
};
} // namespace rimes::plugins
