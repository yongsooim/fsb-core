#include "fsb_core/combat/actions.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core::combat {
namespace {
using namespace actions;
} // namespace

void Actions::play_caster_script(Address scripts) {
    const auto actor = memory_.read(acting_actor);
    memory_.write(actor + 6, memory_.read(actor + 6, 1) & ~hidden_bit, 1);
    const auto facing = memory_.read(memory_.read(acting_actor) + actor_offset::facing);
    if (start_script) start_script(memory_.read(acting_actor), memory_.read(scripts + facing * 4));
}
unsigned Actions::reporting_target(Address controller) const {
    const auto count = signed32(memory_.read(target_count));
    const auto reporter = memory_.read(controller + controller_owner);
    std::int32_t index = 0;
    while (index < count && memory_.read(action_target + std::uint32_t(index) * 4) != reporter) ++index;
    return unsigned(index);
}
bool Actions::all_targets_settled(Address controller) const {
    const auto count = signed32(memory_.read(target_count));
    std::int32_t settled = 0;
    while (settled < count && memory_.read(controller + per_target_flags + std::uint32_t(settled) * 2, 2) == 0)
        ++settled;
    return settled == count;
}
void Actions::run_target_effects(Address controller, Address scripts, const TargetAction& shape,
                                 const std::function<void(Address target)>& spawn_for_target) {
    const auto state = signed32(memory_.read(controller + actor_offset::callback_state));
    const auto count = signed32(memory_.read(target_count));
    const auto adjust = [&](Address counter, int by) {
        memory_.write(controller + counter,
                      std::uint16_t(memory_.read(controller + counter, 2) + by), 2);
    };
    const auto advance = [&] {
        memory_.write(controller + actor_offset::callback_state,
                      memory_.read(controller + actor_offset::callback_state) + 10);
    };
    if (state == shape.effect_done_state) { adjust(pending_effects, -1); return; }
    if (shape.landed_counter && state == hit_state) {
        // Once every effect has landed the action opens its gate.
        adjust(shape.landed_counter, 1);
        if (std::int32_t(memory_.read(controller + shape.landed_counter, 2)) == count)
            memory_.write(shape.landed_gate, 1);
        return;
    }
    if (shape.held_cue && state == shape.release_cue_state) { if (stop_cue) stop_cue(shape.held_cue); return; }
    if (state == done_state) {
        // The target that reported reacts: it stops, plays its own script and
        // takes the effect this action leaves on it.
        const auto index = reporting_target(controller);
        // The first target to react opens the cue the action holds.
        if (shape.held_cue && index == 0) if (play_cue) play_cue(shape.held_cue);
        const auto target = memory_.read(action_target + index * 4);
        if (stop_motion) stop_motion(target);
        const auto facing = shape.per_facing ? memory_.read(target + actor_offset::facing) : 0;
        if (start_script) start_script(target, memory_.read(shape.target_script_table + facing * 4));
        memory_.write(target + 6, memory_.read(target + 6, 1) & ~hidden_bit, 1);
        memory_.write(controller + per_target_flags + index * 2,
                      memory_.read(controller + per_target_flags + index * 2, 1) | 1u, 1);
        const auto selector = memory_.read(shape.selector_table + facing * 4);
        if (spawn_effect)
            spawn_effect(signed32(memory_.read(target + actor_offset::world_x)),
                         signed32(memory_.read(target + actor_offset::world_y) + effect_row),
                         signed32(memory_.read(target + actor_offset::elevation) + effect_lift),
                         memory_.read(target + actor_offset::layer_q16),
                         memory_.read(effect_scripts + selector * 4));
        return;
    }
    if (state == shown_state && shape.counts_reports) { adjust(pending_reports, -1); return; }
    if (state == opened_state) {
        for (std::int32_t i = 0; i < count; ++i) {
            spawn_for_target(memory_.read(action_target + std::uint32_t(i) * 4));
            adjust(pending_effects, 1);
        }
        memory_.write(controller + actor_offset::callback_state, 30);
        return;
    }
    if (state >= missed_low && state <= missed_high) {
        // A target that took nothing hides again and clears its flag.
        const auto index = reporting_target(controller);
        const auto reporter = memory_.read(controller + controller_owner);
        memory_.write(reporter + 6, memory_.read(reporter + 6, 1) | hidden_bit, 1);
        memory_.write(controller + per_target_flags + index * 2,
                      std::uint16_t(memory_.read(controller + per_target_flags + index * 2, 2) & ~1u), 2);
        return;
    }
    if (state == -1) {
        if (shape.reset_cue) if (play_cue) play_cue(shape.reset_cue);
        if (shape.clear_on_reset) memory_.write(shape.clear_on_reset, 0);
        for (unsigned i = 0; i < shape.reset_bytes; ++i)
            memory_.write(controller + per_target_flags + i, 0, 1);
        return;
    }
    if (state == 0) { if (screen_flash && screen_flash(true, flash_ticks)) advance(); return; }
    if (state == 10) { play_caster_script(scripts); advance(); return; }
    if (state == 30) {
        if (memory_.read(controller + pending_effects, 2)) return;
        if (memory_.read(controller + pending_reports, 2)) return;
        if (!all_targets_settled(controller)) return;
        memory_.write(controller + actor_offset::callback_state, 40);
        return;
    }
    if (state != 40) return;
    if (screen_flash && screen_flash(false, flash_ticks))
        if (release_object) release_object(controller);
}
void Actions::run_orbit(Address controller, Address scripts) {
    const auto state = signed32(memory_.read(controller + actor_offset::callback_state));
    const auto count = signed32(memory_.read(target_count));
    const auto adjust = [&](Address counter, int by) {
        memory_.write(controller + counter,
                      std::uint16_t(memory_.read(controller + counter, 2) + by), 2);
    };
    const auto advance = [&] {
        memory_.write(controller + actor_offset::callback_state,
                      memory_.read(controller + actor_offset::callback_state) + 10);
    };
    if (state == done_state) { adjust(pending_effects, -1); return; }
    if (state == hit_state) {
        // The orbit that broke shows its target's amount and scatters.
        const auto index = reporting_target(controller);
        const auto target = memory_.read(action_target + index * 4);
        memory_.write(target + 6, memory_.read(target + 6, 1) | hidden_bit, 1);
        if (show_amount)
            show_amount(target, signed32(memory_.read(target_amounts + index * target_amount_stride)));
        if (spawn_swirl) spawn_swirl(target);
        adjust(pending_reports, 1);
        return;
    }
    if (state == opened_state) {
        for (std::int32_t i = 0; i < count; ++i) {
            const auto target = memory_.read(action_target + std::uint32_t(i) * 4);
            // Only a target flagged for a reaction plays one.
            if (memory_.read(target + 5, 1) & reacts_bit) {
                const auto facing = memory_.read(target + actor_offset::facing);
                if (start_script) start_script(target, memory_.read(orbit_target_scripts + facing * 4));
            }
            memory_.write(target + 6, memory_.read(target + 6, 1) & ~hidden_bit, 1);
            if (spawn_orbit) spawn_orbit(target);
            adjust(pending_effects, 1);
        }
        memory_.write(controller + actor_offset::callback_state, 30);
        return;
    }
    if (state == shown_state) { adjust(pending_reports, -1); return; }
    if (state == 0) { if (screen_flash && screen_flash(true, flash_ticks)) advance(); return; }
    if (state == 10) { play_caster_script(scripts); advance(); return; }
    if (state == 30) {
        if (memory_.read(controller + pending_effects, 2)) return;
        if (memory_.read(controller + pending_reports, 2)) return;
        if (!all_targets_settled(controller)) return;
        memory_.write(controller + actor_offset::callback_state, 40);
        return;
    }
    if (state != 40) return;
    if (screen_flash && screen_flash(false, flash_ticks))
        if (release_object) release_object(controller);
}
void Actions::run_beam(Address controller, Address scripts) {
    const auto state = signed32(memory_.read(controller + actor_offset::callback_state));
    const auto advance = [&] {
        memory_.write(controller + actor_offset::callback_state,
                      memory_.read(controller + actor_offset::callback_state) + 10);
    };
    if (state == hit_state) { memory_.write(controller + actor_offset::callback_state, 30); return; }
    if (state == opened_state) { if (spawn_beam) spawn_beam(memory_.read(action_target)); return; }
    if (state == 0) {
        // Wait for the screen to finish darkening before the caster moves.
        if (screen_flash && screen_flash(true, flash_ticks)) advance();
        return;
    }
    if (state == 10) { play_caster_script(scripts); advance(); return; }
    if (state != 30) return;
    if (screen_flash && screen_flash(false, flash_ticks))
        if (release_object) release_object(controller);
}
} // namespace fsb::core::combat
