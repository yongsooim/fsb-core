#pragma once
#include "actor_slots.hpp"

// Actor slot allocation and release, the party iterator the event opcodes walk,
// and the handoff that moves field control between party members.
namespace fsb::core::actor_core {

// The party roster the scripts iterate:10 character ids at0x5aaf20, filtered
// through the live slot table each time. The cursor at0x7693ac is shared.
inline constexpr Address party_roster = 0x5aaf20;
inline constexpr unsigned party_roster_slots = 10;
inline constexpr Address party_iterator_cursor = 0x7693ac;

// 43070b: how many roster entries are live right now. The original asserts on
// zero, because every caller then walks that many members.
unsigned live_party_count(const Memory& memory);
// 43075b: point the shared cursor at the roster slot the player drives. The
// original asserts when the player is not on the roster.
void rewind_party_iterator(Memory& memory);
// 4307ac: the next live roster member, advancing the shared cursor past it.
// The original spins forever when nothing is live; live_party_count is the
// guard its callers rely on, so this reports that state instead of hanging.
std::uint32_t next_live_party_member(Memory& memory);
// 430a7f: hide every live roster member.
void hide_all_party(const Memory& memory, const GuestWords& words, const ResolveActor& resolve);

// 45e139: open the item-use menu in the given mode and switch to menu mode.
void open_item_use_menu(Memory& memory, std::uint32_t mode);

// 45d7cb/45d7fc: store a three-word vector. The two entries differ only in the
// assert text they carry; both take a second argument the original never reads.
void write_vector3(const GuestWords& words, Address out,
                   std::uint32_t x, std::uint32_t y, std::uint32_t z);

// The +0x148 callback is an indirect call in the original, so the caller
// supplies the invoker rather than this module guessing the target.
using InvokeActorCallback = std::function<void(Address callback, Address object)>;
// Reported when the0x5a..pool tail has no free record left.
using PoolExhausted = std::function<void()>;

// 45d91d: release a record. Drops its alias chain entry, runs its callback one
// last time with the state marker set to-2, wipes the record and puts the
// identity word back.
void finalize_object(Memory& memory, Address object, const InvokeActorCallback& invoke);
// 45d84b: finalize a slot range and reseat each identity word, then publish the
// new active count. The original simply skips an empty or inverted range.
void reset_slot_range(Memory& memory, std::uint32_t first, std::uint32_t end,
                      const InvokeActorCallback& invoke);

// 45d89c allocates from the tail of the pool, past the0x5a script slots.
inline constexpr Address spare_pool_first_flags = 0x810a54;
inline constexpr Address spare_pool_flags_limit = 0x85712c;
inline constexpr std::uint32_t slot_in_use_bits = 0x10840;
// The allocation half on its own; returns0 when the tail is full.
Address allocate_spare_slot(Memory& memory);
// 45d89c: allocate, and when a callback is supplied bind it and run it once
// with the state marker at-1 so it can initialise its own record.
Address spawn_callback_object(Memory& memory, Address callback,
                              const InvokeActorCallback& invoke,
                              const PoolExhausted& exhausted = {});

// 45d9e7: copy the render and motion slice between two records. This is the
// placement, the0xc8-byte path command block, the motion lanes and the two
// facings - not identity, callback ownership or the work fields above +0x114.
void copy_render_state(const GuestWords& words, Address destination, Address source);

// 45da95: rebuild the standalone template record at0x857128 that the floating
// object callback runs from.
inline constexpr Address global_motion_template = 0x857128;
void init_global_motion_template(Memory& memory, std::uint32_t x, std::uint32_t y, std::uint32_t z);

// 45db85: run a record's callback once under a temporary state marker, with an
// argument parked at +0x1a8. The marker is put back only if the callback left
// it alone, so a callback that re-targets itself keeps its new state.
void invoke_callback_substate(Memory& memory, Address object, std::uint32_t substate,
                              std::uint32_t argument, const InvokeActorCallback& invoke);

// 45f495/45f554: move field control from the active party slot to another one.
// Both copy the render state across, swap the visual-control bits, move the
// field movement callback and republish the active slot. 45f495 also updates
// the menu character index; 45f554 does not.
void hand_off_field_control(Memory& memory, const GuestWords& words,
                            std::uint32_t from_slot, std::uint32_t to_slot,
                            bool publish_character_index);
// 45efb6: put a character snapshot back. Rebuilds the party from the ids the
// snapshot holds, re-seats the active slot and places its actor on the tile the
// snapshot recorded. Spawning a slot is 45f0e9, supplied by the caller.
using SpawnPartySlot = std::function<void(std::uint32_t character)>;
inline constexpr Address character_snapshot_slot = 0x803a3c;
inline constexpr Address character_snapshot_active = 0x803a08;
inline constexpr Address character_snapshot_count = 0x803a30;
inline constexpr Address character_snapshot_members = 0x803960;
inline constexpr unsigned character_snapshot_member_bytes = 40;
inline constexpr Address character_snapshot_place = 0x8039d8;
inline constexpr unsigned character_snapshot_place_bytes = 0x10;
void restore_character_snapshot(Memory& memory, const GuestWords& words, std::uint32_t index,
                                const SpawnPartySlot& spawn);

// 45c110: the passive actor tick. Steps the shared motion, then rebuilds the
// integer tile cache and the world pixel position from the Q16 tile lanes.
using StepActorMotion = std::function<void(Address object)>;
void tick_passive_actor(const GuestWords& words, Address object, const StepActorMotion& step);

// 45f495: step to the requested slot, or wrap to the next one when it is-1.
void focus_next_party_slot(Memory& memory, const GuestWords& words, std::uint32_t target);
// 45f554: focus the slot holding a character id. Returns false, changing
// nothing, when that character is absent or already driving.
bool focus_party_character(Memory& memory, const GuestWords& words, std::uint32_t character);

} // namespace fsb::core::actor_core
