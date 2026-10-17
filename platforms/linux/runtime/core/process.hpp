#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace rimes::runtime {
struct ProcessResult {
    bool success = false, cancelled = false, timed_out = false;
    std::string output;
};
// Runs on a worker thread. Text/credentials belong in stdin, never argv.
// Owns a process group and drains bounded stdout without blocking the GUI.
ProcessResult RunProcess(const std::vector<std::string>& command, std::string_view input,
    const std::atomic<bool>& cancelled, std::chrono::milliseconds timeout,
    std::size_t output_limit = 1024 * 1024,
    const std::function<void(std::string_view)>& output = {});
} // namespace rimes::runtime
