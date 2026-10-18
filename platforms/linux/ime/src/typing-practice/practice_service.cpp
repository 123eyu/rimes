#include "practice_service.hpp"
#include "chord/chord_service.hpp"
#include "fcitx/rimes_ime.hpp"
#include "fcitx/rimes_state.hpp"
#include "practice_protocol.hpp"
#include "session/protected_input.hpp"
#include "typing_practice_core.hpp"
#include <fcitx-utils/dbus/bus.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/misc.h>
#include <filesystem>
#include <set>
#include <unistd.h>

namespace fcitx {
namespace pt = rimes::practice;
struct PracticeService::Impl {
    RimesIme &ime;
    bool enabled = false, active = false;
    InputContext *context = nullptr;
    std::string sender, token, reason, source, schema;
    pt::Json frozen, backspaces = pt::Json::array();
    std::set<std::pair<std::uint32_t, std::uint16_t>> seen;
    int chords = 0;
    std::uint64_t expiry = 0, chord_revision = 0, chord_duration = 0;
    std::unique_ptr<dbus::Bus> bus;
    std::unique_ptr<dbus::Slot> service;
    std::unique_ptr<EventSourceTime> timer;
    std::vector<std::unique_ptr<HandlerTableEntry<EventHandler>>> watchers;
    explicit Impl(RimesIme &host) : ime(host) {
        for (const auto type :
             {EventType::InputContextFocusOut, EventType::InputContextDestroyed,
              EventType::InputContextSwitchInputMethod, EventType::InputContextCapabilityChanged})
            watchers.push_back(ime.instance()->watchEvent(
                type, EventWatcherPhase::PreInputMethod, [this, type](Event &event) {
                    if (type == EventType::InputContextCapabilityChanged &&
                        !ProtectedInput(static_cast<CapabilityChangedEvent &>(event).newFlags()))
                        return;
                    if (static_cast<InputContextEvent &>(event).inputContext() == context)
                        Boundary(type == EventType::InputContextSwitchInputMethod
                                     ? "inputSourceChanged"
                                     : "focusLost");
                }));
        watchers.push_back(ime.instance()->watchEvent(
            EventType::InputContextFocusIn, EventWatcherPhase::PreInputMethod,
            [this](Event &event) {
                if (context && static_cast<InputContextEvent &>(event).inputContext() != context)
                    Boundary("focusLost");
            }));
        try {
            bus = std::make_unique<dbus::Bus>(dbus::BusType::Session);
            if (!bus->isOpen())
                return;
            bus->attachEventLoop(&ime.instance()->eventLoop());
            if (!bus->requestName(pt::kBusName, dbus::RequestNameFlag::None))
                return;
            service = bus->addObject(pt::kBusPath, [this](dbus::Message &request) {
                if (request.type() != dbus::MessageType::MethodCall ||
                    request.interface() != pt::kBusInterface || request.member() != "Command" ||
                    request.signature() != "s")
                    return false;
                pt::Json value;
                try {
                    std::string bytes;
                    request >> bytes;
                    if (!request || bytes.size() > 1024)
                        throw std::runtime_error("无效的跟打请求");
                    value = Command(request.sender(), pt::Json::parse(bytes));
                } catch (const std::exception &error) {
                    value = {{"ok", false}, {"message", error.what()}};
                }
                auto reply = request.createReply();
                reply << value.dump();
                reply.send();
                return true;
            });
            timer = ime.instance()->eventLoop().addTimeEvent(
                CLOCK_MONOTONIC, now(CLOCK_MONOTONIC) + 250000, 0,
                [this](EventSourceTime *source, std::uint64_t current) {
                    Check();
                    source->setNextInterval(250000);
                    source->setOneShot();
                    FCITX_UNUSED(current);
                    return true;
                });
        } catch (...) {
            service.reset();
            bus.reset();
        }
    }
    void Boundary(const std::string &why) {
        if (active) {
            active = false;
            reason = why;
        }
        context = nullptr;
    }
    bool Eligible(InputContext *ic) const {
        return enabled && ic && ic->hasFocus() && ic->program() == "rimes-settings" &&
               !ime.observationProtected() && !ProtectedInput(ic->capabilityFlags()) &&
               ime.plugins().Authorize(pt::kPluginID, "metrics.aggregate").has_value();
    }
    bool Check() {
        if (!active)
            return false;
        if (!Eligible(context) || now(CLOCK_MONOTONIC) > expiry) {
            Boundary("interrupted");
            return false;
        }
        if (ime.instance()->inputMethod(context) != source) {
            Boundary("inputSourceChanged");
            return false;
        }
        if (source == "rimes") {
            auto *state = context->propertyFor(&ime.factory());
            if (!state || state->schemaId() != schema ||
                (ime.chord() && (ime.chord()->Revision() != chord_revision ||
                                 ime.chord()->DurationMicros() != chord_duration))) {
                Boundary("configurationChanged");
                return false;
            }
        }
        return true;
    }
    bool OwnSettings(const std::string &owner) {
        // Credentials are checked only when explicitly arming a test. Nothing
        // in the physical key path performs D-Bus calls or filesystem I/O.
        auto credential = [&](const char *method) {
            auto call = bus->createMethodCall("org.freedesktop.DBus", "/org/freedesktop/DBus",
                                              "org.freedesktop.DBus", method);
            call << owner;
            auto response = call.call(250000);
            std::uint32_t value = UINT32_MAX;
            response >> value;
            if (!response)
                throw std::runtime_error("无法验证跟打窗口");
            return value;
        };
        if (credential("GetConnectionUnixUser") != getuid())
            return false;
        const auto pid = credential("GetConnectionUnixProcessID");
        std::error_code error;
        const auto path =
            std::filesystem::read_symlink("/proc/" + std::to_string(pid) + "/exe", error);
        return !error && path.filename() == "rimes-settings";
    }
    pt::Json Command(const std::string &owner, const pt::Json &command) {
        const auto op = command.at("op").get<std::string>();
        if (op == "begin") {
            const auto lease = ime.plugins().Authorize(pt::kPluginID, "metrics.aggregate");
            if (lease)
                (void)ime.acceptsPluginResult(*lease);
            auto *ic = ime.instance()->lastFocusedInputContext();
            if (!Eligible(ic) || !OwnSettings(owner))
                throw std::runtime_error("请聚焦跟打输入区并启用打字测速插件");
            Check();
            if (active && sender != owner)
                throw std::runtime_error("另一跟打窗口正在运行");
            context = ic;
            sender = owner;
            token = rimes::runtime::RandomID();
            reason.clear();
            source = ime.instance()->inputMethod(ic);
            schema = source;
            chords = 0;
            backspaces = pt::Json::array();
            seen.clear();
            chord_revision = ime.chord() ? ime.chord()->Revision() : 0;
            chord_duration = ime.chord() ? ime.chord()->DurationMicros() : 0;
            if (source == "rimes") {
                auto *state = ic->propertyFor(&ime.factory());
                if (state) {
                    if (state->schemaId().empty())
                        state->activate();
                    schema = state->schemaId();
                }
            }
            if (schema.empty())
                throw std::runtime_error("输入方案尚未准备好");
            const bool chord = source == "rimes" && rimes::chord::IsChordSchema(schema) &&
                               ime.chord() && ime.chord()->Enabled();
            frozen = {{"schemaID", schema},
                      {"inputSourceID", "fcitx5." + source},
                      {"chordCountingAvailable", chord}};
            if (chord) {
                const auto &profile = ime.chord()->Profile();
                frozen["keymapID"] = profile.id;
                frozen["keymapRevision"] = rimes::runtime::Sha256(profile.Value().dump()) + ":" +
                                           std::to_string(chord_duration);
            }
            active = true;
            expiry = now(CLOCK_MONOTONIC) + 3000000;
            return {{"ok", true}, {"token", token}, {"context", frozen}};
        }
        if (sender != owner || token.empty() || command.value("token", "") != token)
            throw std::runtime_error("跟打会话已经失效");
        if (op == "end") {
            Boundary("interrupted");
            token.clear();
            sender.clear();
            return {{"ok", true}};
        }
        if (op != "state" && op != "finish")
            throw std::runtime_error("未知跟打操作");
        Check();
        expiry = now(CLOCK_MONOTONIC) + 3000000;
        const auto value = pt::Json{
            {"ok", true}, {"chords", chords}, {"backspaces", backspaces}, {"reason", reason}};
        if (op == "finish") {
            active = false;
            context = nullptr;
        }
        return value;
    }
};
PracticeService::PracticeService(RimesIme &ime) : impl_(std::make_unique<Impl>(ime)) {}
PracticeService::~PracticeService() = default;
void PracticeService::Enable(bool enabled) {
    impl_->enabled = enabled;
    impl_->Boundary("configurationChanged");
}
void PracticeService::Boundary(const std::string &reason) { impl_->Boundary(reason); }
void PracticeService::Key(KeyEvent &event, bool composing) {
    if (!impl_->Check() || event.inputContext() != impl_->context || event.isRelease() ||
        !composing)
        return;
    const auto &key = event.rawKey();
    if (key.sym() != FcitxKey_BackSpace ||
        key.states().testAny(KeyStates(KeyState::Virtual) | KeyState::Ctrl | KeyState::Alt |
                             KeyState::Super | KeyState::Meta))
        return;
    const auto id = std::make_pair(std::uint32_t(event.time()), std::uint16_t(key.code()));
    if (!impl_->seen.insert(id).second)
        return;
    if (impl_->backspaces.size() >= 1024) {
        impl_->Boundary("interrupted");
        return;
    }
    impl_->backspaces.push_back({{"time", id.first}, {"code", id.second}});
}
void PracticeService::Chord(InputContext *ic) {
    if (impl_->Check() && impl_->context == ic &&
        impl_->frozen.value("chordCountingAvailable", false)) {
        if (impl_->chords >= pt::kMaximumCounter)
            impl_->Boundary("interrupted");
        else
            ++impl_->chords;
    }
}
} // namespace fcitx
