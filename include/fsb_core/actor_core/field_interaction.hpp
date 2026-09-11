#pragma once
#include "actor_lifecycle.hpp"

// What happens when the player presses confirm on the field: probe the tile the
// actor faces, and talk to whatever is standing there or pick up whatever is
// lying there.
//
// The probe looks one tile ahead first. If that tile holds nothing it may look
// one further, but only when both edges it would cross are open. An actor can
// be talked to from either distance; an item can only be picked up from the
// tile immediately ahead.
namespace fsb::core::actor_core {

// One dword per cell, 0x1000 cells per layer. Bit16 marks the cell occupied
// and the low half word is what occupies it.
inline constexpr Address tile_occupancy = 0x7abca0;
inline constexpr unsigned occupancy_cells_per_layer = 0x1000;
inline constexpr std::uint32_t occupancy_occupied = 0x10000;
inline constexpr std::uint32_t occupancy_object_mask = 0xffff;
// The tile attribute planes are one 0x4000-byte plane per grid.
inline constexpr unsigned tile_attribute_plane_bytes = 0x4000;
inline constexpr Address current_grid_id = 0x77e598;
// One blocking bit per edge of a tile, in facing order.
inline constexpr std::uint32_t edge_blocked[] = {0x20, 0x40, 0x80, 0x100};
// The message each facing hands the dialogue.
inline constexpr Address facing_dialogue_message = 0x5bf498;
// Object ids below this are live actors; the rest are map objects.
inline constexpr std::uint32_t interaction_actor_limit = 0x5a;
// Set on an actor that has already answered this press.
inline constexpr std::uint32_t dialogue_answered_bit = 0x40000000;
// The state an actor is put into while it is talking, and the range of actor
// types that get that treatment rather than just raising the event.
inline constexpr std::uint32_t dialogue_pose_state = 5;
inline constexpr std::int32_t dialogue_actor_type_limit = 7;
inline constexpr Address field_event_pending = 0x802c9c;
inline constexpr Address field_event_block = 0x802ca0;

// How far ahead the probe found something.
enum class ProbeDistance : std::int32_t { None = -1, Adjacent = 1, TwoAway = 2 };
struct TileProbe {
    ProbeDistance distance = ProbeDistance::None;
    std::int32_t object = -1;
};
// The probe on its own, without acting on what it found.
TileProbe probe_faced_tile(const Memory& memory, const GuestWords& words, Address actor);

struct FieldInteractionHooks {
    // 413b74. Answers zero when no dialogue could start. `actor_type` is an out
    // parameter the dialogue fills in, and stays negative when it sets none.
    std::function<std::uint32_t(std::uint32_t actor, std::uint32_t message,
                                std::int32_t& actor_type)> start_dialogue;
    // 455099: what the map sparkle on that cell holds, if anything.
    std::function<void(std::uint32_t object, std::int32_t& item,
                       std::int32_t& quantity)> collect_sparkle;
    std::function<void()> play_pickup_cue;
};

// 45d395. The return value is a mix of probe sentinels and record addresses;
// no caller reads it, so it is reproduced rather than given a meaning.
std::uint32_t probe_adjacent_trigger(Memory& memory, const GuestWords& words, Address actor,
                                     const FieldInteractionHooks& hooks);

} // namespace fsb::core::actor_core
