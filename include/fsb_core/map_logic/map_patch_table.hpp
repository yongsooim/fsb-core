#pragma once
#include "../primitives.hpp"

namespace fsb::core {
class Map;
namespace map_logic {
// Descriptor table at tables::map_patches, 0x604040..0x6079c4. Each record owns
// the map it belongs to, the target rectangle, and the two tile blobs the
// original swaps in for the set and cleared state of the matching event flag.
inline constexpr unsigned map_patch_records=0x199,map_patch_stride=36;
namespace map_patch_offset {
inline constexpr unsigned map_id=0,x=4,y=8,width=16,height=20,set_tiles=24,cleared_tiles=28,sound_cue=32;
}
// 0x49711f: re-apply every patch record of one map from its stored event flag.
// Walks the descriptor table in table order, so a later record overwrites an
// earlier one on shared cells exactly as the original does.
void resync_map_patches(Memory& memory,Map& map,std::uint32_t map_id);
} // namespace map_logic
} // namespace fsb::core
