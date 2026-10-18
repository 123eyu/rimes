#include "chord_session.hpp"
#include "engine/rime_key.hpp"
#include <utility>

namespace rimes::linuxime {
chord::Context ChordContext(const EngineSnapshot& snapshot) {
    return {snapshot.raw_input, snapshot.raw_caret_utf8, snapshot.caret_utf8,
            snapshot.selection_start_utf8, snapshot.selection_end_utf8};
}
bool ChordSession::Stage(std::int32_t key, const chord::Profile& profile,
                         const EngineSnapshot& before) {
    if (!batch_.Pending()) {
        profile_ = profile;
        base_ = ChordContext(before);
    }
    if (!profile_ || profile_->Value() != profile.Value()) {
        Cancel();
        return false;
    }
    return batch_.Stage(key, profile);
}
void ChordSession::Cancel() {
    batch_.Reset();
    pairing_.Reset();
    profile_.reset();
    last_batch_count_ = 0;
}
std::optional<EngineSnapshot> ChordSession::Flush(RimeEngine& engine, RimeEngine::SessionId session,
                                                  const chord::Profile& current,
                                                  const Valid& valid) {
    last_batch_count_ = 0;
    if (!Pending())
        return std::nullopt;
    const auto keys = batch_.Take();
    auto profile = std::move(profile_);
    profile_.reset();
    if (!profile || profile->Value() != current.Value() || !valid()) {
        Cancel();
        return std::nullopt;
    }
    EngineSnapshot snapshot;
    if (!engine.Refresh(session, &snapshot) || !(ChordContext(snapshot) == base_) ||
        snapshot.ascii_mode || snapshot.schema_id != profile->SchemaID()) {
        Cancel();
        return std::nullopt;
    }
    std::string commits;
    bool authority_lost = false;
    auto step = [&](std::int32_t key, std::int32_t mask = 0) {
        if (!valid()) {
            authority_lost = true;
            return false;
        }
        if (!engine.ProcessKey(session, key, mask, &snapshot))
            return false;
        commits += snapshot.commit_text;
        return snapshot.handled;
    };
    auto replay = [&](const std::string& replay_keys, chord::Boundary boundary) {
        const bool delimiter = profile->InsertBoundary(replay_keys);
        const bool leading = !delimiter || !boundary.before || step('\'');
        std::string accepted;
        for (char key : replay_keys)
            if (step(key))
                accepted += key;
        std::size_t released = 0;
        for (char key : accepted)
            if (step(key, kReleaseMask))
                ++released;
        const bool trailing = !delimiter || !boundary.after || step('\'');
        return leading && trailing && accepted.size() == replay_keys.size() &&
               released == accepted.size();
    };
    auto boundary = chord::Plan(base_, *profile);
    auto engine_keys = keys;
    auto engine_base = base_.input;
    auto previous = pairing_.Take(keys, base_, *profile);
    if (previous) {
        bool restored = true;
        for (std::size_t index = 0; index < previous->inserted_scalars; ++index)
            if (!step(kBackspace))
                restored = false;
        if (restored && snapshot.raw_input == previous->base_input) {
            engine_keys = previous->keys + keys;
            engine_base = previous->base_input;
            boundary = previous->boundary;
        } else {
            if (!authority_lost && snapshot.raw_input == previous->base_input &&
                replay(previous->keys, previous->boundary))
                pairing_.Record(previous->keys, previous->base_input, ChordContext(snapshot),
                                previous->boundary, *profile);
            if (authority_lost) {
                Cancel();
                return std::nullopt;
            }
            snapshot.commit_text = std::move(commits);
            return snapshot;
        }
    }
    if (replay(engine_keys, boundary)) {
        last_batch_count_ = keys.size();
        // A complete explicit syllable cannot remain a left-half candidate.
        if (profile->MayAwaitComplement(engine_keys))
            pairing_.Record(keys, base_.input, ChordContext(snapshot), boundary, *profile);
        else
            pairing_.Reset();
    } else if (!authority_lost) {
        if (const auto count = chord::InsertedScalarCount(engine_base, snapshot.raw_input))
            for (std::size_t index = 0; index < *count; ++index)
                (void)step(kBackspace);
        if (previous && snapshot.raw_input == previous->base_input &&
            replay(previous->keys, previous->boundary))
            pairing_.Record(previous->keys, previous->base_input, ChordContext(snapshot),
                            previous->boundary, *profile);
    }
    if (authority_lost || !valid()) {
        Cancel();
        return std::nullopt;
    }
    snapshot.commit_text = std::move(commits);
    return snapshot;
}
} // namespace rimes::linuxime
