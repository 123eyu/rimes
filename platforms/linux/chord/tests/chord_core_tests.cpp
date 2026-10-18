#include "chord_core.hpp"
#include <iostream>
#include <stdexcept>

using namespace rimes::chord;
void Check(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    try {
        const auto& native = Contract();
        std::vector<Profile> profiles;
        for (const auto& item : native.at("compilers")) {
            auto profile = Profile::Parse(item.at("profile"));
            Check(profile.Value() == item.at("profile"), "native normalization differs");
            Check(profile.SchemaID() == item.at("schemaID"), "schema identity differs");
            Check(Json(profile.Algebra()) == item.at("algebra"), "Rime algebra differs");
            Check(profile.SchemaYAML() == item.at("yaml"), "complete Rime schema differs");
            profiles.push_back(profile);
        }
        for (const auto& rule : native.at("rules")) {
            const auto& profile = profiles.at(rule.at("profileIndex").get<std::size_t>());
            const auto keys = rule.at("keys").get<std::string>();
            Check(profile.Canonical(keys) == rule.at("canonical"), "canonical key order");
            Check(profile.InsertBoundary(keys) == rule.at("insertBoundary"), "syllable boundary");
            Check(profile.MayCombine(keys) == rule.at("mayCombine"), "combine permission");
            Check(profile.MayAwaitComplement(keys) == rule.at("mayAwaitComplement"),
                  "pairing permission");
        }
        for (const auto& item : native.at("validation")) {
            for (bool apply : {false, true}) {
                bool accepted = true;
                try {
                    (void)Profile::Parse(item.at("profile"), apply);
                } catch (...) {
                    accepted = false;
                }
                Check(accepted == item.at(apply ? "applyAccepted" : "draftAccepted"),
                      "validation differs: " + item.at("name").get<std::string>());
            }
        }
        const auto builtin = Profile::BuiltIn();
        Batch batch;
        Check(batch.Stage('q', builtin) && !batch.Stage('q', builtin) && batch.Stage('y', builtin),
              "repeat or chord staging");
        Check(!batch.Stage(0x1234, builtin) && batch.Take() == "qy" && !batch.Pending(),
              "batch settlement");
        Pairing pairing;
        const Context q{"q", 1, 1, 0, 1};
        pairing.Record("q", "", q, {}, builtin);
        Check(pairing.Take("km", q, builtin).has_value(), "left singleton/right chord pairing");
        Check(!pairing.Take("km", q, builtin), "pairing consumed twice");
        pairing.Record("q", "", q, {}, builtin);
        Check(!pairing.Take("y", q, builtin), "two singletons must remain literal");
        pairing.Record("dv", "", {"n", 1, 1, 0, 1}, {}, builtin);
        Check(pairing.Take("i", {"n", 1, 1, 0, 1}, builtin).has_value(),
              "left chord/right singleton pairing");
        pairing.Record("q", "", q, {}, builtin);
        Check(!pairing.Take("km", {"q", 0, 1, 0, 1}, builtin),
              "raw caret edit did not retire pairing");
        pairing.Record("q", "", q, {}, builtin);
        Check(!pairing.Take("km", {"q", 1, 1, 1, 1}, builtin),
              "selection edit did not retire pairing");
        pairing.Record("q", "", q, {}, builtin);
        pairing.Reset();
        Check(!pairing.Take("km", q, builtin), "focus cancellation did not retire pairing");
        const auto& explicit_profile = profiles[1];
        pairing.Record("cd", "", {"ni'", 3, 3, 0, 3}, {}, explicit_profile);
        Check(!pairing.Take("ef", {"ni'", 3, 3, 0, 3}, explicit_profile),
              "complete syllable remained pairable");
        Check(InsertedScalarCount("nihao", "ni'wo'hao") == 4 &&
                  InsertedScalarCount("🙂字", "🙂中🦊字") == 2 &&
                  !InsertedScalarCount("old", "new"),
              "contiguous insertion rollback");
        Check(Plan({"nihao", 2}, explicit_profile).before &&
                  Plan({"nihao", 2}, explicit_profile).after && !Plan({"ni'", 3}, builtin).before,
              "middle/end boundary plan");
        for (const auto& preset : Profile::NativeProfiles()) {
            Check(preset.IsPreset() && IsChordSchema(preset.SchemaID()) && preset.mappings.empty(),
                  "native preset");
            auto malicious = preset.Value();
            malicious["nativeSchemeID"] = "rime_ice";
            bool accepted = true;
            try {
                (void)Profile::Parse(malicious);
            } catch (...) {
                accepted = false;
            }
            Check(!accepted, "native preset identity injection");
        }
        for (const auto& fixture : native.at("learning")) {
            const auto courses = Curriculum(Profile::Parse(fixture.at("profile")));
            Check(courses.size() == fixture.at("courses").size(), "native curriculum course count");
            for (std::size_t index = 0; index < courses.size(); ++index) {
                const auto& course = courses[index];
                const auto& expected = fixture.at("courses").at(index);
                Check(course.id == expected.at("id") && course.title == expected.at("title") &&
                          course.key_count == expected.at("keyCount") &&
                          course.mappings.size() == expected.at("mappings").size(),
                      "native curriculum identity");
                for (std::size_t n = 0; n < course.mappings.size(); ++n) {
                    const auto& actual = course.mappings[n];
                    const auto& mapping = expected.at("mappings").at(n);
                    Check(actual.id == mapping.at("id") && actual.keys == mapping.at("keys") &&
                              actual.output == mapping.at("output"),
                          "stable mapping or native source order changed");
                }
                const auto sample = Sample(course, 30, Json::object(), 42);
                for (std::size_t n = 0; n < sample.size(); ++n)
                    Check(sample[n].id == expected.at("sample42").at(n),
                          "native deterministic sample differs");
            }
        }
        Check(Matches("ydf", "dfy") && !Matches("ddf", "dfy") && !Matches("", ""),
              "physical key-set matching");
        std::cout << "PASS: " << profiles.size() << " actual native schemas, "
                  << native.at("rules").size() << " boundary fixtures, "
                  << native.at("validation").size()
                  << " validation fixtures, batching, exact-context pairing and rollback\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
