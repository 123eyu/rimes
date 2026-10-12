#pragma once
#include "provider.hpp"
namespace rimes::windows::workbench {
bool LocalTranslationReady();
bool GenerateLocalTranslation(const Settings&, const Generation&,
    const std::function<bool(const std::string&)>&,
    const std::function<bool()>&, std::string*);
}
