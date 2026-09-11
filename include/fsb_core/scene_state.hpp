#pragma once
#include <cstdint>

namespace fsb::core {
// Scene selection and event handoff. These values are the actual storage,
// not a mirror of the original address image. Sentinel bit patterns survive
// import/export unchanged; typed ScriptId conversion is a separate boundary.
struct SceneState {
    std::uint32_t current_event = 0;
    std::uint32_t previous_event = 0;
    std::uint32_t pending_event = 0;
    std::uint32_t resume_event = 0;
    std::uint32_t current_map = 0;
    std::uint32_t game_mode = 0;
};
} // namespace fsb::core
