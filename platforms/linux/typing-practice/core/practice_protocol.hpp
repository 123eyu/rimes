#pragma once
#include "typing_practice_core.hpp"
namespace rimes::practice {
inline constexpr char kPluginID[] = "builtin.typing-speed";
inline constexpr char kBusName[] = "org.scholay.Rimes.Practice1";
inline constexpr char kBusPath[] = "/org/scholay/Rimes/Practice1";
inline constexpr char kBusInterface[] = "org.scholay.Rimes.Practice1";
// Validate the IPC boundary before callbacks use typed JSON accessors. A stale
// or incompatible service must leave practice usable, never unwind into GTK.
inline bool ValidReply(const std::string &operation, const Json &reply) {
    try {
        if (!reply.is_object() || !reply.at("ok").is_boolean())
            return false;
        if (!reply.at("ok").get<bool>())
            return true;
        const auto text = [](const Json &object, const char *key, std::size_t maximum) {
            const auto &value = object.at(key);
            return value.is_string() && !value.get_ref<const std::string &>().empty() &&
                   value.get_ref<const std::string &>().size() <= maximum &&
                   value.get_ref<const std::string &>().find('\0') == std::string::npos;
        };
        const auto integer = [](const Json &value, std::uint64_t maximum) {
            return value.is_number_integer() && value >= 0 && value <= maximum;
        };
        if (operation == "begin") {
            const auto &context = reply.at("context");
            if (!text(reply, "token", 128) || !context.is_object() ||
                !text(context, "schemaID", 256) || !text(context, "inputSourceID", 256) ||
                !context.at("chordCountingAvailable").is_boolean())
                return false;
            return !context.at("chordCountingAvailable").get<bool>() ||
                   (text(context, "keymapID", 256) && text(context, "keymapRevision", 256));
        }
        if (operation == "state" || operation == "finish") {
            if (!integer(reply.at("chords"), kMaximumCounter) ||
                !reply.at("backspaces").is_array() || reply.at("backspaces").size() > 1024 ||
                !reply.at("reason").is_string())
                return false;
            const auto reason = reply.at("reason").get<std::string>();
            if (!reason.empty() && reason != "focusLost" && reason != "interrupted" &&
                reason != "inputSourceChanged" && reason != "configurationChanged")
                return false;
            for (const auto &row : reply.at("backspaces"))
                if (!integer(row.at("time"), UINT32_MAX) || !integer(row.at("code"), UINT16_MAX))
                    return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}
} // namespace rimes::practice
