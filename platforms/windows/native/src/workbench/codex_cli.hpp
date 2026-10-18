#pragma once
#include "provider.hpp"
#include "../core/control.hpp"
#include <filesystem>
#include <string_view>
#include <vector>

namespace rimes::windows::workbench {
// Pure protocol decoder: only a successful terminal turn can authorize text.
// Reasoning and diagnostics are discarded; tool-shaped items fail closed.
class CodexJsonDecoder {
 public:
  bool Feed(std::string_view bytes);
  bool Finish();
  const std::string& Text() const { return text_; }
 private:
  bool Line(const std::string& line);
  std::string pending_, text_;
  std::size_t total_ = 0;
  bool started_ = false, done_ = false, failed_ = false;
};
std::wstring QuoteWindowsArgument(const std::wstring& value);
std::vector<std::wstring> CodexTextArguments(const std::filesystem::path& workspace,
                                            const std::string& model, const std::string& effort = {});
std::vector<std::wstring> CodexAppServerArguments(const std::filesystem::path& workspace);
bool RunCodexStreamingProcess(const std::filesystem::path& executable,
    const std::vector<std::wstring>& arguments, const std::filesystem::path& workspace,
    const std::string& input, const Generation& job,
    const std::function<bool(const std::string&)>& snapshot,
    const std::function<bool()>& cancelled, std::string* error, unsigned timeout_ms = 120000);
std::filesystem::path ResolveCodex(const std::string& override_path);
std::filesystem::path CodexHome();
bool RunCodexControl(const std::filesystem::path& executable,
    const std::vector<std::wstring>& arguments, const std::function<bool()>& cancelled,
    std::string* error, unsigned timeout_ms = 10000,
    const std::function<void(std::string_view)>& output = {});
bool GenerateCodex(const Settings&, const Generation&,
                   const std::function<bool(const std::string&)>&,
                   const std::function<bool()>&, std::string*);
// Explicit test seam: runs the same pipe/job/timeout implementation with an
// inert fixture executable. Production always uses ResolveCodex.
bool RunCodexTextProcess(const std::filesystem::path& executable,
                         const std::vector<std::wstring>& arguments,
                         const std::filesystem::path& workspace,
                         const std::string& input,
                         const std::function<bool(const std::string&)>& chunk,
                         const std::function<bool()>& cancelled,
                         std::string* error, unsigned timeout_ms = 120000);
// Fixed local helpers share the bounded, cancellable job and pipe transport.
// Bounded JSON-line worker for offline model reuse; cancellation retires it.
bool RunWarmLocalTextProcess(const std::filesystem::path& executable,
    const std::vector<std::wstring>& arguments, const std::filesystem::path& workspace,
    const std::string& input, const std::function<bool(const std::string&)>& chunk,
    const std::function<bool()>& cancelled, std::string* error, unsigned timeout_ms = 120000);
bool RunLocalTextProcess(const std::filesystem::path& executable,
    const std::vector<std::wstring>& arguments, const std::filesystem::path& workspace,
    const std::string& input, const std::function<bool(const std::string&)>& output,
    const std::function<bool()>& cancelled, std::string* error, unsigned timeout_ms = 120000);
}
