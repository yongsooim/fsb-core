#pragma once
#include "actor_lifecycle.hpp"

// Per-frame actor callbacks that drive a record's own motion.
namespace fsb::core::actor_core {

// 45c063: the entrance dive a battle enemy plays on arrival. +0x38 counts the
// step, +0x34 is where the dive ends, and the height traces0x190 - step^2
// until the counter passes20, after which the actor is on the ground.
inline constexpr unsigned dive_step = 0x38;
inline constexpr unsigned dive_length = 0x34;
inline constexpr std::int32_t dive_apex = 0x190;
inline constexpr std::int32_t dive_last_arc_step = 0x14;
inline constexpr Address dive_frame_table = 0x5d0770;
inline constexpr std::uint32_t dive_sprite_selector = 0x20006;
void tick_entrance_dive(const GuestWords& words, const Memory& memory, Address object);

// 45d208: the oscillation a floating object rides. +0x168 selects the shape
// and which axes take part:
//   bit0 accumulate a delta that reverses at its limit
//   bit1 place the axis absolutely on a sine wave
//   bit2 add a sine offset to wherever the axis already is
//   bits4/5/6 enable the x, y and z axes
// With none of the shape bits set the routine does nothing at all, not even
// refresh the tile lanes.
inline constexpr unsigned oscillation_mode = 0x168;
inline constexpr std::uint32_t oscillation_accumulate = 1;
inline constexpr std::uint32_t oscillation_absolute = 2;
inline constexpr std::uint32_t oscillation_relative = 4;
inline constexpr std::uint32_t oscillation_axis_x = 0x10;
inline constexpr std::uint32_t oscillation_axis_y = 0x20;
inline constexpr std::uint32_t oscillation_axis_z = 0x40;
using FixedWave = std::function<std::uint32_t(std::uint32_t angle)>;
void tick_oscillation(const GuestWords& words, Address object,
                      const FixedWave& sine, const FixedWave& cosine);

// 45befc: walk the packed path commands an actor was given. Each command is a
// half word at +0x3c; +0x38 is the cursor and +0x34 the length. A0x01 high
// byte turns on the spot, a high bit sets a step whose length depends on the
// command, and running out of commands hands the actor back to field movement.
inline constexpr unsigned path_cursor_index = 0x38;
inline constexpr unsigned path_length = 0x34;
inline constexpr unsigned path_active_marker = 0x2c;
inline constexpr std::uint32_t path_running = 1;
inline constexpr std::uint32_t path_command_turn = 0x0100;
inline constexpr std::uint32_t path_command_step = 0x8000;
inline constexpr std::uint32_t path_command_long_step = 0x8100;
inline constexpr std::uint32_t path_command_step_bit = 0x8000;
// The step is expressed in eighths of a tile before the Q16 shift.
inline constexpr unsigned path_step_shift = 13;
using StepActorMotionByte = std::function<std::uint32_t(Address object)>;
using ResolveActorFrame = std::function<void(Address object)>;
void tick_scripted_path(const GuestWords& words, Memory& memory, Address object,
                        const StepActorMotionByte& step, const ResolveActorFrame& resolve_frame);

// 45c3a9: the cursor the player steers over the battle grid. Despite the
// "floating object" name in the decompiler cache, this reads the four arrow
// states and walks a record one tile at a time.
//
// A move takes four frames. The frame that accepts input picks a direction,
// takes the first quarter tile and sets the hold counter at +0x10c to1; the
// next three frames take a quarter each and step the counter1,2,3,0. Input is
// only read again once the counter is back at0, so a move always completes.
// A hidden record is skipped entirely, and each edge is guarded so the cursor
// stays one tile inside the grid.
inline constexpr unsigned cursor_hold = 0x10c;
inline constexpr unsigned cursor_hold_frames = 4;
inline constexpr std::int32_t cursor_quarter_tile = 0x4000;
inline constexpr std::int32_t cursor_edge_margin = 2;
inline constexpr std::uint32_t cursor_sprite_selector = 0x30000;
inline constexpr std::uint32_t cursor_sprite_base_frame = 0x60;
inline constexpr unsigned cursor_blink_frames = 2;
void tick_grid_cursor(Memory& memory, const GuestWords& words, Address object);

} // namespace fsb::core::actor_core
