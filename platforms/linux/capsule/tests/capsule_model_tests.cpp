#include "capsule_model.hpp"
#include "capsule_protocol.hpp"

#include <iostream>

namespace {

int g_failures = 0;

void Expect(bool cond, const char* message) {
    if (!cond) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

}  // namespace

int main() {
    rimes::capsule::CapsuleModel model;
    rimes::capsule::Record note;
    note.id = rimes::capsule::kDefaultEntryId;
    note.kind = rimes::capsule::Kind::Note;
    note.title = rimes::capsule::kDefaultEntryTitle;
    note.content = rimes::capsule::kDefaultEntryContent;
    rimes::capsule::Record second;
    second.id = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
    second.kind = rimes::capsule::Kind::Note;
    second.title = "Other";
    second.content = "zzz only";
    model.LoadCards({note, second});
    Expect(model.cards().size() == 2, "loaded two notes");
    Expect(model.Selected() != nullptr && model.Selected()->title == note.title, "first selected");

    model.Move(1);
    Expect(model.Selected()->title == "Other", "move right");
    model.append_query('z');
    model.append_query('z');
    model.append_query('z');
    Expect(model.cards().size() == 1, "search filters");
    Expect(model.Selected()->title == "Other", "filter keeps match");
    model.backspace_query();
    model.backspace_query();
    model.backspace_query();
    Expect(model.cards().size() == 2, "clear search");

    model.EnableModule(rimes::capsule::Module::Temporary, true);
    model.set_tab(rimes::capsule::Tab::Recent);
    Expect(model.cards().empty(), "empty temporary module");
    Expect(model.module() == rimes::capsule::Module::Temporary, "legacy tab selects module");
    model.set_tab(rimes::capsule::Tab::Note);

    {
        // Legacy tab clients must read back the tab they selected (#332).
        using rimes::capsule::Module;
        using rimes::capsule::Tab;
        model.EnableModule(Module::Capture, true);
        model.EnableModule(Module::Resources, true);
        struct LegacyTab { Tab tab; Module module; const char* filter; const char* raw; };
        const LegacyTab legacy[] = {
            {Tab::Image, Module::Capture, "image", "image"},
            {Tab::Video, Module::Capture, "video", "video"},
            {Tab::Captures, Module::Capture, "all", "captures"},
            {Tab::Skill, Module::Resources, "skills", "skill"},
            {Tab::Pdf, Module::Resources, "reference", "pdf"},
            {Tab::Resource, Module::Resources, "all", "resource"},
            {Tab::Note, Module::Notes, "all", "note"},
        };
        for (const auto& item : legacy) {
            const auto before = model.generation();
            model.set_tab(item.tab);
            const auto label = std::string("legacy tab ") + item.raw;
            Expect(model.tab() == item.tab, (label + " reports itself").c_str());
            Expect(model.module() == item.module, (label + " selects its module").c_str());
            Expect(model.filter() == item.filter, (label + " applies its filter").c_str());
            Expect(rimes::capsule::MakeSnapshot(model).tab == item.raw, (label + " reaches the snapshot").c_str());
            Expect(model.generation() > before, (label + " bumps the generation").c_str());
            const auto settled = model.generation();
            model.set_tab(item.tab);
            Expect(model.generation() == settled, (label + " is idempotent").c_str());
        }
        model.EnableModule(Module::Capture, false);
        model.set_tab(Tab::Image);
        Expect(model.tab() == Tab::Note && model.module() == Module::Notes && model.filter() == "all",
               "legacy tab of a disabled module keeps the current selection");
        model.EnableModule(Module::Resources, false);
    }

    model.set_visible(true);
    model.set_armed(true);
    model.set_token("1");
    Expect(model.arms("1"), "armed token");
    model.Disarm("switch");
    Expect(!model.armed(), "disarmed");
    Expect(!model.arms("1"), "old token dead");
    Expect(model.visible(), "disarm keeps visible");
    Expect(model.hint().find("re-arm") != std::string::npos, "disarm hint names re-arm");

    model.append_query('z');
    model.set_visible(true);
    model.set_armed(true);
    model.set_token("2");
    model.Hide();
    Expect(!model.visible(), "hide");
    Expect(!model.armed(), "hide disarms");
    Expect(model.query().empty(), "hide clears leftover search");
    Expect(model.cards().size() == 2, "hide restores the unfiltered note list");

    model.set_password_field(true);
    Expect(!model.visible(), "password hides");
    Expect(!model.armed(), "password disarms");

    if (g_failures != 0) {
        std::cerr << g_failures << " capsule model failures\n";
        return 1;
    }
    std::cout << "ok: capsule model\n";
    return 0;
}
