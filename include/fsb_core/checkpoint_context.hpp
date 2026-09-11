#pragma once
#include "primitives.hpp"
namespace fsb::core {
namespace checkpoint_context {
inline constexpr unsigned secret_arena_lobby=166, secret_arena_ring=167;
inline constexpr unsigned west_tower_floor=424, east_tower_floor=442;
inline constexpr Address arena_return_map=0x5d2298;
}
// The original DAT omits this transient field. A checkpoint's separately
// recorded context restores it; it does not infer a tower from party membership.
bool can_restore_secret_arena_return(std::span<const std::uint8_t> save,unsigned origin_map);
bool restore_secret_arena_return(Memory& memory,unsigned origin_map);
}
