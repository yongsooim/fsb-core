#pragma once
#include "actor_lifecycle.hpp"

// The field callback that owns an actor while it is crossing something it
// cannot simply walk over: a ledge, a stair, a gate or a cart rail.
//
// field_actor_movement (458ed7) installs this callback with motion state 8 and
// takes the actor back once state 9 finishes. Between those two the actor is
// driven entirely from its own record:
//
//   +0x104 motion state   5  turn one step per frame toward +0x114, then 10
//                         8  a sixteen-frame hop, then 10
//                         9  the gate side-step, then back to field movement
//                        0xb the variable arc the tile under the actor chose
//                        10  settled, but this callback still owns the actor
//                        12  a static pose
//   +0x108 motion frame   counts up while the arc runs; 0 means finished
//   +0x10c frame group    the frame the arc ends on (states 8/9: a 0/1 toggle)
//   +0x110 facing         0 up, 1 down, 2 left, 3 right, 4..7 diagonal
//   +0x114 target facing  where state 5 is turning to
//   +0x3c  arc submode    16 bits, an alias of the path-command block
//
// Both the sound cues this reaches are injected: they are the audio service
// boundary, not part of the motion.
namespace fsb::core::actor_core {

inline constexpr std::int32_t jump_fixed_one = 0x10000;
// One sixteenth of a tile: the nudge that seeds a step and the drift the two
// sideways facings carry every frame of a hop.
inline constexpr std::int32_t jump_nudge = 0x1000;
inline constexpr std::int32_t jump_quarter_step = 0x4000;
// A third of a tile, in the four roundings the original actually writes.
inline constexpr std::int32_t jump_third_step = 0x5555;
inline constexpr std::int32_t jump_third_step_up = 0x5556;
inline constexpr std::int32_t jump_third_step_rise = 0x555d;
inline constexpr std::int32_t jump_third_step_small = 0x555f;

inline constexpr std::int32_t jump_state_normal = 0;
inline constexpr std::int32_t jump_state_turning = 5;
inline constexpr std::int32_t jump_state_hop = 8;
inline constexpr std::int32_t jump_state_side_step = 9;
inline constexpr std::int32_t jump_state_settled = 10;
inline constexpr std::int32_t jump_state_arc = 0xb;
inline constexpr std::int32_t jump_state_static_pose = 0xc;

// The hop and the side-step both run a sixteen-frame loop.
inline constexpr std::int32_t jump_hop_frames = 0x10;
// An arc runs its speed table for this many frames before it consults the tile
// again; it is also the frame an arc is seeded at the end of.
inline constexpr std::int32_t jump_arc_table_frames = 0x1d;
inline constexpr std::int32_t jump_first_frame = 1;

// The arc submode at +0x3c, which outlives the frame that chose it.
inline constexpr std::uint32_t jump_arc_flat = 0;
inline constexpr std::uint32_t jump_arc_small_up = 1;
inline constexpr std::uint32_t jump_arc_large_up = 2;
inline constexpr std::uint32_t jump_arc_short_table = 3;
inline constexpr std::uint32_t jump_arc_flat_fast = 4;

// Tile attribute lanes. Bits [21..25] pick the arc the tile wants; the two
// three-bit direction fields above them are the ledge's entry and exit sides.
inline constexpr unsigned jump_tile_arc_shift = 0x15;
inline constexpr std::uint32_t jump_tile_arc_mask = 0x1f;
inline constexpr unsigned jump_tile_entry_dir_shift = 0x1a;
inline constexpr unsigned jump_tile_exit_dir_shift = 0x1d;
inline constexpr std::uint32_t jump_tile_block = 0x200;
inline constexpr std::uint32_t jump_tile_no_exit_up = 0x20;
inline constexpr std::uint32_t jump_tile_no_exit_down = 0x40;
// A tile whose arc lane holds exactly this is never continued onto.
inline constexpr std::uint32_t jump_tile_arc_lane = 0x3e00000;
inline constexpr std::uint32_t jump_tile_arc_forbidden = 0x600000;
// Byte 2, bit 0 of an occupancy cell: somebody is standing there.
inline constexpr std::uint32_t jump_cell_occupied = 0x10000;

// Which arc the tile asked for, in the same order as the attribute lane.
inline constexpr std::uint32_t jump_tile_arc_swap_or_flat = 0;
inline constexpr std::uint32_t jump_tile_arc_small_up = 1;
inline constexpr std::uint32_t jump_tile_arc_large_up = 2;
inline constexpr std::uint32_t jump_tile_arc_short_table = 3;
inline constexpr std::uint32_t jump_tile_arc_flat_fast = 4;

// The direction the settled actor is being asked to continue in. The order is
// the field input order, and it is also the encoding direction_to_cardinal
// maps a facing into, which is how the two are compared.
inline constexpr std::uint32_t jump_continue_up = 0;
inline constexpr std::uint32_t jump_continue_down = 1;
inline constexpr std::uint32_t jump_continue_left = 2;
inline constexpr std::uint32_t jump_continue_right = 3;
inline constexpr std::uint32_t jump_continue_none = 4;

// The gate an actor may reserve while it steps sideways through it: a pair of
// tile coordinates per slot, selected by the shared active-gate index.
inline constexpr Address jump_active_gate = 0x5f858c;
inline constexpr Address jump_gate_tile_x = 0x5f8590;
inline constexpr Address jump_gate_tile_y = 0x5f8594;
inline constexpr unsigned jump_gate_slot_bytes = 8;

// Frame tables. The hop and the side-step share both of them.
inline constexpr Address jump_hop_frame_table = 0x5d05e8;
inline constexpr Address jump_hop_rise_table = 0x5d0708;
inline constexpr Address jump_diagonal_to_cardinal = 0x5d0578;
inline constexpr unsigned jump_frames_per_direction = 6;
inline constexpr unsigned jump_diagonal_frame = 5;
inline constexpr unsigned jump_hop_frame_stride = 8;

// A map layer descriptor is 0x8028 bytes and owns 0x1000 grid cells.
inline constexpr unsigned jump_layer_descriptor_bytes = 0x8028;
inline constexpr std::int32_t jump_layer_cells = 0x1000;

// The four held/pressed input pairs, in the order the original tests them.
struct JumpDirectionKeys { Address held, pressed; };
inline constexpr JumpDirectionKeys jump_direction_keys[4] = {
    {0x6da888, 0x6da688}, {0x6da8a8, 0x6da6a8},
    {0x6da894, 0x6da694}, {0x6da89c, 0x6da69c},
};

// The audio boundary. `stop` drops whatever is still playing on the cue,
// `play` starts one.
struct JumpMotionSounds {
    std::function<void(std::uint32_t cue)> play;
    std::function<void(std::uint32_t cue)> stop;
};
inline constexpr std::uint32_t jump_cue_step = 0x164;
inline constexpr std::uint32_t jump_cue_land_small = 0x165;
inline constexpr std::uint32_t jump_cue_mid_air = 0x166;
inline constexpr std::uint32_t jump_cue_land_large = 0x167;

// Only an actor carrying this flag is offered a continuation when it settles.
inline constexpr std::uint32_t jump_continues_bit = 0x80;

// 45abf9. Advances one frame of the actor's special movement, then chooses the
// sprite frame that frame is drawn with. The original returns whatever its
// last expression left in EAX and no caller reads it.
void tick_jump_motion(Memory& memory, const GuestWords& words, Address object,
                      const JumpMotionSounds& sound);

} // namespace fsb::core::actor_core
