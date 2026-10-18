#include "chat_codec.hpp"
#include "codex_channel.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace rimes::runtime {
namespace {
using Executable = VerifiedCLIExecutable;
struct Workspace {
    std::filesystem::path root;
    explicit Workspace(const std::filesystem::path& state)
        : root(state / "ai/requests" / RandomID()) {
        (void)PrivateDirectory(root / "workspace");
    }
    ~Workspace() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
};
void MountImageSkill(const std::filesystem::path& workspace) {
    std::filesystem::path source;
    for (const auto* candidate : {RIMES_INSTALLED_CODEX_SKILL, RIMES_BUILD_CODEX_SKILL})
        if (std::filesystem::is_regular_file(std::filesystem::path(candidate) / "SKILL.md")) {
            source = candidate;
            break;
        }
    if (source.empty())
        throw std::runtime_error("Bundled imagegen skill unavailable");
    std::size_t total = 0, count = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(source)) {
        if (++count > 256 || entry.is_symlink())
            throw std::runtime_error("Invalid bundled skill");
        const auto relative = entry.path().lexically_relative(source);
        const auto target = workspace / "skills/imagegen" / relative;
        if (entry.is_directory()) {
            (void)PrivateDirectory(target);
            continue;
        }
        if (!entry.is_regular_file() || entry.file_size() > 8 * 1048576 - total)
            throw std::runtime_error("Invalid bundled skill");
        std::ifstream input(entry.path(), std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(input)), {});
        if (!input.eof() && input.fail())
            throw std::runtime_error("Cannot read bundled skill");
        total += bytes.size();
        if (total > 8 * 1048576)
            throw std::runtime_error("Invalid bundled skill");
        PrivateDirectory(target.parent_path()).Write(target.filename().string(), bytes);
    }
}
std::string Preview(const std::string& raw, std::string_view separator) {
    // Reuse the API channel's semantic-block preview; partial JSON syntax and
    // reasoning never become Buffer prose.
    openai::Decoder decoder;
    const auto chunk =
        Json{{"choices", Json::array({{{"index", 0}, {"delta", {{"content", raw}}}}})}};
    decoder.Feed("data: " + chunk.dump() + "\n\n");
    return decoder.Preview(separator);
}
} // namespace
const char* CodexErrorMessage(CodexError error) {
    static const auto presentation = CodexContract().at("presentation");
    switch (error) {
    case CodexError::None:
        return "";
    case CodexError::Missing:
        return presentation.at("missing").get_ref<const std::string&>().c_str();
    case CodexError::Incompatible:
        return presentation.at("incompatible").get_ref<const std::string&>().c_str();
    case CodexError::LoginRequired:
        return presentation.at("loginRequired").get_ref<const std::string&>().c_str();
    case CodexError::Cancelled:
        return "生成已取消";
    case CodexError::Timeout:
        return "Codex 请求超时，请重试";
    case CodexError::Protocol:
        return "Codex 回复格式无效，原文已保留";
    case CodexError::TooLarge:
        return "Codex 回复过长，请缩短请求后重试";
    case CodexError::Busy:
        return "前一个请求正在停止，请稍后重试";
    }
    return "Codex 请求失败";
}
std::filesystem::path LocateCodex() {
    std::vector<std::filesystem::path> candidates;
    const auto* home = std::getenv("HOME");
    if (home && std::filesystem::path(home).is_absolute())
        for (const auto* directory :
             {".local/bin", ".npm-global/bin", ".bun/bin", ".volta/bin", ".local/share/pnpm"})
            candidates.push_back(std::filesystem::path(home) / directory / "codex");
    for (const auto* directory : {"/usr/local/bin", "/usr/bin", "/bin"})
        candidates.push_back(std::filesystem::path(directory) / "codex");
    std::istringstream paths(std::getenv("PATH") ? std::getenv("PATH") : "");
    std::string path;
    while (std::getline(paths, path, ':'))
        if (std::filesystem::path(path).is_absolute())
            candidates.push_back(std::filesystem::path(path) / "codex");
    for (const auto& candidate : candidates) {
        try {
            return Executable(candidate).path;
        } catch (...) {
        }
    }
    return {};
}
bool HasCodexCredential(const std::filesystem::path& root) {
    try {
        const auto raw = PrivateDirectory(root / "ai/codex-home").Read("auth.json", 65536);
        if (!raw)
            return false;
        const auto value = Json::parse(*raw);
        return value.value("auth_mode", "") == "chatgpt" && value.contains("tokens") &&
               value.at("tokens").is_object() &&
               !value.at("tokens").value("access_token", "").empty() &&
               !value.at("tokens").value("refresh_token", "").empty();
    } catch (...) {
        return false;
    }
}
std::vector<std::string> CodexArguments(const std::filesystem::path& workspace, bool image) {
    auto args = CodexContract()
                    .at(image ? "imageArguments" : "textArguments")
                    .get<std::vector<std::string>>();
    const std::string marker = "\"/RIMES_WORKSPACE\"";
    for (auto& arg : args) {
        // The actual native TOML builder uses JSON-compatible string escaping.
        // Replace the whole quoted value so quotes/backslashes in a real path
        // cannot become extra permissions or arguments.
        const auto offset = arg.find(marker);
        if (offset != arg.npos)
            arg.replace(offset, marker.size(), Json(workspace.string()).dump());
    }
    return args;
}
std::map<std::string, std::string> CodexEnvironment(const std::filesystem::path& executable,
                                                    const std::filesystem::path& home,
                                                    const std::filesystem::path& temporary) {
    auto environment = CLIEnvironment(executable, temporary);
    environment["CODEX_HOME"] = home.string();
    return environment;
}
void RunCodex(CodexRequest request, std::atomic<bool>& cancelled, CodexReceive receive) {
    CodexEvent completion;
    completion.finished = true;
    try {
        if (cancelled.load())
            completion.error = CodexError::Cancelled;
        else {
            const bool generating =
                request.mode == CodexMode::Text || request.mode == CodexMode::Image;
            if (generating && (request.text.empty() ||
                               request.text.size() >
                                   CodexContract().at("maximumSourceBytes").get<std::size_t>() ||
                               request.text.find('\0') != request.text.npos))
                throw std::runtime_error("Invalid request");
            if ((request.mode == CodexMode::Image && request.skill != "imagegen") ||
                (request.mode != CodexMode::Image && !request.skill.empty()))
                throw std::runtime_error("Invalid skill channel");
            (void)Json(request.text).dump();
            if (generating) {
                // One source of truth for accepted model/effort choices.
                (void)ValidateFields(PluginFields("builtin.codex-cli"), {{"model", request.model},
                                                                         {"effort", request.effort},
                                                                         {"skill", request.skill}});
            }
            const auto located = request.executable.empty() ? LocateCodex() : request.executable;
            if (located.empty())
                completion.error = CodexError::Missing;
            else {
                const Executable executable(located);
                PrivateDirectory home(request.root / "ai/codex-home");
                {
                    const auto lock = home.Lock(false);
                    home.Write("config.toml",
                               "# Managed by XSIME; dedicated ChatGPT "
                               "connector.\ncli_auth_credentials_store = "
                               "\"file\"\nforced_login_method = \"chatgpt\"\n\n[mcp_servers]\n");
                }
                Workspace temporary(request.root);
                const auto workspace = temporary.root / "workspace";
                if (request.mode == CodexMode::Image)
                    MountImageSkill(workspace);
                CLISessionOptions options;
                options.argv = CodexArguments(workspace, request.mode == CodexMode::Image);
                options.argv.insert(options.argv.begin(), executable.path.string());
                options.environment =
                    CodexEnvironment(executable.path, home.Path(), temporary.root);
                options.directory = workspace;
                options.timeout = request.timeout;
                options.maximum_output =
                    request.mode == CodexMode::Image
                        ? 32 * 1048576
                        : CodexContract().at("maximumWireBytes").get<std::size_t>();
                std::string previous;
                CodexProtocol protocol(request, workspace, [&](CodexEvent event) {
                    if (cancelled.load())
                        return;
                    if (!event.text.empty()) {
                        event.text =
                            Preview(event.text, request.prepared_prompt.empty() ? "" : "\n\n");
                        if (event.text.empty() || event.text == previous)
                            return;
                        previous = event.text;
                    }
                    receive(std::move(event));
                });
                if (!executable.Matches())
                    completion.error = CodexError::Incompatible;
                else {
                    const auto result = RunCLISession(
                        options, cancelled, protocol.Start(),
                        [&](auto bytes, auto& pending) { return protocol.Feed(bytes, pending); },
                        [&] { return protocol.Interrupt(); });
                    if (result == CLIExit::Cancelled)
                        completion.error = CodexError::Cancelled;
                    else if (result == CLIExit::Timeout)
                        completion.error = CodexError::Timeout;
                    else if (result == CLIExit::TooLarge)
                        completion.error = CodexError::TooLarge;
                    else if (protocol.Error() != CodexError::None)
                        completion.error = protocol.Error();
                    else if (result != CLIExit::Completed)
                        completion.error = CodexError::Incompatible;
                    else if (request.mode == CodexMode::Text) {
                        completion.blocks = openai::LogicalBlocks(protocol.FinalText());
                        completion.text = openai::Joined(completion.blocks);
                    } else if (request.mode == CodexMode::Image) {
                        completion.image_started = protocol.ImageStarted();
                        completion.image_path = protocol.ImagePath();
                    }
                }
            }
        }
    } catch (const ExecutableUnavailable&) {
        completion.error = CodexError::Missing;
    } catch (const StorageBusy&) {
        completion.error = CodexError::Busy;
    } catch (const openai::Failure& failure) {
        completion.error =
            failure.code == openai::Error::TooLarge ? CodexError::TooLarge : CodexError::Protocol;
    } catch (...) {
        completion.error = CodexError::Protocol;
    }
    if (cancelled.load())
        completion.error = CodexError::Cancelled;
    if (completion.error != CodexError::None) {
        completion.text.clear();
        completion.blocks.clear();
        completion.image_path.clear();
    }
    receive(std::move(completion));
}
void CodexWorker::Run(CodexRequest request, std::shared_ptr<std::atomic<bool>> cancelled,
                      CodexReceive receive) {
    const auto discarded = [receive] {
        CodexEvent event;
        event.finished = true;
        event.error = CodexError::Cancelled;
        receive(std::move(event));
    };
    if (!jobs_.Run(
            cancelled,
            [request = std::move(request), cancelled, receive] {
                RunCodex(request, *cancelled, receive);
            },
            discarded)) {
        CodexEvent event;
        event.finished = true;
        event.error = CodexError::Busy;
        receive(std::move(event));
    }
}
} // namespace rimes::runtime
