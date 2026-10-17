#include "plugin_registry.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace rimes::plugins;
void Check(bool condition) { if (!condition) throw std::runtime_error("registry contract failed"); }
Descriptor Buffer(const char* id) { return {id, "AI", "1.0.0", Kind::Buffer, {"buffer.read", "ai.generate"}}; }

int main() {
    Registry registry;
    int started = 0, stopped = 0;
    registry.Register(Buffer("builtin.api"), {[&] { ++started; }, [&] { ++stopped; }});
    registry.Register(Buffer("builtin.translator"));
    registry.Register({"builtin.capsule.notes", "笔记", "1.1.0", Kind::Capsule, {"capsule.navigate"}});
    registry.Register({"builtin.mailbox.chat", "对话", "1.1.0", Kind::Mailbox, {"mailbox.navigate"}});
    registry.Register({"builtin.statistics", "统计", "2.0.0", Kind::Extension, {"metrics.aggregate"}});
    Check(registry.Entries().size() == 5);
    Check(!registry.ApplyState("builtin.missing", true, true));
    Check(!registry.ApplyState("builtin.api", false, true));
    Check(!registry.Authorize("builtin.api", "ai.generate"));
    Check(registry.ApplyState("builtin.api", true, true));
    Check(started == 1 && stopped == 0 && registry.SelectedBuffer().empty());
    Check(!registry.Authorize("builtin.api", "ai.generate"));
    Check(registry.SelectBuffer("builtin.api"));
    const auto first = *registry.Authorize("builtin.api", "ai.generate");
    Check(registry.Accepts(first));
    Check(!registry.Authorize("builtin.api", "capsule.navigate"));
    Check(registry.ApplyState("builtin.api", true, true));
    Check(registry.Accepts(first) && started == 1);
    Check(registry.ApplyState("builtin.translator", true, true));
    Check(registry.SelectedBuffer() == "builtin.api");
    Check(registry.SelectBuffer("builtin.translator"));
    Check(!registry.Accepts(first));
    Check(registry.SelectBuffer("builtin.api"));
    Check(!registry.Accepts(first));
    const auto before_update = *registry.Authorize("builtin.api", "ai.generate");
    registry.Invalidate("builtin.api");
    Check(!registry.Accepts(before_update));
    const auto before_disable = *registry.Authorize("builtin.api", "ai.generate");
    Check(registry.ApplyState("builtin.api", true, false));
    Check(!registry.Accepts(before_disable) && registry.SelectedBuffer().empty() && stopped == 1);
    Check(!registry.SelectBuffer("builtin.api"));
    Check(registry.ApplyState("builtin.api", true, true));
    Check(registry.SelectBuffer("builtin.api"));
    const auto before_uninstall = *registry.Authorize("builtin.api", "ai.generate");
    Check(registry.ApplyState("builtin.api", false, false));
    Check(!registry.Accepts(before_uninstall) && registry.SelectedBuffer().empty());
    Check(registry.ApplyState("builtin.capsule.notes", true, true));
    Check(!registry.SelectBuffer("builtin.capsule.notes"));
    Check(registry.Authorize("builtin.capsule.notes", "capsule.navigate").has_value());
    Registry restored;
    restored.Register(Buffer("builtin.api"));
    Check(restored.ApplyState("builtin.api", true, true));
    Check(restored.SelectBuffer("builtin.api"));
    Check(!restored.Accepts(before_update));
    bool rejected = false;
    try { registry.Register(Buffer("builtin.api")); } catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected);
    rejected = false;
    try { registry.Register(Buffer("builtin../outside")); } catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected);
    int unwound = 0;
    registry.Register(Buffer("builtin.failure"), {[] { throw std::runtime_error("failed"); }, [&] { ++unwound; }});
    Check(!registry.ApplyState("builtin.failure", true, true));
    Check(!registry.Find("builtin.failure")->enabled && unwound == 1);
    registry.Shutdown();
    Check(!registry.ApplyState("builtin.api", true, true));
    Check(!registry.Authorize("builtin.capsule.notes", "capsule.navigate"));
    std::cout << "Plugin lifecycle, capabilities, revocation and restart isolation passed\n";
}
