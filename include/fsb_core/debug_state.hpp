#pragma once
#include "primitives.hpp"

namespace fsb::core {
class Runtime;
struct DebugField {std::string label,value;};
struct DebugSection {std::string id,title;std::vector<DebugField> fields;};
// An observation, never a second writable copy of the game's state.
struct DebugSnapshot {std::uint32_t frame_ms=0;std::vector<DebugSection> sections;};
DebugSnapshot inspect_runtime(const Runtime& runtime,const std::vector<std::uint8_t>& cp949={});
std::string debug_json(const DebugSnapshot& snapshot);
} // namespace fsb::core
