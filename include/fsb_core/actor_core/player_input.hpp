#pragma once
#include "actor_lifecycle.hpp"

// The callback that drives a party actor while the engine is waiting for a
// battle command. It turns arrow input into one of the four cardinal
// directions, rotates the actor toward it one eighth-turn per frame, and
// accepts the confirm and cancel keys that hand control back to field movement.
namespace fsb::core::actor_core {

// A fresh key press wins immediately and clears every hold counter; a held
// direction only repeats once its own counter passes the threshold.
inline constexpr Address player_repeat_counters = 0x8021c8;
inline constexpr unsigned player_repeat_threshold = 3;
// 0 up, 1 down, 2 left, 3 right; 4 means nothing was asked for.
inline constexpr std::uint32_t player_no_direction = 4;

// The eight-step turn ring and the two tables that map between a facing and
// its place on it.
inline constexpr Address direction_to_arc = 0x5d05c8;
inline constexpr Address arc_to_direction = 0x5d05a8;
inline constexpr unsigned turn_arc_steps = 8;

inline constexpr unsigned player_state_idle = 0;
inline constexpr unsigned player_state_turning = 5;
// Cleared when the actor stops taking commands.
inline constexpr std::uint32_t player_taking_input_bit = 0x2000;
inline constexpr Address engine_substate = 0x775cac;
inline constexpr unsigned player_returned_to_field = 5;

// Which way an actor should step around the turn ring to reach a target
// facing: forward when the short way round is forward, backward otherwise.
// The two halves are not symmetric - the original allows four steps clockwise
// but five counter-clockwise before it turns the other way.
std::uint32_t step_turn_arc(std::uint32_t current, std::uint32_t target);

// 45c14f. The four calls out are the battle placement check, the battle
// handler the turn completes into, the follow-up the confirm key asks for and
// the line effect cancel raises; all four belong to the battle session.
struct PlayerCommandHooks {
    std::function<void(std::uint32_t direction)> validate_placement;
    std::function<void(std::uint32_t facing)> start_handler;
    std::function<bool()> select_followup;
    std::function<void()> raise_cancel_effect;
    std::function<void(Address object)> resolve_frame;
};
void tick_player_command(Memory& memory, const GuestWords& words, Address object,
                         const PlayerCommandHooks& hooks);

} // namespace fsb::core::actor_core
