#include "practice_telemetry.hpp"
#include "practice_protocol.hpp"
#include <cstring>
#include <mutex>

namespace rimes::desktop {
struct PracticeConnection {
    std::mutex mutex;
    GDBusConnection *bus = nullptr;
    ~PracticeConnection() {
        if (bus)
            g_object_unref(bus);
    }
    GDBusConnection *Get(GCancellable *cancel, GError **error) {
        std::lock_guard<std::mutex> guard(mutex);
        if (!bus)
            bus = g_bus_get_sync(G_BUS_TYPE_SESSION, cancel, error);
        return bus ? G_DBUS_CONNECTION(g_object_ref(bus)) : nullptr;
    }
};
namespace {
struct Request {
    Json command;
    std::function<void(Json)> done;
    std::shared_ptr<PracticeConnection> connection;
};
void CallBus(Json command, GCancellable *cancel, std::shared_ptr<PracticeConnection> connection,
             std::function<void(Json)> done) {
    auto *task = g_task_new(
        nullptr, cancel,
        +[](GObject *, GAsyncResult *result, gpointer) {
            auto *task = G_TASK(result);
            GError *error = nullptr;
            auto value =
                std::unique_ptr<Json>(static_cast<Json *>(g_task_propagate_pointer(task, &error)));
            g_clear_error(&error);
            auto *request = static_cast<Request *>(g_task_get_task_data(task));
            if (value && request->done)
                request->done(std::move(*value));
        },
        nullptr);
    g_task_set_task_data(
        task, new Request{std::move(command), std::move(done), std::move(connection)},
        +[](gpointer p) { delete static_cast<Request *>(p); });
    g_task_run_in_thread(
        task, +[](GTask *task, gpointer, gpointer p, GCancellable *cancel) {
            auto *request = static_cast<Request *>(p);
            Json value = {{"ok", false}};
            GError *error = nullptr;
            auto *bus = request->connection->Get(cancel, &error);
            if (bus) {
                const auto bytes = request->command.dump();
                auto *response = g_dbus_connection_call_sync(
                    bus, practice::kBusName, practice::kBusPath, practice::kBusInterface, "Command",
                    g_variant_new("(s)", bytes.c_str()), G_VARIANT_TYPE("(s)"),
                    G_DBUS_CALL_FLAGS_NO_AUTO_START, 1000, cancel, &error);
                if (response) {
                    const char *text = nullptr;
                    g_variant_get(response, "(&s)", &text);
                    if (text && std::strlen(text) <= 65536) {
                        auto decoded = Json::parse(text, nullptr, false);
                        if (practice::ValidReply(request->command.at("op"), decoded))
                            value = std::move(decoded);
                    }
                    g_variant_unref(response);
                }
                g_object_unref(bus);
            }
            if (error) {
                value["message"] = "输入状态服务暂不可用";
                g_clear_error(&error);
            }
            g_task_return_pointer(
                task, new Json(std::move(value)),
                +[](gpointer p) { delete static_cast<Json *>(p); });
        });
    g_object_unref(task);
}
} // namespace
PracticeTelemetry::PracticeTelemetry()
    : cancel_(g_cancellable_new()), connection_(std::make_shared<PracticeConnection>()) {}
PracticeTelemetry::~PracticeTelemetry() {
    End();
    alive_->store(false);
    g_cancellable_cancel(cancel_);
    g_object_unref(cancel_);
}
void PracticeTelemetry::Call(Json command, std::function<void(Json)> done) {
    CallBus(std::move(command), cancel_, connection_,
            [flag = alive_, done = std::move(done)](Json reply) {
                if (flag->load())
                    done(std::move(reply));
            });
}
void PracticeTelemetry::Begin(std::function<void(Json)> done) {
    End();
    const auto generation = generation_;
    CallBus({{"op", "begin"}}, nullptr, connection_,
            [this, connection = connection_, flag = alive_, generation,
             done = std::move(done)](Json reply) {
                if (!flag->load() || generation != generation_) {
                    if (reply.contains("token") && reply.at("token").is_string())
                        CallBus({{"op", "end"}, {"token", reply.at("token")}}, nullptr, connection,
                                {});
                    return;
                }
                if (reply.value("ok", false) && reply.contains("token") &&
                    reply.at("token").is_string())
                    token_ = reply.at("token");
                done(std::move(reply));
            });
}
void PracticeTelemetry::Read(bool finish, std::function<void(Json)> done) {
    if (token_.empty()) {
        done({{"ok", false}});
        return;
    }
    Call({{"op", finish ? "finish" : "state"}, {"token", token_}}, std::move(done));
}
void PracticeTelemetry::End() {
    ++generation_;
    if (token_.empty())
        return;
    CallBus({{"op", "end"}, {"token", token_}}, nullptr, connection_, {});
    token_.clear();
}
} // namespace rimes::desktop
