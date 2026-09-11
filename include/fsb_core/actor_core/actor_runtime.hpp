#pragma once
#include "actor_slots.hpp"

// Runtime object bookkeeping: the flag bits, the saved-pose block and the
// table resets that surround actor lifetime, plus the small script-facing
// setters that share this address range.
namespace fsb::core::actor_core {

// The actor pool: 0x300 records of0x1ac bytes starting at0x8073d8.
inline constexpr unsigned actor_pool_slots = 0x300;

// --- flag bits on the actor flag word at +4 -----------------------------
// Bit6 is visibility (42ff9b). Bit7 travels with it during the active-visual
// handoff, so45db3d/45db48 move the pair together.
inline constexpr std::uint32_t visual_control_bits = 0xc0;

// 45db3d: raise both visual-control bits on a record. The decompiler cache
// labels this "clear"; the instruction is `or byte [obj+4],0xc0`.
void set_visual_control_bits(const GuestWords& words, Address object);
// 45db48: drop both. The cache label is likewise inverted; the instruction is
// `and byte [obj+4],0x3f`.
void clear_visual_control_bits(const GuestWords& words, Address object);

// --- the pending-move save block at +0x2c..+0x38 -------------------------
// +0x2c is the marker:5 means a walk is in flight,0 means nothing is saved,
// and any other value is the motion state to restore. The three words behind
// it hold the facing, motion frame and target facing captured with it. The
// same three offsets are named path_cost/path_count/path_cursor by the path
// code, which is an alias on the same storage, not a second field.
inline constexpr unsigned pending_move_marker = 0x2c;
inline constexpr unsigned pending_move_facing = 0x30;
inline constexpr unsigned pending_move_frame = 0x34;
inline constexpr unsigned pending_move_target_facing = 0x38;
inline constexpr std::uint32_t pending_move_walking = 5;

// 45db53/45db6c: the0x24-byte block at +0x08 is the live placement, and +0x7c
// is where a callback parks a copy of it across a temporary displacement.
inline constexpr unsigned motion_block_offset = 0x08;
inline constexpr unsigned motion_block_backup = 0x7c;
inline constexpr unsigned motion_block_bytes = 0x24;
void save_motion_block(const GuestWords& words, Address object);
void restore_motion_block(const GuestWords& words, Address object);

// 45d6ab: finish a scripted step. A facing of-1 or below leaves the pose
// untouched and only drops the input-hold bit; otherwise the pending-move
// block is either discarded (a walk was in flight) or played back.
// The optional menu id is opened through45e139 when it is not negative.
using OpenItemMenu = std::function<void(std::uint32_t menu)>;
void clear_pending_move_target(const GuestWords& words, Memory& memory,
                               std::uint32_t selector, std::uint32_t facing,
                               std::uint32_t menu, const OpenItemMenu& open_menu);

// --- table resets --------------------------------------------------------
// 45d9c8: drop the pending-move block on every actor slot in the pool.
void clear_all_pending_moves(Memory& memory);
// 45f47e: empty the live party slot table. Same routine as Actors::reset_party.
void reset_party_slots(Memory& memory);
// 460848: drop the visual-control bits on every party member except the one
// the player is driving, so only the focused actor keeps direct field control.
void release_unfocused_party_visuals(Memory& memory);
// 461841: clear the head word of all32 map tileset/surface resource slots.
void clear_tileset_slot_heads(Memory& memory);
// 45da86: reset the two battle track words.
void reset_battle_track_state(Memory& memory);
// 458d78: finalize and clear the two gatewarp object pairs. Finalizing is
// 45d91d, which stays an explicit call so the callback contract is preserved.
using FinalizeObject = std::function<void(Address object)>;
void release_gatewarp_objects(Memory& memory, const FinalizeObject& finalize);
// 458da9: the gatewarp release plus the three "no special transition" markers.
void prepare_special_map_transition(Memory& memory, const FinalizeObject& finalize);

// --- script-facing words -------------------------------------------------
// 431700/431710: the event flag table is0x44-byte records; only the first
// word of each record is a flag set.
std::uint32_t event_flag_word(const Memory& memory, std::uint32_t index);
void set_event_flag_bit(Memory& memory, std::uint32_t index, std::uint32_t bit);
// 4604df: mark one party member's status panel visible. The mask is saved, so
// this only ever sets; clearing belongs to the party and menu resets.
void show_party_panel(Memory& memory, std::uint32_t panel);
// 460528: queue a map transition that resolves through a save slot rather than
// a direct map id.
void request_slot_map_transition(Memory& memory, std::uint32_t save_slot);
// 45e404: three-way compare with the original's ordering, which is not the
// usual -1/0/1: below is0, equal is2, above is1.
std::uint32_t compare_three_way(std::int32_t left, std::int32_t right);
// 431749: remap a script worldmap mode selector onto the engine mode and enter
// it through436100. Selectors above0xb are the original's assertion.
inline constexpr std::uint32_t worldmap_mode_selectors = 12;
std::uint32_t worldmap_mode_for_selector(std::uint32_t selector);

} // namespace fsb::core::actor_core
