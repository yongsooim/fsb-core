#include "fsb_core/visual_effects/sequences.hpp"
#include "fsb_core/visual_effects/directional_particles.hpp"
#include "fsb_core/visual_effects/effect_objects.hpp"

namespace fsb::core::visual_effects {
namespace {
template <typename Service>
const Service& required(const Service& service, Address at, const char* what) {
    if (!service) throw Fault(at, what);
    return service;
}
constexpr std::int32_t flash_in = 1, flash_out = 0;
}

Address TargetFanoutSequence::target(std::int32_t index) const {
    return memory_.read(battle::target_objects + 4 * std::uint32_t(index));
}

std::int32_t TargetFanoutSequence::notifying_target_index(Address at) const {
    const EffectObject sequence(memory_, at);
    const auto notifier = Address(sequence.get(field::owner_object));
    const auto count = target_count();
    std::int32_t index = 0;
    while (index < count && target(index) != notifier) ++index;
    return index;
}

bool TargetFanoutSequence::flash(Address at, std::int32_t mode) {
    return required(services_.screen_flash_transition, at, "screen flash service is not attached")(
               mode, flash_frames) != 0;
}

void TargetFanoutSequence::run_actor_script(Address actor_scripts) {
    EffectObject actor(memory_, memory_.read(battle::active_actor));
    actor.clear(sweep_flag::target_visual_busy);
    required(services_.run_effect_script, actor.address(), "effect script service is not attached")(
        actor.address(), memory_.read(actor_scripts + 4 * memory_.read(actor.address() + field::facing)));
}

void TargetFanoutSequence::adjust_pending(Address at, Address counter, int delta) {
    EffectObject sequence(memory_, at);
    sequence.set_half(counter, std::int16_t(sequence.half(counter) + delta));
}

void TargetFanoutSequence::recover_targets(Address at, Address actor_scripts) {
    EffectObject sequence(memory_, at);
    constexpr Address counter = sweep_field::ring_children_pending;
    switch (sequence.phase()) {
    case phase::notify_target_hit:
        adjust_pending(at, counter, -1);
        return;
    case phase::notify_actor_done: {
        const auto count = target_count();
        for (std::int32_t index = 0; index < count; ++index) {
            RecoverTargetChild(memory_, services_).spawn(target(index));
            adjust_pending(at, counter, 1);
        }
        sequence.set_phase(30);
        return;
    }
    case 0:
        if (!flash(at, flash_in)) return;
        break;
    case 10:
        run_actor_script(actor_scripts);
        break;
    case 30:
        if (sequence.half(counter) != 0) return;
        sequence.set_phase(40);
        return;
    case 40:
        if (!flash(at, flash_out)) return;
        required(services_.finalize_object, at, "object release service is not attached")(at);
        return;
    default:
        return;
    }
    sequence.set_phase(sequence.phase() + 10);
}

void TargetFanoutSequence::shared_status_buff(Address at, Address actor_scripts) {
    EffectObject sequence(memory_, at);
    // This one keeps its count in the per-target flag array's first entry.
    constexpr Address counter = sweep_field::target_pending_flags;
    switch (sequence.phase()) {
    case phase::notify_target_hit: {
        // The target that reported becomes visible again as its child leaves.
        EffectObject reported(memory_, target(notifying_target_index(at)));
        reported.raise(sweep_flag::target_visual_busy);
        adjust_pending(at, counter, -1);
        return;
    }
    case phase::notify_actor_done: {
        const auto count = target_count();
        for (std::int32_t index = 0; index < count; ++index) {
            EffectObject object(memory_, target(index));
            StatusBuffChild(memory_, services_).spawn(object.address());
            object.clear(sweep_flag::target_visual_busy);
            adjust_pending(at, counter, 1);
        }
        sequence.set_phase(30);
        return;
    }
    case 0:
        if (!flash(at, flash_in)) return;
        break;
    case 10:
        run_actor_script(actor_scripts);
        break;
    case 30: {
        if (sequence.half(counter) != 0) return;
        EffectObject(memory_, memory_.read(battle::active_actor)).raise(sweep_flag::target_visual_busy);
        break;
    }
    case 40:
        if (!flash(at, flash_out)) return;
        required(services_.finalize_object, at, "object release service is not attached")(at);
        return;
    default:
        return;
    }
    sequence.set_phase(sequence.phase() + 10);
}

void RecoverTargetChild::spawn(Address at) {
    EffectObject child(memory_, required(services_.spawn_object, at,
                                         "effect object spawn service is not attached")(child_callback));
    child.set(field::linked_object, std::int32_t(at));
    required(services_.play_cue, at, "effect cue service is not attached")(0x113);
}

void StatusBuffChild::spawn(Address at) {
    const EffectObject from(memory_, at);
    EffectObject child(memory_, required(services_.spawn_object, at,
                                         "effect object spawn service is not attached")(child_callback));
    child.set(field::sprite_id, from.get(field::sprite_id));
    child.set(field::x, from.get(field::x));
    child.set(field::y, from.get(field::y) + 0x10000);
    child.set(field::z, from.get(field::z));
    required(services_.run_effect_script, at, "effect script service is not attached")(
        child.address(), child_script);
    child.set(field::linked_object, std::int32_t(at));
    required(services_.play_cue, at, "effect cue service is not attached")(0x12f);
}

bool TargetFanoutSequence::drained(Address at) const {
    const EffectObject sequence(memory_, at);
    if (sequence.half(sweep_field::ring_children_pending) != 0) return false;
    if (sequence.half(sweep_field::numbers_pending) != 0) return false;
    const auto count = target_count();
    std::int32_t index = 0;
    while (index < count &&
           sequence.half(sweep_field::target_pending_flags + 2 * std::uint32_t(index)) == 0) ++index;
    return index == count;
}

void TargetFanoutSequence::all_target_lift_numbers(Address at, Address actor_scripts) {
    EffectObject sequence(memory_, at);
    switch (sequence.phase()) {
    case phase::notify_ring_done:
        adjust_pending(at, sweep_field::ring_children_pending, -1);
        return;
    case phase::notify_target_hit: {
        const auto index = notifying_target_index(at);
        required(services_.spawn_floating_number_glyphset, at, "floating number service is not attached")(
            target(index),
            std::int32_t(memory_.read(battle::result_amounts +
                                      battle::result_record_bytes * std::uint32_t(index))));
        adjust_pending(at, sweep_field::numbers_pending, 1);
        return;
    }
    case phase::notify_numbers_done:
        adjust_pending(at, sweep_field::numbers_pending, -1);
        return;
    case phase::notify_actor_done: {
        const auto count = target_count();
        for (std::int32_t index = 0; index < count; ++index) {
            LiftNumberChild(memory_, services_).spawn(target(index));
            adjust_pending(at, sweep_field::ring_children_pending, 1);
        }
        sequence.set_phase(30);
        return;
    }
    case 0:
        if (!flash(at, flash_in)) return;
        break;
    case 10:
        run_actor_script(actor_scripts);
        break;
    case 30:
        if (!drained(at)) return;
        sequence.set_phase(40);
        return;
    case 40:
        if (!flash(at, flash_out)) return;
        required(services_.finalize_object, at, "object release service is not attached")(at);
        return;
    default:
        return;
    }
    sequence.set_phase(sequence.phase() + 10);
}

void TargetFanoutSequence::descending_particle_burst(Address at, Address actor_scripts) {
    EffectObject sequence(memory_, at);
    // Two 16-bit counters of its own, not the per-target slot array: one says
    // the acting actor's script is still running, the other counts the numbers.
    constexpr Address actor_running = 0x3c, numbers = 0x40;
    const auto state = sequence.phase();

    if (state == phase::notify_numbers_ready) {
        const auto count = target_count();
        for (std::int32_t index = 0; index < count; ++index) {
            required(services_.spawn_floating_number, at, "floating number service is not attached")(
                target(index),
                std::int32_t(memory_.read(battle::result_amounts +
                                          battle::result_record_bytes * std::uint32_t(index))));
            adjust_pending(at, numbers, 1);
        }
        sequence.set_half(actor_running, 0);
        return;
    }
    if (state == phase::notify_actor_done) {
        const auto& run_script = required(services_.run_effect_script, at,
                                          "effect script service is not attached");
        const auto facing = memory_.read(memory_.read(battle::active_actor) + field::facing);
        const auto count = target_count();
        for (std::int32_t index = 0; index < count; ++index) {
            EffectObject object(memory_, target(index));
            required(services_.restore_motion_block, at, "object motion service is not attached")(
                object.address());
            const auto remapped = memory_.read(facing_remap + 4 * facing);
            run_script(object.address(), memory_.read(target_hit_scripts + 4 * remapped));
            object.clear(sweep_flag::target_visual_busy);
            required(services_.spawn_effect_object, at, "effect object spawn service is not attached")(
                object.get(field::x), object.get(field::y) + 0x1900000,
                object.get(field::z) + 0x1c00000, object.get(field::sprite_id),
                memory_.read(burst_descriptors + 4 * facing));
            DescendingBurst(memory_, services_).spawn(object.address());
        }
        return;
    }
    if (state == phase::notify_numbers_done) {
        adjust_pending(at, numbers, -1);
        return;
    }
    // The three codes just above -13 mark one target as busy again.
    if (state <= -10) {
        if (state <= -13) return;
        EffectObject(memory_, Address(sequence.get(field::owner_object)))
            .raise(sweep_flag::target_visual_busy);
        return;
    }
    if (state == 0) {
        run_actor_script(actor_scripts);
        sequence.set_phase(sequence.phase() + 10);
        sequence.set_half(actor_running, 1);
        return;
    }
    if (state != 10) return;
    if (sequence.half(actor_running) != 0 || sequence.half(numbers) != 0) return;
    required(services_.finalize_object, at, "object release service is not attached")(at);
}

void LiftNumberChild::spawn(Address at) {
    EffectObject child(memory_, required(services_.spawn_object, at,
                                         "effect object spawn service is not attached")(child_callback));
    child.set(field::linked_object, std::int32_t(at));
}

void DescendingBurst::spawn(Address at) {
    const auto& create = required(services_.spawn_object, at, "effect object spawn service is not attached");
    const auto& random = required(services_.random, at, "effect random service is not attached");
    const EffectObject from(memory_, at);
    for (unsigned remaining = 8; remaining; --remaining) {
        EffectObject particle(memory_, create(child_callback));
        particle.set(field::sprite_id, from.get(field::sprite_id));
        particle.set(field::x, from.get(field::x));
        particle.set(field::y, from.get(field::y) + 0x1900000);
        particle.set(field::motion_anchor_z, 0);
        particle.set(field::z, from.get(field::z) + 0x1b40000);
        particle.set(field::motion_flags, 0x71);
        particle.set(0x180, 0xcccc);
        // Both draws are centred on the middle of the random range.
        particle.set(field::step_x, (random() - 0x4000) << 4);
        particle.set(field::step_y, (random() - 0x4000) << 4);
    }
}

void DescendingBurst::tick(Address at) {
    EffectObject particle(memory_, at);
    const auto state = particle.phase();
    if (state == -1) {
        required(services_.run_effect_script, at, "effect script service is not attached")(at, child_script);
        return;
    }
    if (state != 0) return;
    required(services_.apply_oscillation, at, "object motion service is not attached")(at);
    if (particle.get(field::z) > 0x1880000) return;
    required(services_.finalize_object, at, "object release service is not attached")(at);
}

bool TargetFanoutSequence::target_slots_clear(Address at) const {
    const EffectObject sequence(memory_, at);
    const auto count = target_count();
    std::int32_t index = 0;
    while (index < count &&
           sequence.half(attack_field::target_slots + 2 * std::uint32_t(index)) == 0) ++index;
    return index == count;
}

void TargetFanoutSequence::clear_target_slots(Address at) {
    for (unsigned offset = 0; offset < attack_field::target_slot_bytes; offset += 4)
        memory_.write(at + attack_field::target_slots + offset, 0);
}

void TargetFanoutSequence::mark_notifier_done(Address at) {
    EffectObject sequence(memory_, at);
    const auto index = notifying_target_index(at);
    const auto notifier = Address(sequence.get(field::owner_object));
    EffectObject(memory_, notifier).raise(sweep_flag::target_visual_busy);
    const auto slot = attack_field::target_slots + 2 * std::uint32_t(index);
    sequence.set_half(slot, std::int16_t(sequence.half(slot) & ~1));
}

void TargetFanoutSequence::open_target_hit(Address at, std::int32_t index) {
    EffectObject sequence(memory_, at);
    EffectObject object(memory_, target(index));
    required(services_.restore_motion_block, at, "object motion service is not attached")(object.address());
    required(services_.run_effect_script, at, "effect script service is not attached")(
        object.address(),
        memory_.read(target_hit_scripts + 4 * memory_.read(object.address() + field::facing)));
    object.clear(sweep_flag::target_visual_busy);
    const auto slot = attack_field::target_slots + 2 * std::uint32_t(index);
    sequence.set_half(slot, std::int16_t(sequence.half(slot) | 1));
    const auto facing = memory_.read(memory_.read(battle::active_actor) + field::facing);
    required(services_.spawn_effect_object, at, "effect object spawn service is not attached")(
        object.get(field::x), object.get(field::y) + 0x1900000,
        object.get(field::z) + 0x1c00000, object.get(field::sprite_id),
        memory_.read(burst_descriptors + 4 * facing));
}

void TargetFanoutSequence::strike_target(Address at, std::int32_t index, std::int32_t bursts) {
    EffectObject object(memory_, target(index));
    required(services_.spawn_floating_number, at, "floating number service is not attached")(
        object.address(),
        std::int32_t(memory_.read(battle::result_amounts +
                                  battle::result_record_bytes * std::uint32_t(index))));
    adjust_pending(at, attack_field::numbers_pending, 1);
    open_target_hit(at, index);
    for (std::int32_t remaining = bursts; remaining > 0; --remaining)
        DescendingBurst(memory_, services_).spawn(object.address());
}

void TargetFanoutSequence::single_target_particle_attack(Address at, std::int32_t bursts) {
    EffectObject sequence(memory_, at);
    const auto state = sequence.phase();
    const auto cursor = [&] { return std::uint16_t(sequence.half(attack_field::target_cursor)); };

    if (state == phase::notify_ready) {
        // Between targets: play the acting actor's repeat animation, unless
        // every target has already had its turn.
        if (std::int32_t(cursor()) >= target_count()) return;
        const auto actor = memory_.read(battle::active_actor);
        required(services_.run_effect_script, at, "effect script service is not attached")(
            actor, memory_.read(single_attack_repeat_scripts + 4 * memory_.read(actor + field::facing)));
        return;
    }
    if (state == phase::notify_actor_done) {
        const auto index = std::int32_t(cursor());
        sequence.set_half(attack_field::target_cursor, std::int16_t(index + 1));
        strike_target(at, index, bursts);
        required(services_.play_cue, at, "effect cue service is not attached")(0x10a);
        required(services_.play_cue, at, "effect cue service is not attached")(0xc0);
        sequence.set_phase(20);
        return;
    }
    if (state == phase::notify_numbers_done) {
        adjust_pending(at, attack_field::numbers_pending, -1);
        return;
    }
    if (state <= -10) {
        if (state <= -13) return;
        mark_notifier_done(at);
        return;
    }
    switch (state) {
    case -1:
        clear_target_slots(at);
        return;
    case 0: {
        EffectObject actor(memory_, memory_.read(battle::active_actor));
        actor.clear(sweep_flag::target_visual_busy);
        required(services_.run_effect_script, at, "effect script service is not attached")(
            actor.address(),
            memory_.read(single_attack_actor_scripts + 4 * memory_.read(actor.address() + field::facing)));
        sequence.set_phase(state + 10);
        return;
    }
    case 20:
        if (std::int32_t(cursor()) != target_count()) return;
        if (sequence.half(attack_field::numbers_pending) != 0) return;
        if (!target_slots_clear(at)) return;
        sequence.set_phase(30);
        return;
    case 30:
        required(services_.finalize_object, at, "object release service is not attached")(at);
        return;
    default:
        return;
    }
}

void TargetFanoutSequence::multitarget_particle_attack(Address at, std::int32_t bursts) {
    EffectObject sequence(memory_, at);
    const auto state = sequence.phase();
    const auto play = [&](unsigned cue) {
        required(services_.play_cue, at, "effect cue service is not attached")(cue);
    };

    if (state == phase::notify_actor_done) {
        play(0x10a);
        const auto count = target_count();
        for (std::int32_t index = 0; index < count; ++index) strike_target(at, index, bursts);
        play(0x10a);
        play(0xbe);
        play(0x10a);
        sequence.set_phase(20);
        return;
    }
    if (state == phase::notify_numbers_done) {
        adjust_pending(at, attack_field::numbers_pending, -1);
        return;
    }
    // This one ignores everything below -12 rather than below -13.
    if (state < -12) return;
    if (state <= -10) {
        mark_notifier_done(at);
        return;
    }
    switch (state) {
    case -1:
        clear_target_slots(at);
        return;
    case 0: {
        EffectObject actor(memory_, memory_.read(battle::active_actor));
        actor.clear(sweep_flag::target_visual_busy);
        required(services_.run_effect_script, at, "effect script service is not attached")(
            actor.address(),
            memory_.read(multitarget_actor_scripts + 4 * memory_.read(actor.address() + field::facing)));
        sequence.set_phase(state + 10);
        return;
    }
    case 20:
        if (sequence.half(attack_field::numbers_pending) != 0) return;
        if (!target_slots_clear(at)) return;
        sequence.set_phase(30);
        return;
    case 30:
        required(services_.finalize_object, at, "object release service is not attached")(at);
        return;
    default:
        return;
    }
}

void TargetFanoutSequence::first_target_directional_burst(Address at, Address actor_scripts) {
    EffectObject sequence(memory_, at);
    const auto state = sequence.phase();
    constexpr Address children = 0x78, reporters = 0x7a, numbers = attack_field::numbers_pending;

    if (state == phase::notify_number_arrived) {
        // The child that was sent up has come back down with the number.
        required(services_.spawn_floating_number, at, "floating number service is not attached")(
            target(0), std::int32_t(memory_.read(battle::result_amounts)));
        adjust_pending(at, numbers, 1);
        adjust_pending(at, reporters, -1);
        return;
    }
    if (state == phase::notify_target_hit) {
        EffectObject object(memory_, target(0));
        required(services_.restore_motion_block, at, "object motion service is not attached")(
            object.address());
        required(services_.run_effect_script, at, "effect script service is not attached")(
            object.address(),
            memory_.read(target_hit_scripts + 4 * memory_.read(object.address() + field::facing)));
        object.clear(sweep_flag::target_visual_busy);
        sequence.set_half(attack_field::target_slots,
                          std::int16_t(sequence.half(attack_field::target_slots) | 1));
        const auto facing = memory_.read(memory_.read(battle::active_actor) + field::facing);
        required(services_.spawn_effect_object, at, "effect object spawn service is not attached")(
            object.get(field::x), object.get(field::y) + 0x1900000,
            object.get(field::z) + 0x1c00000, object.get(field::sprite_id),
            memory_.read(burst_descriptors + 4 * facing));
        // The number rises from a point jittered around the target and reports
        // back with -110 when it gets there.
        const auto jitter = (required(services_.random, at, "effect random service is not attached")()
                             % 0xc - 6) << 16;
        required(services_.spawn_effect_object_with_notify, at,
                 "effect object spawn service is not attached")(
            object.get(field::x) + jitter, object.get(field::y) + 0x7c0000,
            object.get(field::z) + 0xac0000, object.get(field::sprite_id),
            first_target_number_descriptor, phase::notify_number_arrived);
        sequence.set_half(children, 0);
        adjust_pending(at, reporters, 1);
        return;
    }
    if (state == phase::notify_actor_done) {
        DirectionalHitParticles(memory_, services_)
            .spawn_directional_expand(target(0), memory_.read(battle::active_actor));
        adjust_pending(at, children, 1);
        sequence.set_phase(30);
        return;
    }
    if (state == phase::notify_numbers_done) {
        adjust_pending(at, numbers, -1);
        return;
    }
    if (state < -12) return;
    if (state <= -10) {
        mark_notifier_done(at);
        return;
    }
    switch (state) {
    case 0:
        if (!flash(at, flash_in)) return;
        break;
    case 10:
        run_actor_script(actor_scripts);
        break;
    case 30:
        if (sequence.half(children) != 0) return;
        if (sequence.half(reporters) != 0) return;
        if (sequence.half(numbers) != 0) return;
        if (sequence.half(attack_field::target_slots) != 0) return;
        sequence.set_phase(40);
        return;
    case 40:
        if (!flash(at, flash_out)) return;
        required(services_.finalize_object, at, "object release service is not attached")(at);
        return;
    default:
        return;
    }
    sequence.set_phase(sequence.phase() + 10);
}

void TargetFanoutSequence::target_flash_drift(Address at, Address actor_scripts) {
    EffectObject sequence(memory_, at);
    const auto state = sequence.phase();
    constexpr Address cursor = 0x78, pending = 0x7a, drifts_done = 0x7c;
    const auto count = target_count();
    const auto at_cursor = [&] { return std::int32_t(std::uint16_t(sequence.half(cursor))); };

    if (state == phase::notify_drift_done) {
        adjust_pending(at, drifts_done, 1);
        return;
    }
    if (state == phase::notify_ring_done) {
        open_target_hit(at, notifying_target_index(at));
        return;
    }
    if (state == phase::notify_target_hit) {
        const auto index = notifying_target_index(at);
        EffectObject object(memory_, target(index));
        required(services_.spawn_floating_number, at, "floating number service is not attached")(
            object.address(),
            std::int32_t(memory_.read(battle::result_amounts +
                                      battle::result_record_bytes * std::uint32_t(index))));
        DriftEffect(memory_, services_).spawn(object.address());
        adjust_pending(at, pending, 1);
        return;
    }
    if (state == phase::notify_actor_done) {
        sequence.set_half(cursor, 0);
        sequence.set_phase(30);
        return;
    }
    if (state == phase::notify_numbers_done) {
        adjust_pending(at, pending, -1);
        return;
    }
    if (state < -12) return;
    if (state <= -10) {
        mark_notifier_done(at);
        return;
    }
    switch (state) {
    case -1:
        clear_target_slots(at);
        return;
    case 0:
        if (!flash(at, flash_in)) return;
        break;
    case 10:
        run_actor_script(actor_scripts);
        break;
    case 30:
        // One more target starts every eighth tick, until they all have.
        if (at_cursor() >= count) {
            sequence.set_phase(40);
            return;
        }
        if (std::uint32_t(sequence.get(field::tick_count)) & flash_drift_stagger) return;
        sequence.set_half(cursor, std::int16_t(at_cursor() + 1));
        TargetFlashNotify(memory_, services_).spawn(target(at_cursor() - 1));
        return;
    case 40:
        if (at_cursor() != count) return;
        if (sequence.half(pending) != 0) return;
        if (std::int32_t(std::uint16_t(sequence.half(drifts_done))) != count) return;
        if (!target_slots_clear(at)) return;
        sequence.set_phase(50);
        return;
    case 50:
        if (!flash(at, flash_out)) return;
        required(services_.finalize_object, at, "object release service is not attached")(at);
        return;
    default:
        return;
    }
    sequence.set_phase(sequence.phase() + 10);
}

void TargetFlashNotify::spawn(Address at) {
    const EffectObject from(memory_, at);
    EffectObject child(memory_, required(services_.spawn_object, at,
                                         "effect object spawn service is not attached")(child_callback));
    child.set(field::sprite_id, from.get(field::sprite_id));
    child.set(field::x, from.get(field::x));
    child.set(field::y, from.get(field::y));
    child.set(field::z, from.get(field::z));
    required(services_.run_effect_script, at, "effect script service is not attached")(
        child.address(), child_script);
    child.set(field::linked_object, std::int32_t(at));
    required(services_.play_cue, at, "effect cue service is not attached")(0xf7);
}

void TargetFlashNotify::tick(Address at) {
    const EffectObject effect(memory_, at);
    if (effect.phase() != 0) return;
    const auto linked = Address(effect.get(field::linked_object));
    const auto& notify = required(services_.notify_controller, at,
                                  "effect controller service is not attached");
    // Two ticks in, the target is ready for its hit; when the script ends, the
    // flash is done.
    if (effect.get(field::tick_count) == 2) notify(linked, phase::notify_ring_done);
    if (effect.holds(flag::effect_script_running)) return;
    notify(linked, phase::notify_target_hit);
    required(services_.finalize_object, at, "object release service is not attached")(at);
}

}
