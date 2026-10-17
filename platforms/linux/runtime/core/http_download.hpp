#pragma once
#include <string>
#include <string_view>

namespace rimes::runtime {
bool OfficialDownloadURL(std::string_view url);
std::string DownloadOfficialPackage(const std::string& url);
} // namespace rimes::runtime
