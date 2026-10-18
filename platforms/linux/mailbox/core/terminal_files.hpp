#pragma once
#include "mailbox_store.hpp"
#include <atomic>
namespace rimes::mailbox {
const Json& TerminalContract();
struct TerminalFile {
    std::filesystem::path path;
    std::string body, png;
};
TerminalFile ReadTerminalFile(const std::filesystem::path& path,
                              const std::atomic<bool>& cancelled);
std::vector<std::filesystem::path> TerminalFiles(const std::filesystem::path& directory,
                                                 const std::atomic<bool>& cancelled);
} // namespace rimes::mailbox
