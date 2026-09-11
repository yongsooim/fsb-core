#pragma once
#include "../primitives.hpp"
#include <functional>

// Actor slot identity and the small per-actor state the sequence opcodes read
// and write. Every function here is a semantic reconstruction of one original
// routine; the original entry point is named on each declaration so the
// contract stays checkable against the executable.
//
// Storage is still Memory: this stage moves the logic out of the virtual
// register machine, it does not yet move actor state into its own store.
namespace fsb::core::actor_core {

// 42feaa. Selectors below this are script actor ids; at or above it the value
// already is an actor record address. Shared with the profile-flag opcodes.
inline constexpr std::uint32_t direct_object_selector = 0x10000;

// 4300e1/430434: the sprite selector value that means "play the frame row".
inline constexpr std::uint32_t animated_sprite_selector = 0x20000;

// 457ec9 walks the alias chain between these bounds. The low scan stops above
// index0x10 and the high scan runs while the record pointer stays below
// 0x5be6d0, which is index0x2a0 - four records past the0x29c entry guard.
inline constexpr std::uint32_t alias_scan_low_exclusive = 0x10;
inline constexpr std::uint32_t alias_entry_guard = 0x29c;
inline constexpr std::uint32_t alias_scan_high_exclusive = 0x2a0;

// 42feb9 is the shared selector-to-record boundary, and it is owned outside
// this module. Routines that call it take the resolver as a parameter so the
// original call edge survives and hosts can still intercept it.
using ResolveActor = std::function<Address(std::uint32_t selector)>;
// The plain in-memory resolver, for product paths with no interception.
ResolveActor memory_resolver(const Memory& memory);

// Word access for the pointers a caller hands in. Legacy callers still pass
// frame locals that live in the guest call stack rather than in Memory, so the
// medium is chosen at the boundary instead of assumed here.
struct GuestWords {
    std::function<std::uint32_t(Address)> read;
    std::function<void(Address, std::uint32_t)> write;
};
GuestWords memory_words(Memory& memory);

// 42feaa: the shared16-bit script id guard.
bool is_script_selector(std::uint32_t selector);

// 457ec9: resolve a script actor slot through the alias chain table. Returns0
// when no chain entry claims the slot. Callers decide whether that is an error.
Address resolve_slot(const Memory& memory, std::uint32_t slot);

// 42fecd: normalize an opcode operand.0xffffffff means the sequence's current
// actor at +0xfc, script ids resolve through the chain, object addresses pass
// through unchanged. This one reaches457ec9 directly, not through42feb9.
Address resolve_selector(const Memory& memory, Address sequence, std::uint32_t selector);

// 42fef2: cache the resolved actor object on the sequence that owns it.
Address attach_sequence_actor(Memory& memory, Address sequence, const ResolveActor& resolve);

// 4302d2: resolve a selector for the opcodes that tolerate a missing actor.
// The original reports the failure through the shared error log and still
// returns0, so the diagnostic stays an injected boundary rather than a
// silently dropped effect.
using MissingActorReport = std::function<void(std::uint32_t selector)>;
Address resolve_or_report(std::uint32_t selector, const ResolveActor& resolve,
                          const MissingActorReport& report = {});

// 42ff47: the visibility bit. Faults on a null record the way the original
// asserts valid_pobj.
bool visible(const GuestWords& words, Address object);
// 42ff81: same query addressed by selector; an unresolved selector reads false.
bool visible_by_selector(const GuestWords& words, std::uint32_t selector,
                         const ResolveActor& resolve);
// 430005: set visibility by selector. Mode is0 or1; anything else is the
// original's second assertion. A selector that resolves to nothing is a no-op.
void set_visible_by_selector(const GuestWords& words, std::uint32_t selector,
                             std::uint32_t mode, const ResolveActor& resolve);

// 43001f: the elevated/tile-state bit sampled before the state opcode writes it.
bool tile_state(const GuestWords& words, Address object);

// 4301a8/4301b9/4301ca: the two facing lanes. +0x110 is the live facing the
// frame row is built from, +0x114 is the facing motion is steering toward.
// These are direction fields; nothing here resets an animation frame.
void set_facing(const GuestWords& words, Address object, std::uint32_t facing);
void set_target_facing(const GuestWords& words, Address object, std::uint32_t facing);
void set_both_facings(const GuestWords& words, Address object, std::uint32_t facing);

// 4300e1: rebuild the sprite frame row from the live facing and put the sprite
// selector back into its animated mode. The eight rows are the original table;
// an out-of-range facing is the original valid-pose assertion.
void reset_pose_row(const GuestWords& words, Address object);
// 43036f: resolve a selector, then mirror the facing into both lanes.
void turn_by_selector(const GuestWords& words, std::uint32_t selector, std::uint32_t facing,
                      const ResolveActor& resolve, const MissingActorReport& report = {});

// 45f528: is this character id currently in the live party slot table.
bool character_active(const Memory& memory, std::uint32_t character);
// 4306cb: the opcode-facing wrapper, which asserts the id is a script id first.
bool actor_exists(const Memory& memory, std::uint32_t character);
// 457e56: the character id of the party slot the player currently drives.
std::uint32_t player_character(const Memory& memory);

} // namespace fsb::core::actor_core
