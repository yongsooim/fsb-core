#pragma once
#include "actor_lifecycle.hpp"

// The gatewarp transition sprites.
//
// A gatewarp runs off one shared frame counter at0x5d0768. Every sprite
// divides it by three to get a bucket and looks its frame up in its own table;
// a negative entry means "hidden this bucket". Two families exist:
//
//  * three sprites attached to an actor (4580f7/45826c/4583cd), spawned as a
//    group by45852e. Each frame they re-follow the actor slot their group
//    index selects and sit at a fixed offset from it.
//  * four screen sprites (458691/4588ee), spawned by458b79 at a fixed tile.
//
// The original builds each table on its own stack frame every call; they are
// constants, so they live here.
namespace fsb::core::actor_core {

inline constexpr Address gatewarp_frame_counter = 0x5d0768;
inline constexpr Address gatewarp_state = 0x8021b0;
inline constexpr Address gatewarp_sequence_mode = 0x8021a0;
inline constexpr Address gatewarp_paired_actor_slot = 0x5d0a40;
inline constexpr unsigned gatewarp_frames_per_bucket = 3;
inline constexpr std::uint32_t gatewarp_hidden_selector = 0xf7;
inline constexpr std::uint32_t gatewarp_hidden_frame = 0x2a;
inline constexpr std::uint32_t gatewarp_attached_selector = 0x14d;
inline constexpr std::uint32_t gatewarp_screen_selector = 0x14e;
inline constexpr std::uint32_t gatewarp_sprite_sheet = 0xd0;
inline constexpr std::uint32_t gatewarp_draw_bit = 0x40;
// +0x160 carries the group index for attached sprites and the map id for
// screen sprites; +0x38 marks the sprite that advances the shared counter.
inline constexpr unsigned gatewarp_payload = 0x160;
inline constexpr unsigned gatewarp_frame_advancer = 0x38;

// Which sprite family a callback belongs to, and hence its frame table.
enum class GatewarpSprite { AttachedA, AttachedB, AttachedC, ScreenA, ScreenB };

// 4580f7/45826c/4583cd: follow the actor this group tracks and pick the frame.
// Does nothing while the record's callback state is negative, which is how the
// spawn path keeps a sprite quiet until the group is live.
void tick_attached_gatewarp(Memory& memory, const GuestWords& words, Address effect,
                            GatewarpSprite sprite);
// 458691: a screen sprite only picks a frame; it never moves.
void tick_screen_gatewarp(Memory& memory, const GuestWords& words, Address effect);

// 4588ee: the same, plus the timeline the whole gatewarp runs on.
// gatewarp_state is0 while the opening loop repeats,1 once something has
// asked for the loop to end, and2 once the timeline is running. The sprite
// that owns the advancer flag steps the shared counter during the loop and
// wraps it; everything stays hidden until the timeline starts.
inline constexpr std::uint32_t gatewarp_state_looping = 0;
inline constexpr std::uint32_t gatewarp_state_leaving_loop = 1;
inline constexpr std::uint32_t gatewarp_state_timeline = 2;
inline constexpr std::int32_t gatewarp_loop_restart_bucket = 0;
inline constexpr std::int32_t gatewarp_loop_last_frame = 0xe;
void tick_gatewarp_timeline(Memory& memory, const GuestWords& words, Address effect);

// 45852e: spawn one three-sprite group attached to an actor.
void spawn_attached_gatewarp_group(Memory& memory, std::uint32_t group, Address source_actor,
                                   const InvokeActorCallback& invoke);
// 458b79: spawn the four screen sprites and arm the shared counter. Loading the
// palette and starting the cue are platform services the caller supplies.
using GatewarpPresentation = std::function<void()>;
void spawn_screen_gatewarp(Memory& memory, const InvokeActorCallback& invoke,
                           const GatewarpPresentation& present);

} // namespace fsb::core::actor_core
