#pragma once
#include "engine/rime_snapshot.hpp"
#include "../../../../../Shared/CandidateMatrix/navigation.hpp"
namespace fcitx {
struct CandidatePageTraits {
    static bool visible(const rimes::linuxime::EngineSnapshot& page) { return !page.candidates.empty(); }
    static std::size_t highlighted(const rimes::linuxime::EngineSnapshot& page) { return static_cast<std::size_t>(std::max(0, page.highlighted)); }
    static std::uint32_t pageId(const rimes::linuxime::EngineSnapshot& page) { return page.page_no * page.page_size; }
};
using CandidateNavigation = rimes::candidates::Navigation<rimes::linuxime::EngineSnapshot, CandidatePageTraits>;

// Space-chord schemas need Space in librime's chord_composer, which then
// commits librime's own highlight. The arrow keys and the matrix move only
// XSIME's selection, so Space may go to librime only while the two agree.
inline bool SpaceSelectsEngineHighlight(const CandidateNavigation& navigation) {
    return !navigation.empty() && !navigation.expanded() &&
           navigation.column() == CandidatePageTraits::highlighted(navigation.page(navigation.row()));
}
}
