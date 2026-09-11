#include "fsb_core/visual_effects/effect_objects.hpp"
#include "fsb_core/effect_script.hpp"
#include <algorithm>

namespace fsb::core::visual_effects {
namespace {
// The ring works in a 16.16 angle that wraps at one turn.
constexpr std::int32_t turn = 0x10000;
constexpr std::int32_t quarter_turn = 0x4000;
constexpr std::int32_t random_angle_buckets = 0x400;
constexpr std::int32_t rise_step = 0x10000;      // Applied to elevation each rise frame.
constexpr std::int32_t rise_limit = 0x600001;
constexpr std::int32_t angle_step_gain = 0x20;
constexpr std::int32_t angle_step_ceiling = 0x1000;
constexpr std::int32_t initial_angle_step = 0x400;
constexpr std::int32_t initial_radius = 0x20;
constexpr std::int32_t initial_radius_counter = 0x18;
constexpr std::int32_t finish_radius_gain = 10;
constexpr std::int32_t finish_radius_limit = 600;
constexpr std::int32_t ring_motion_flags = 0x32;

constexpr unsigned cue_ring_launch = 0xca;
constexpr unsigned cue_first_damage = 0xcb;
constexpr std::int32_t flash_in = 1, flash_out = 0, flash_frames = 30;

template <typename Service>
const Service& required(const Service& service, Address at, const char* what) {
    if (!service) throw Fault(at, what);
    return service;
}
}

std::int32_t RingEffect::spawn(Address owner) {
    const auto& random = required(services_.random, owner, "effect random service is not attached");
    const auto& create = required(services_.spawn_object, owner, "effect object spawn service is not attached");
    const auto& oscillate = required(services_.apply_oscillation, owner, "object motion service is not attached");
    const auto& run_script = required(services_.run_effect_script, owner, "effect script service is not attached");

    const EffectObject parent(memory_, owner);
    // One random start angle for the ring; the children divide the turn evenly.
    std::int32_t angle = (random() % random_angle_buckets * turn) / random_angle_buckets;
    std::int32_t carried = 0;
    Address last = 0;
    for (unsigned remaining = 4; remaining; --remaining) {
        EffectObject child(memory_, create(child_callback));
        child.set(field::z, 0);
        child.set(field::sprite_id, parent.get(field::sprite_id));
        child.set(field::motion_flags, ring_motion_flags);
        child.set(field::motion_anchor_x, parent.get(field::x));
        child.set(field::motion_anchor_z, 0);
        child.set(field::motion_anchor_y, parent.get(field::y));
        child.set(ring_field::motion_radius, initial_radius);
        child.set(ring_field::radius_counter, initial_radius_counter);
        child.set(ring_field::motion_angle, angle);
        child.set(ring_field::angle_accumulator, angle);
        child.set(ring_field::angle_step, initial_angle_step);
        oscillate(child.address());
        run_script(child.address(), child_script);
        carried = angle + quarter_turn;
        angle = carried % turn;
        last = child.address();
    }
    // Only the final child reports for the whole ring.
    EffectObject(memory_, last).raise(flag::notify_owner_on_finish);
    EffectObject(memory_, last).set(field::notify_target, std::int32_t(owner));
    return carried / turn;
}

void RingEffect::tick(Address at) {
    EffectObject child(memory_, at);
    const auto phase_now = child.phase();
    if (phase_now == phase::ring_inactive) return;

    const auto& oscillate = required(services_.apply_oscillation, at, "object motion service is not attached");
    // The accumulator carries the angle; the motion field is this frame's value.
    const auto advance_angle = [&](std::int32_t step) {
        auto angle = signed32(std::uint32_t(child.get(ring_field::angle_accumulator)) + std::uint32_t(step));
        child.set(ring_field::angle_accumulator, angle);
        if (angle > turn - 1) {
            angle -= turn;
            child.set(ring_field::angle_accumulator, angle);
        }
        child.set(ring_field::motion_angle, angle);
    };
    const auto report = [&](std::int32_t code) {
        if (!child.holds(flag::notify_owner_on_finish)) return;
        required(services_.notify_controller, at, "effect controller service is not attached")(
            Address(child.get(field::notify_target)), code);
    };

    switch (phase_now) {
    case phase::ring_rise: {
        child.add(field::z, rise_step);
        const auto step = std::min(signed32(std::uint32_t(child.get(ring_field::angle_step)) + std::uint32_t(angle_step_gain)), angle_step_ceiling);
        advance_angle(step);
        child.set(ring_field::angle_step, step);
        oscillate(at);
        if (!(child.byte(ring_field::suppress_trail) & 1))
            required(services_.spawn_swirl_particle, at, "swirl particle service is not attached")(at);
        if (child.get(field::z) < rise_limit) return;
        break;
    }
    case phase::ring_spin: {
        advance_angle(child.get(ring_field::angle_step));
        const auto radius = child.get(ring_field::radius_counter);
        child.set(ring_field::motion_radius, radius);
        child.set(ring_field::radius_counter, signed32(std::uint32_t(radius) - 1u));
        oscillate(at);
        if (child.get(ring_field::motion_radius) > 0) return;
        report(phase::notify_target_hit);
        child.set(ring_field::radius_step, 0);
        break;
    }
    case phase::ring_finish:
        child.add(ring_field::radius_step, finish_radius_gain);
        child.add(ring_field::radius_counter, child.get(ring_field::radius_step));
        child.set(ring_field::motion_radius, child.get(ring_field::radius_counter));
        oscillate(at);
        required(services_.spawn_swirl_particle, at, "swirl particle service is not attached")(at);
        if (child.get(ring_field::motion_radius) < finish_radius_limit) return;
        report(phase::notify_ring_done);
        required(services_.finalize_object, at, "object release service is not attached")(at);
        return;
    default:
        return;
    }
    child.add(field::phase, phase::ring_step);
}

std::int32_t SweepController::target_index(Address object) const {
    const auto count = target_count();
    std::int32_t index = 0;
    while (index < count && target(index) != object) ++index;
    return index;
}

void SweepController::launch_rings(Address controller) {
    EffectObject sweep(memory_, controller);
    const auto& run_script = required(services_.run_effect_script, controller,
                                      "effect script service is not attached");
    const auto count = target_count();
    for (std::int32_t index = 0; index < count; ++index) {
        EffectObject object(memory_, target(index));
        if (object.holds(sweep_flag::target_script_ready))
            run_script(object.address(),
                       memory_.read(battle::target_hit_scripts + 4 * memory_.read(object.address() + sweep_field::script_index)));
        object.clear(sweep_flag::target_visual_busy);
        RingEffect(memory_, services_).spawn(object.address());
        sweep.set_half(sweep_field::ring_children_pending,
                       std::int16_t(sweep.half(sweep_field::ring_children_pending) + 1));
    }
}

bool SweepController::targets_drained(Address controller) const {
    const EffectObject sweep(memory_, controller);
    if (sweep.half(sweep_field::ring_children_pending) != 0) return false;
    if (sweep.half(sweep_field::numbers_pending) != 0) return false;
    // One 16-bit pending flag per target, held on the controller itself.
    const auto count = target_count();
    std::int32_t index = 0;
    while (index < count && sweep.half(sweep_field::target_pending_flags + 2 * std::uint32_t(index)) == 0) ++index;
    return index == count;
}

void SweepController::run(Address controller, Address actor_scripts) {
    EffectObject sweep(memory_, controller);
    const auto state = sweep.phase();

    if (state < phase::notify_target_hit + 1) {
        if (state == phase::notify_target_hit) {
            const auto index = target_index(Address(sweep.get(field::owner_object)));
            if (index == 0)
                required(services_.play_cue, controller, "effect cue service is not attached")(cue_first_damage);
            // The original does not guard a target that is not in the array;
            // the index then addresses the slot past the last one.
            EffectObject hit(memory_, target(index));
            hit.raise(sweep_flag::target_visual_busy);
            required(services_.spawn_floating_number_glyphset, controller,
                     "floating number service is not attached")(
                hit.address(),
                std::int32_t(memory_.read(battle::result_amounts + battle::result_record_bytes * std::uint32_t(index))));
            sweep.set_half(sweep_field::numbers_pending, std::int16_t(sweep.half(sweep_field::numbers_pending) + 1));
        } else if (state == phase::notify_ring_done) {
            sweep.set_half(sweep_field::ring_children_pending,
                           std::int16_t(sweep.half(sweep_field::ring_children_pending) - 1));
        }
        return;
    }
    if (state == phase::notify_actor_done) {
        launch_rings(controller);
        required(services_.play_cue, controller, "effect cue service is not attached")(cue_ring_launch);
        sweep.set_phase(phase::sweep_wait_drain);
        return;
    }
    if (state == phase::notify_numbers_done) {
        sweep.set_half(sweep_field::numbers_pending, std::int16_t(sweep.half(sweep_field::numbers_pending) - 1));
        return;
    }

    const auto flash = [&](std::int32_t mode) {
        return required(services_.screen_flash_transition, controller,
                        "screen flash service is not attached")(mode, flash_frames) != 0;
    };
    switch (state) {
    case phase::sweep_flash_in:
        if (!flash(flash_in)) return;
        break;
    case phase::sweep_actor_script: {
        EffectObject actor(memory_, memory_.read(battle::active_actor));
        actor.clear(sweep_flag::target_visual_busy);
        required(services_.run_effect_script, controller, "effect script service is not attached")(
            actor.address(), memory_.read(actor_scripts + 4 * memory_.read(actor.address() + sweep_field::script_index)));
        break;
    }
    case phase::sweep_wait_drain:
        if (!targets_drained(controller)) return;
        sweep.set_phase(phase::sweep_flash_out);
        return;
    case phase::sweep_flash_out:
        if (!flash(flash_out)) return;
        required(services_.finalize_object, controller, "object release service is not attached")(controller);
        return;
    default:
        return;
    }
    sweep.add(field::phase, phase::ring_step);
}

void DriftEffect::spawn(Address source) {
    const auto& create = required(services_.spawn_object, source, "effect object spawn service is not attached");
    const auto& oscillate = required(services_.apply_oscillation, source, "object motion service is not attached");
    const auto& run_script = required(services_.run_effect_script, source, "effect script service is not attached");
    const EffectObject from(memory_, source);
    // One random start angle; the four children divide the turn evenly. Unlike
    // the ring, this angle is never wrapped back under one turn.
    std::int32_t angle = (required(services_.random, source, "effect random service is not attached")()
                          % 0x100 * turn) / 0x400;
    Address last = 0;
    for (unsigned remaining = 4; remaining; --remaining) {
        EffectObject child(memory_, create(child_callback));
        child.set(field::sprite_id, from.get(field::sprite_id));
        child.set(field::motion_flags, 0x32);
        child.set(field::motion_anchor_x, from.get(field::x));
        child.set(ring_field::motion_radius, 0);
        child.set(ring_field::radius_counter, 0);
        child.set(field::motion_anchor_y, from.get(field::y));
        child.set(ring_field::motion_angle, angle);
        child.set(ring_field::angle_accumulator, angle);
        oscillate(child.address());
        run_script(child.address(), child_script);
        angle += quarter_turn;
        last = child.address();
    }
    EffectObject(memory_, last).raise(flag::notify_owner_on_finish);
}

void DebrisEffect::spawn(Address source) {
    const auto& create = required(services_.spawn_object, source, "effect object spawn service is not attached");
    const auto& random = required(services_.random, source, "effect random service is not attached");
    const EffectObject from(memory_, source);
    // A spread in whole q16 units: the draw is taken modulo `range`, scaled to
    // q16, then divided down to the span the field wants.
    const auto spread = [&](std::int32_t range, std::int32_t divisor) {
        return ((random() % range) << 16) / divisor;
    };
    for (unsigned remaining = 4; remaining; --remaining) {
        EffectObject piece(memory_, create(child_callback));
        piece.set(field::sprite_id, from.get(field::sprite_id));
        piece.set(field::x, from.get(field::x));
        piece.set(field::y, from.get(field::y));
        piece.set(field::z, from.get(field::z));
        piece.set(field::motion_flags, 0x71);
        piece.set(field::motion_anchor_x, from.get(field::x));
        piece.set(field::motion_anchor_y, from.get(field::y));
        piece.set(field::motion_anchor_z, from.get(field::z));
        piece.set(field::step_x, spread(0xbb8, 0x1f4) - 0x30000);
        piece.set(field::step_y, spread(0x7d0, 0x1f4) - 0x20000);
        piece.raise(flag::visible);
        piece.set(actor_offset::sprite_selector, 0x12c);
        piece.set(field::step_z, spread(0xbb8, 0x3e8));
        piece.raise(flag::visible);
        piece.set(actor_offset::sprite_frame, random() % 0xc + 0xc);
    }
}

void RisingEffect::spawn(Address source) {
    const auto& create = required(services_.spawn_object, source, "effect object spawn service is not attached");
    EffectObject child(memory_, create(child_callback));
    const EffectObject actor(memory_, memory_.read(battle::active_actor)), from(memory_, source);
    child.set(field::sprite_id, actor.get(field::sprite_id));
    child.set(field::x, from.get(field::x));
    child.set(field::z, 0);
    child.set(field::y, signed32(std::uint32_t(from.get(field::y)) + std::uint32_t(0x10000)));
    required(services_.play_cue, source, "effect cue service is not attached")(0x124);
}

void AnimatedParticle::tick(Address at) {
    EffectObject particle(memory_, at);
    if (particle.phase() != 0) return;
    // The animation stage follows how far the particle has risen.
    auto stage = particle.get(field::z) / turn / 0x10;
    if (stage > 7) stage = 7;
    const auto script = memory_.read(stage_scripts + 4 * std::uint32_t(stage));
    // Restart only when the stage has actually moved on.
    if (script != std::uint32_t(particle.get(effect_script::script)))
        required(services_.run_effect_script, at, "effect script service is not attached")(at, script);
    required(services_.apply_oscillation, at, "object motion service is not attached")(at);
    if (particle.get(field::z) / turn > 0x80)
        required(services_.finalize_object, at, "object release service is not attached")(at);
}

void TrailSprite::spawn(Address source) {
    const EffectObject from(memory_, source);
    EffectObject sprite(memory_, required(services_.spawn_object, source,
                                          "effect object spawn service is not attached")(child_callback));
    sprite.set(field::sprite_id, from.get(field::sprite_id));
    sprite.set(field::x, from.get(field::x));
    sprite.set(field::y, from.get(field::y));
    sprite.set(field::z, from.get(field::z));
    required(services_.run_effect_script, source, "effect script service is not attached")(
        sprite.address(), child_script);
}

}
