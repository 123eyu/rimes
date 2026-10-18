#include "codex_cli.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
using namespace rimes::windows::workbench;
void Check(bool ok, const char* reason) { if (!ok) { std::cerr << reason << '\n'; std::exit(1); } }
int main(int argc, char** argv) {
  Check(argc == 2, "fixture path required");
  const std::string events = "{\"type\":\"turn.started\"}\n{\"type\":\"item.completed\",\"item\":{\"type\":\"agent_message\",\"text\":\"你好💡\"}}\n{\"type\":\"turn.completed\"}";
  CodexJsonDecoder bytewise;
  for (const auto c : events) Check(bytewise.Feed(std::string_view(&c, 1)), "split UTF-8 JSONL");
  Check(bytewise.Finish() && bytewise.Text() == "你好💡", "authoritative Unicode final");
  for (const auto* line : {"{\"type\":\"turn.completed\"}\n", "{\"type\":\"turn.failed\"}\n", "broken\n",
      "{\"type\":\"item.started\",\"item\":{\"type\":\"mcp_tool_call\"}}\n"}) {
    CodexJsonDecoder decoder;
    Check(!decoder.Feed(line) && !decoder.Finish(), "reject malformed, failed, tool, missing final");
  }
  CodexJsonDecoder overflow;
  Check(!overflow.Feed(std::string(4 * 1024 * 1024 + 1, 'x')) && !overflow.Finish(), "bound output");
  Check(QuoteWindowsArgument(L"a b\\\"c\\") == L"\"a b\\\\\\\"c\\\\\"", "Windows quote/backslash rules");
  auto workspace = std::filesystem::temp_directory_path() /
      (L"rimes-codex-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
  std::filesystem::create_directory(workspace);
  const auto args = CodexTextArguments(workspace, "fixture model");
  Check(std::find(args.begin(), args.end(), L"--ignore-user-config") != args.end() &&
      std::find(args.begin(), args.end(), L"--strict-config") != args.end() &&
      std::find(args.begin(), args.end(), L"--ephemeral") != args.end() && args.back() == L"-", "isolated stdin invocation");
  const auto executable = std::filesystem::absolute(std::filesystem::path(Wide(argv[1])));
  for (const auto* mode : {L"success", L"malformed", L"tool", L"unfinished", L"nonzero", L"stderr", L"delay", L"blocked", L"cancel"}) {
    std::cout << "Process scenario: " << Utf8(mode) << std::endl;
    std::string result, error;
    const auto start = GetTickCount64(); const std::wstring value(mode);
    const bool ok = RunCodexTextProcess(executable, {value == L"cancel" ? L"delay" : value}, workspace,
        value == L"blocked" ? std::string(Model::kLimit, 'x') : "synthetic source & ; \"中文💡\"",
        [&](const auto& text) { result += text; return true; },
        [&] { return value == L"cancel" && GetTickCount64() - start > 50; }, &error, 500);
    Check(ok == (value == L"success"), "process success/failure contract");
    Check(result == (ok ? "你好💡" : ""), "never expose failed, partial, reasoning, or tool output");
    Check(GetTickCount64() - start < 2500, "prompt writer and process tree cancellation bounded");
    std::cout << "Scenario passed: " << Utf8(mode) << std::endl;
  }
  // Isolate the control/login home too; never touch an installed authorization.
  const auto previous_size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
  std::wstring previous(previous_size, 0);
  if (previous_size) { GetEnvironmentVariableW(L"LOCALAPPDATA", previous.data(), previous_size); previous.resize(wcslen(previous.c_str())); }
  Check(SetEnvironmentVariableW(L"LOCALAPPDATA", workspace.c_str()) != FALSE, "isolate login home");
  for (const auto* mode : {L"login-output",L"login-cancel"}) {
    std::string output, error; bool early = false;
    const auto start = GetTickCount64();
    const bool ok = RunCodexControl(executable, {mode},
        [&] { return std::wstring(mode) == L"login-cancel" && output.find("code_challenge") != output.npos; }, &error, 1500,
        [&](std::string_view bytes) { output.append(bytes); early = early || (output.find("code_challenge") != output.npos && output.find("login-complete") == output.npos); });
    Check(ok == (std::wstring(mode) == L"login-output") && early && GetTickCount64() - start < 2500,
        "login URL streams before process exit and cancellation retires the process");
    if (ok) Check(output.find("login-complete") != output.npos, "control transport drains final stdout");
  }
  SetEnvironmentVariableW(L"LOCALAPPDATA", previous.empty() ? nullptr : previous.c_str());
  for (const auto* child : {L"RIMES/ai/codex-home", L"RIMES/ai", L"RIMES"})
    Check(std::filesystem::remove(workspace / child), "remove empty synthetic login home");
  const auto app_args = CodexAppServerArguments(workspace);
  Check(std::find(app_args.begin(), app_args.end(), L"app-server") != app_args.end() &&
      std::find(app_args.begin(), app_args.end(), L"exec") == app_args.end(), "real app-server delta protocol selected");
  for (const auto* mode : {L"app-success", L"app-mcp", L"app-model", L"app-tool", L"app-malformed", L"app-unfinished", L"app-delay"}) {
    Generation job; job.model = "fixture-model"; job.effort = "high";
    std::string summary, output, error; bool observed_early = false;
    job.progress = [&](bool public_summary, const std::string& text) { if (public_summary) summary = text; return true; };
    const auto start = GetTickCount64();
    const bool ok = RunCodexStreamingProcess(executable, {mode}, workspace, "synthetic source", job,
        [&](const std::string& text) { output = text; if (text == "你好。") observed_early = true; return true; },
        [&] { return std::wstring(mode) == L"app-delay" && GetTickCount64()-start > 180; }, &error, 1500);
    Check(ok == (std::wstring(mode) == L"app-success"), "app-server terminal turn authorizes success");
    if (ok) Check(observed_early && output == "你好。第二块💡。" && summary == "公开摘要", "incremental body and public summary only");
    if (std::wstring(mode) == L"app-mcp" || std::wstring(mode) == L"app-model")
      Check(output.empty() && summary.empty(), "MCP or model mismatch rejected before prompt transmission");
    Check(GetTickCount64()-start < 2500, "app-server cancellation retires writer and process tree");
  }
  std::string warm_first, warm_second, warm_error;
  const auto warm = [&](const std::wstring& mode, const std::string& input, std::string& output,
                        const std::function<bool()>& cancelled, unsigned timeout = 500) {
    return RunWarmLocalTextProcess(executable, {mode}, workspace, input,
        [&](const auto& text) { output = text; return true; }, cancelled, &warm_error, timeout);
  };
  Check(warm(L"worker", "first", warm_first, [] { return false; }), "warm worker first response");
  Check(warm(L"worker", "different", warm_second, [] { return false; }), "warm worker second response");
  Check(warm_first.substr(0, warm_first.find(':')) == warm_second.substr(0, warm_second.find(':')) &&
        warm_first.ends_with(":1:first") && warm_second.ends_with(":2:different"), "same process, distinct requests, no cached response");
  const auto warm_pid = std::stoul(warm_first);
  std::string ignored;
  Check(!warm(L"worker", "cancel", ignored, [] { return true; }), "cancel retires warm worker");
  const auto retired = OpenProcess(SYNCHRONIZE, FALSE, warm_pid);
  Check(!retired || WaitForSingleObject(retired, 0) == WAIT_OBJECT_0, "cancel leaves no old worker alive");
  if (retired) CloseHandle(retired);
  for (const auto* mode : {L"worker-delay", L"worker-blocked", L"worker-invalid"}) {
    const auto started = GetTickCount64(); ignored.clear();
    Check(!warm(mode, mode == std::wstring(L"worker-blocked") ? std::string(Model::kLimit, 'x') : "input", ignored,
        [&] { return GetTickCount64() - started > 100; }), "worker cancellation/protocol failure rejected");
    Check(ignored.empty() && GetTickCount64() - started < 2500, "worker cancellation unblocks pipe writer without publishing output");
  }
  Check(warm(L"worker", "restart", warm_second, [] { return false; }) && warm_second.ends_with(":1:restart"),
        "worker recovers after cancellation and protocol failure");
  Check(!warm(L"worker", "", ignored, [] { return true; }), "retire test worker before directory cleanup");
  bool rejected = false;
  // PowerShell needs PATHEXT to invoke and wait for a native .exe. Without it
  // the translation installer can launch Python as a document, then fail its
  // self-check and try removing files while that orphan is still running.
  wchar_t system[MAX_PATH]{};
  Check(GetSystemDirectoryW(system, MAX_PATH) != 0, "find native Windows shell");
  std::string native_output, native_error;
  Check(RunLocalTextProcess(std::filesystem::path(system) / L"WindowsPowerShell/v1.0/powershell.exe",
      {L"-NoProfile", L"-NonInteractive", L"-Command",
       L"if (!$env:PATHEXT -or !$env:ComSpec) { exit 12 }; & $env:ComSpec /d /c 'echo native-ready'; exit $LASTEXITCODE"},
      workspace, {}, [&](const auto& text) { native_output = text; return true; },
      [] { return false; }, &native_error, 10000) && native_output == "native-ready\r\n",
      "isolated resource shell waits for native subprocess completion");
  try { (void)ResolveCodex("C:/missing/codex.cmd"); } catch (...) { rejected = true; }
  Check(rejected, "explicit .cmd path fails closed without fallback");
  std::error_code cleanup_error;
  Check(std::filesystem::remove(workspace, cleanup_error) && !cleanup_error, "cancel waits for child exit and releases workspace");
  std::cout << "Codex decoder, isolation, Unicode, pipe, cancel, timeout, and failure tests passed\n";
}
