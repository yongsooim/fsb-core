#include "fsb_core/actor_core/player_input.hpp"
#include "fsb_core/actor_core/motion_callbacks.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core::actor_core {
namespace {
constexpr std::uint32_t keydown = 0x100;
// Bit30 of the input flags marks a key pressed with Alt held, which this
// callback ignores entirely.
constexpr std::uint32_t alt_held = 0x40000000;
constexpr std::uint32_t key_left = 0x25, key_up = 0x26, key_right = 0x27, key_down = 0x28;
constexpr std::uint32_t key_confirm_x = 0x58, key_confirm_enter = 0x0d, key_confirm_pad = 0x61;
constexpr std::uint32_t key_cancel_z = 0x5a, key_cancel_escape = 0x1b;
// A pad cancel arrives as a scan code rather than a virtual key.
constexpr std::uint32_t cancel_scan_signature = 0x00520000;
constexpr std::uint32_t scan_extended_mask = 0x800000, scan_code_mask = 0xff0000;

// The four directions, in facing order, with the two input states and the hold
// counter that belongs to each.
struct PlayerDirection { Address held, pressed; std::uint32_t key; };
constexpr PlayerDirection player_directions[] = {
    {globals::up_held, 0x6da688, key_up},
    {globals::down_held, 0x6da6a8, key_down},
    {globals::left_held, 0x6da694, key_left},
    {globals::right_held, 0x6da69c, key_right},
};

bool plain_keydown(const Memory& memory) {
    return memory.read(globals::input_message) == keydown &&
           !(memory.read(globals::input_flags) & alt_held);
}
// A pad cancel folds its extended bit down beside the scan code.
std::uint32_t scan_signature(const Memory& memory) {
    const auto flags = memory.read(globals::input_flags);
    return ((signed32(flags) >> 1) & scan_extended_mask) | (flags & scan_code_mask);
}
// Each held direction has to survive a few frames before it repeats.
bool repeat_ready(Memory& memory, unsigned direction) {
    const auto counter = player_repeat_counters + direction * 4;
    const auto next = signed32(memory.read(counter)) + 1;
    memory.write(counter, std::uint32_t(next));
    return next > std::int32_t(player_repeat_threshold);
}

std::uint32_t decode_direction(Memory& memory) {
    auto chosen = player_no_direction;
    if (plain_keydown(memory)) {
        // A fresh press wins outright. The original tests left, up, right, down
        // in that order, which only matters if two keys arrive at once.
        for (auto key : {key_left, key_up, key_right, key_down})
            if (memory.read(globals::input_key) == key) {
                for (unsigned index = 0; index < 4; ++index)
                    if (player_directions[index].key == key) chosen = index;
                break;
            }
    } else {
        // Otherwise the first held direction that has waited long enough wins,
        // tested up, down, left, right.
        for (unsigned index = 0; index < 4; ++index) {
            const auto& direction = player_directions[index];
            if (!memory.read(direction.held) && !memory.read(direction.pressed)) continue;
            if (repeat_ready(memory, index)) chosen = index;
            break;
        }
    }
    // Any accepted direction restarts every counter.
    if (chosen < player_no_direction)
        for (unsigned index = 0; index < 4; ++index)
            memory.write(player_repeat_counters + index * 4, 0);
    return chosen;
}
} // namespace

std::uint32_t step_turn_arc(std::uint32_t current, std::uint32_t target) {
    const auto here = signed32(current), there = signed32(target);
    if (there < here) return std::uint32_t(here - there < 4 ? here + 7 : here + 1);
    return std::uint32_t(there - here < 5 ? here + 1 : here + 7);
}

void tick_player_command(Memory& memory, const GuestWords& words, Address object,
                         const PlayerCommandHooks& hooks) {
    const auto state = words.read(object + actor_offset::motion_state);
    if (state == player_state_idle) {
        const auto direction = decode_direction(memory);
        if (direction < player_no_direction) {
            // With nothing blocking input the direction is a placement request;
            // otherwise the actor just turns to face it.
            if (!memory.read(globals::shift_key_state) && !memory.read(0x6da698) &&
                !memory.read(0x6da6a0)) {
                if (hooks.validate_placement) hooks.validate_placement(direction);
            } else if (direction != memory.read(tables::direction_to_cardinal +
                                                words.read(object + actor_offset::facing) * 4)) {
                words.write(object + actor_offset::motion_state, player_state_turning);
                words.write(object + actor_offset::target_facing, direction);
            }
        }
    } else if (state == player_state_turning) {
        const auto facing = words.read(object + actor_offset::facing);
        if (words.read(object + actor_offset::target_facing) == facing) {
            words.write(object + actor_offset::motion_state, player_state_idle);
            if (hooks.start_handler) hooks.start_handler(facing);
        } else {
            // One eighth-turn per frame, the short way round.
            const auto target = memory.read(direction_to_arc +
                                            words.read(object + actor_offset::target_facing) * 4);
            const auto stepped = step_turn_arc(memory.read(direction_to_arc + facing * 4), target);
            words.write(object + actor_offset::facing,
                        memory.read(arc_to_direction + (stepped % turn_arc_steps) * 4));
        }
    }
    // Confirm and cancel are read even on a frame that did not move, and both
    // hand the actor back to field movement.
    if (memory.read(globals::input_message) == keydown) {
        const auto release = [&] {
            words.write(object + actor_offset::flags,
                        words.read(object + actor_offset::flags) & ~player_taking_input_bit);
            words.write(object + actor_offset::callback, routines::field_actor_movement);
        };
        const auto key = memory.read(globals::input_key);
        if (plain_keydown(memory) &&
            (key == key_confirm_x || key == key_confirm_enter || key == key_confirm_pad) &&
            hooks.select_followup && hooks.select_followup()) {
            release();
            memory.write(engine_substate, player_returned_to_field);
        }
        if (plain_keydown(memory) &&
            (key == key_cancel_z || key == key_cancel_escape ||
             scan_signature(memory) == cancel_scan_signature)) {
            release();
            if (hooks.raise_cancel_effect) hooks.raise_cancel_effect();
        }
    }
    if (hooks.resolve_frame) hooks.resolve_frame(object);
}
} // namespace fsb::core::actor_core
