#pragma once
#include "plugin_store.hpp"

namespace rimes::plugins {
std::map<std::string, Descriptor> CompiledAdapters();
Json OfficialCatalog();
std::map<std::string, std::string> BundledPackages();
} // namespace rimes::plugins
