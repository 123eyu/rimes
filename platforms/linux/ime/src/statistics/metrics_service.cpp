#include "buffer_application.hpp"
#include "fcitx/rimes_ime.hpp"
#include "metrics_protocol.hpp"
#include "metrics_service.hpp"
#include "session/protected_input.hpp"
#include <fcitx-utils/dbus/bus.h>
#include <set>

namespace fcitx {
namespace mt = rimes::metrics;
struct MetricsService::Impl {
    RimesIme& ime;
    bool enabled = false, collect_keys = true, collect_activity = true;
    InputContext* owner = nullptr;
    std::set<std::uint32_t> down;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    EventDispatcher dispatcher;
    mt::Worker worker{rimes::runtime::UserRoot()};
    std::unique_ptr<dbus::Bus> bus;
    std::unique_ptr<dbus::Slot> service;
    std::unique_ptr<HandlerTableEntry<EventHandler>> key_watch;
    explicit Impl(RimesIme& host) : ime(host) {
        dispatcher.attach(&ime.instance()->eventLoop());
        try {
            bus = std::make_unique<dbus::Bus>(dbus::BusType::Session);
            if (!bus->isOpen())
                return;
            bus->attachEventLoop(&ime.instance()->eventLoop());
            if (!bus->requestName(mt::kBusName, dbus::RequestNameFlag::None))
                return;
            service = bus->addObject(mt::kBusPath, [this](dbus::Message& request) {
                if (request.type() != dbus::MessageType::MethodCall ||
                    request.interface() != mt::kBusInterface || request.member() != "Command" ||
                    request.signature() != "s")
                    return false;
                auto reply = std::make_shared<dbus::Message>(request.createReply());
                try {
                    std::string bytes;
                    request >> bytes;
                    if (!request || bytes.size() > 4096)
                        throw std::runtime_error("统计请求过大或无效");
                    const auto lease = ime.plugins().Authorize(mt::kPluginID, mt::kCapability);
                    // UI requests can refresh config/grants; physical observation
                    // below stays read-only and does no file I/O in key routing.
                    if (lease)
                        (void)ime.acceptsPluginResult(*lease);
                    const bool current =
                        enabled &&
                        ime.plugins().Authorize(mt::kPluginID, mt::kCapability).has_value();
                    const bool keys = collect_keys, activity = collect_activity;
                    if (!worker.Request(mt::Json::parse(bytes), [this, flag = alive, reply, current,
                                                                 keys, activity](mt::Json value) {
                            if (!flag->load())
                                return;
                            value["enabled"] = current;
                            value["recordsKeyFrequency"] = keys;
                            value["recordsTypingActivity"] = activity;
                            dispatcher.schedule([flag, reply, value = std::move(value)] {
                                if (!flag->load())
                                    return;
                                *reply << value.dump();
                                reply->send();
                            });
                        }))
                        throw std::runtime_error("统计正在处理记录，请稍后重试");
                } catch (const std::exception& error) {
                    *reply << mt::Json({{"ok", false}, {"message", error.what()}}).dump();
                    reply->send();
                }
                return true;
            });
        } catch (...) {
            service.reset();
            bus.reset();
        }
    }
    ~Impl() {
        alive->store(false);
        key_watch.reset();
        service.reset();
        bus.reset();
        // Worker destruction joins before dispatcher destruction (member order).
    }
    bool Eligible(InputContext* ic) const {
        return enabled && ic && ic->hasFocus() && !ime.observationProtected() &&
               !ProtectedInput(ic->capabilityFlags()) &&
               ime.instance()->inputMethod(ic) == "rimes" &&
               !rimes::buffer::IsCompanionApplication(ic->program()) &&
               ime.plugins().Authorize(mt::kPluginID, mt::kCapability).has_value();
    }
    void Boundary() {
        owner = nullptr;
        down.clear();
        worker.EndSession();
    }
    void Observe(InputContext* ic, mt::Event event) {
        if (Eligible(ic))
            worker.Observe(std::move(event), collect_keys, collect_activity);
    }
};
MetricsService::MetricsService(RimesIme& ime) : impl_(std::make_unique<Impl>(ime)) {
    // Registered before the workspace shortcut handler. Observation never
    // filters or accepts a key, and only admits the focused RIMES context.
    impl_->key_watch = ime.instance()->watchEvent(EventType::InputContextKeyEvent,
        EventWatcherPhase::PreInputMethod, [this](Event& event) {
            KeyPress(static_cast<KeyEvent&>(event));
        });
}
MetricsService::~MetricsService() = default;
void MetricsService::Enable(bool value) {
    impl_->enabled = value;
    impl_->Boundary();
}
bool MetricsService::EnabledFor(InputContext* ic) const { return impl_->Eligible(ic); }
void MetricsService::Suspend() {
    impl_->collect_keys = false;
    impl_->collect_activity = false;
    impl_->Boundary();
}
void MetricsService::Configure(const mt::Json& value) {
    const auto options = rimes::runtime::ConfigurationStore::NormalizePlugin(
        mt::kPluginID, value.at("plugins").value(mt::kPluginID, mt::Json::object()));
    const bool keys = options.at("recordsKeyFrequency"),
               activity = options.at("recordsTypingActivity");
    if (keys != impl_->collect_keys || activity != impl_->collect_activity)
        impl_->Boundary();
    impl_->collect_keys = keys;
    impl_->collect_activity = activity;
}
void MetricsService::KeyPress(KeyEvent& event) {
    auto* ic = event.inputContext();
    if (!impl_->Eligible(ic))
        return;
    const auto& raw = event.rawKey();
    if (raw.states().test(KeyState::Virtual))
        return;
    const auto physical = raw.code() ? std::uint32_t(raw.code()) : std::uint32_t(raw.sym());
    if (impl_->owner != ic) {
        impl_->owner = ic;
        impl_->down.clear();
    }
    if (event.isRelease()) {
        impl_->down.erase(physical);
        return;
    }
    const auto id = MetricsKeyID(raw);
    if (id.empty())
        return;
    mt::Event measured;
    measured.key = id;
    measured.timestamp = mt::Store::Now();
    measured.repeat = raw.states().test(KeyState::Repeat) || !impl_->down.insert(physical).second;
    measured.shortcut =
        raw.states().testAny(KeyStates(KeyState::Ctrl) | KeyState::Alt | KeyState::Super |
                             KeyState::Super2 | KeyState::Hyper | KeyState::Meta | KeyState::Mod5);
    impl_->Observe(ic, std::move(measured));
}
void MetricsService::Commit(InputContext* ic, std::int64_t count) {
    mt::Event event;
    event.kind = mt::Event::Kind::Commit;
    event.timestamp = mt::Store::Now();
    event.characters = count;
    impl_->Observe(ic, std::move(event));
}
void MetricsService::Chord(InputContext* ic, double duration, int releases) {
    mt::Event event;
    event.kind = mt::Event::Kind::Chord;
    event.timestamp = mt::Store::Now();
    event.chord_duration = duration;
    event.handled_releases = releases;
    impl_->Observe(ic, std::move(event));
}
void MetricsService::Focus(InputContext* ic) {
    impl_->owner = ic;
    impl_->down.clear();
    if (!impl_->Eligible(ic))
        impl_->worker.EndSession();
}
void MetricsService::Forget(InputContext* ic) {
    if (impl_->owner == ic) {
        impl_->owner = nullptr;
        impl_->down.clear();
    }
}
void MetricsService::EndSession() { impl_->Boundary(); }
} // namespace fcitx
