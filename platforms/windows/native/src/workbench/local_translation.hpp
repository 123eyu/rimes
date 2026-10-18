#pragma once
#include "provider.hpp"
namespace rimes::windows::workbench {
bool LocalTranslationReady();
// Resource preparation has its own lifetime, independent of Buffer requests.
bool PrepareLocalTranslation(const std::function<bool()>& cancelled, std::string* error);
bool GenerateLocalTranslation(const Settings&, const Generation&,
    const std::function<bool(const std::string&)>&,
    const std::function<bool()>&, std::string*);
}
