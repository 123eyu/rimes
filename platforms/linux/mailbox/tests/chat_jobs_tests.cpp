#include "capsule_hash.hpp"
#include "chat_jobs.hpp"
#include <iostream>
#include <thread>
using namespace rimes::runtime;
using namespace rimes::mailbox;
using namespace std::chrono_literals;
namespace {
void Check(bool ok, const std::string& why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void Reject(F operation) {
    bool failed = false;
    try {
        operation();
    } catch (...) {
        failed = true;
    }
    Check(failed, "invalid conversation accepted");
}
template <class F> void Wait(F condition) {
    const auto end = std::chrono::steady_clock::now() + 6s;
    while (!condition()) {
        Check(std::chrono::steady_clock::now() < end, "chat deadline");
        std::this_thread::sleep_for(10ms);
    }
}
struct Results {
    std::mutex mutex;
    ChatEvent last;
    void Add(ChatEvent event) {
        std::lock_guard lock(mutex);
        last = std::move(event);
    }
    ChatEvent Get() {
        std::lock_guard lock(mutex);
        return last;
    }
    void Clear() {
        std::lock_guard lock(mutex);
        last = {};
    }
};
void Contract() {
    const auto& native = ChatContract();
    Check(InitialChatPrompt(native.at("source")) == native.at("initial"),
          "native initial prompt differs");
    for (const auto* format : {"plain", "markdown", "json"})
        Check(ContinuationChatPrompt(native.at("turns"), format) ==
                  native.at("continuations").at(format),
              "native continuation differs");
    Reject([&] { InitialChatPrompt(std::string(262145, 'x')); });
    Reject([&] {
        ContinuationChatPrompt({{{"role", "user"}, {"content", std::string(1048576, 'x')}}});
    });
    Json row{{"messages",
              {{{"role", "user"}, {"kind", "localNote"}, {"body", "PRIVATE NOTE"}},
               {{"role", "system"}, {"kind", "status"}, {"body", "PRIVATE STATUS"}},
               {{"role", "inbound"}, {"kind", "content"}, {"body", "public response"}}}}};
    Check(ChatTurns(row, "public next").dump().find("PRIVATE") == std::string::npos &&
              ChatTurns(row).size() == 1,
          "local notes entered context");
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 4)
        return 2;
    Contract();
    const auto root = std::filesystem::temp_directory_path() / ("xsime-chat-tests-" + RandomID());
    PrivateDirectory home(root / "home");
    setenv("HOME", home.Path().c_str(), 1);
    PrivateDirectory codex(root / "ai/codex-home");
    Store store(root / "mailbox");
    ChatJobs jobs;
    Results events;
    std::atomic<bool> grant{true};
    const Commit authorize = [&](const auto& write) {
        if (grant.load())
            write();
    };
    const auto receive = [&](ChatEvent event) { events.Add(std::move(event)); };
    const auto start = [&](ChatRequest request) {
        events.Clear();
        Check(jobs.Start(request, authorize, receive), "start rejected");
    };
    const auto done = [&] {
        Wait([&] { return events.Get().finished; });
        return events.Get();
    };
    try {
        for (int channel = 0; channel < 2; ++channel) {
            auto& fixture = channel == 0 ? codex : home;
            ChatRequest request;
            request.root = root;
            request.executable = std::filesystem::absolute(argv[channel + 1]);
            request.timeout = 3s;
            request.source = {{"kind", channel == 0 ? "codexCLI" : "claudeCodeCLI"},
                              {"displayName", channel == 0 ? "ChatGPT" : "Claude"},
                              {"model", channel == 0 ? "gpt-6-sol" : "claude-opus-5-5"},
                              {"replyCapability", "aiContinuation"}};
            request.body = "公开请求";
            fixture.Write("fixture-mode", "chat-logged-out");
            const auto before = store.Read();
            start(request);
            Check(!done().admitted && store.Read().document == before.document,
                  "unavailable CLI consumed draft");
            fixture.Write("fixture-mode", "chat-text-hold");
            start(request);
            Wait([&] { return !events.Get().preview.empty(); });
            const auto id = events.Get().thread;
            Check(events.Get().admitted && !store.RecoverInterrupted(),
                  "live conversation recovered as crash");
            Check(store.Get(id).at("messages").size() == 1 &&
                      store.Get(id).dump().find("公开🙂") == std::string::npos,
                  "partial output persisted");
            Check(!jobs.Start(ChatRequest{request.source, id, ConversationRevision(store.Get(id)),
                                          "duplicate", false, root, request.executable, 3s},
                              authorize, receive),
                  "concurrent turn admitted");
            fixture.Write("release-text", "release");
            Check(done().success, "CLI initial generation failed: " + events.Get().message);
            auto row = store.Get(id);
            Check(row.at("messages").back().at("body") == "公开🙂。\n\n第二句。",
                  "native paragraph separator lost");
            request.thread = id;
            request.expected = ConversationRevision(row);
            request.body = "继续公开问题";
            fixture.Write("fixture-mode", "chat-text");
            start(request);
            Check(done().success && store.Get(id).at("messages").size() == 4,
                  "continuation failed");
            const auto prompt = *fixture.Read("last-chat-prompt", 1048576);
            Check(prompt.find("CONVERSATION_JSON:\n") != prompt.npos &&
                      prompt.find("公开🙂") != prompt.npos,
                  "continuation omitted history");
            fixture.Remove("release-text");
            fixture.Remove("text-started");
            fixture.Write("fixture-mode", "chat-text-hold");
            request.expected = ConversationRevision(store.Get(id));
            start(request);
            Wait([&] { return !events.Get().preview.empty(); });
            jobs.Cancel(id);
            Check(!done().success && store.Get(id).at("messages").size() == 5 &&
                      store.Get(id).at("generation").at("phase") == "failed",
                  "cancel persisted partial reply");
            request.retry = true;
            request.body.clear();
            request.expected = ConversationRevision(store.Get(id));
            fixture.Write("fixture-mode", "chat-text");
            start(request);
            Check(done().success && store.Get(id).at("messages").size() == 6,
                  "retry duplicated last user turn");
            request.retry = false;
            request.body = "公开撤销测试";
            request.expected = ConversationRevision(store.Get(id));
            fixture.Write("fixture-mode", "chat-text-hold");
            start(request);
            Wait([&] { return !events.Get().preview.empty(); });
            grant.store(false);
            fixture.Write("release-text", "release");
            Check(!done().success && store.Get(id).at("messages").size() == 7,
                  "revoked output persisted");
            grant.store(true);
        }
        const Json legacy_source{{"kind", "codexCLI"},
                                 {"displayName", "ChatGPT"},
                                 {"replyCapability", "aiContinuation"}};
        const auto legacy_generation = rimes::capsule::NewUuidV4();
        auto legacy =
            store.BeginGeneration(legacy_source, "旧会话", "公开旧问题", legacy_generation, "chat");
        legacy = store.CompleteGeneration(legacy.at("id"), legacy_generation, "公开旧回复");
        ChatRequest old;
        old.root = root;
        old.thread = legacy.at("id");
        old.expected = ConversationRevision(legacy);
        legacy = store.MarkRead(old.thread, rimes::mailbox::Revision(legacy));
        legacy = store.Rename(old.thread, rimes::mailbox::Revision(legacy), "公开重命名");
        Check(ConversationRevision(legacy) == old.expected,
              "read/title metadata invalidated conversation history");
        old.body = "公开旧会话继续";
        old.executable = std::filesystem::absolute(argv[1]);
        codex.Write("fixture-mode", "chat-text");
        start(old);
        Check(done().success && store.Get(old.thread).at("source").at("model") == "gpt-6-sol",
              "legacy CLI default was not pinned on reply");
        Limits limits;
        limits.messages = 2;
        Store small(root / "small", limits);
        const auto generation = rimes::capsule::NewUuidV4();
        auto full = small.BeginGeneration(legacy_source, "", "公开容量", generation, "chat");
        full = small.CompleteGeneration(full.at("id"), generation, "公开结果");
        const auto unchanged = small.Read();
        Reject([&] {
            small.BeginReply(full.at("id"), ConversationRevision(full), "公开续聊",
                             rimes::capsule::NewUuidV4());
        });
        Check(small.Read().document == unchanged.document, "capacity rejection changed history");
        ConfigurationStore config(root / "configuration");
        auto prefs = config.Load(), provider = FieldDefaults(ProviderFields());
        provider["id"] = RandomID();
        provider["name"] = "Public A";
        provider["model"] = "fixture-model";
        provider["baseURL"] = std::string(argv[3]) + "/chat";
        prefs["providers"].push_back(provider);
        prefs["activeProvider"] = provider.at("id");
        config.Save(prefs, prefs.at("revision"), {{provider.at("id"), "fixture-credential"}});
        ChatRequest api;
        api.root = root;
        api.body = "公开 API 问题";
        api.source = {{"kind", "openAICompatible"},
                      {"displayName", "Public A"},
                      {"model", "fixture-model"},
                      {"replyCapability", "aiContinuation"},
                      {"providerRoute", ProviderRoute(config.Load().at("providers")[0])}};
        start(api);
        Check(done().success, "HTTP initial failed: " + events.Get().message);
        api.thread = events.Get().thread;
        prefs = config.Load();
        auto second = provider;
        second["id"] = RandomID();
        second["model"] = "TRAP";
        prefs["providers"].push_back(second);
        prefs["activeProvider"] = second.at("id");
        config.Save(prefs, prefs.at("revision"));
        api.body = "公开 API 继续";
        api.expected = ConversationRevision(store.Get(api.thread));
        start(api);
        Check(done().success, "global selection changed a saved route");
        const auto intact = store.Get(api.thread);
        prefs = config.Load();
        prefs["providers"][0]["model"] = "TRAP";
        config.Save(prefs, prefs.at("revision"));
        api.expected = ConversationRevision(intact);
        start(api);
        Check(!done().admitted && store.Get(api.thread) == intact,
              "edited provider silently rerouted conversation");
        const auto orphan =
            store.BeginGeneration(api.source, "", "公开中断", rimes::capsule::NewUuidV4(), "chat");
        Check(store.RecoverInterrupted() &&
                  store.Get(orphan.at("id")).at("generation").at("phase") == "failed",
              "abandoned chat not recovered");
        Check(store.Read().document.dump().find("fixture-credential") == std::string::npos,
              "credential persisted in Mailbox");
        jobs.Shutdown();
        if (const auto* destination = std::getenv("RIMES_CHAT_EXPORT")) {
            const std::filesystem::path output(destination);
            Check(!std::filesystem::exists(output), "public export already exists");
            const auto document = store.Read().document;
            PrivateDirectory(output / "mailbox").Write("mailbox.json", document.dump());
            auto expected = document.at("threads");
            for (auto& row : expected) if (row.contains("generation") && row.at("generation").is_object()) row["generation"].erase("linuxOwner");
            PrivateDirectory(output).Write("linux-public-fixture.json", Json{{"purpose", "public-parity-fixture"}, {"threads", expected}}.dump());
        }
        std::filesystem::remove_all(root);
        std::cout
            << "Mailbox chat: native prompts, three transports, frozen routes, final-only "
               "persistence, live/crash ownership, cancel/revoke/retry and history bounds passed\n";
    } catch (...) {
        jobs.Shutdown();
        std::filesystem::remove_all(root);
        throw;
    }
}
