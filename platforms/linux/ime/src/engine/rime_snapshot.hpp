#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rimes::linuxime {

inline constexpr std::size_t kMaxCandidateCount = 16;
inline constexpr std::size_t kMaxTextBytes = 16U * 1024U;

struct EngineCandidate {
    std::string text;
    std::string comment;
    std::string label;
};

// Owned, validated view of one librime update. Positions stay UTF-8 bytes
// because Fcitx5 Text::setCursor is byte-based.
struct EngineSnapshot {
    bool handled = false;
    bool composing = false;
    bool ascii_mode = false;
    std::string schema_id;
    std::string preedit;
    std::string raw_input;
    std::string commit_text;
    std::size_t caret_utf8 = 0;
    // Keep raw-input caret and marked selection distinct from the formatted
    // preedit cursor. Cross-batch chords may replace only the exact insertion
    // that this private session still owns.
    std::size_t raw_caret_utf8 = 0;
    std::size_t selection_start_utf8 = 0, selection_end_utf8 = 0;
    int page_no = 0;
    int page_size = 0;
    int highlighted = -1;
    bool is_last_page = true;
    std::vector<EngineCandidate> candidates;
};

bool IsValidUtf8(std::string_view text) noexcept;

bool CopyBoundedUtf8(const char* value,
                     std::size_t maximum_bytes,
                     std::string* output,
                     std::string* error) noexcept;

}  // namespace rimes::linuxime
