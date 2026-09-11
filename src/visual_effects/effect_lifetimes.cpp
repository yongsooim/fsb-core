#include "fsb_core/visual_effects/effect_lifetimes.hpp"
#include "fsb_core/visual_effects/effect_objects.hpp"

namespace fsb::core::visual_effects {
namespace {
template <typename Service>
const Service& required(const Service& service, Address at, const char* what) {
    if (!service) throw Fault(at, what);
    return service;
}
// Bits 45d208 and 462de2 clear once the object has arrived.
constexpr std::int32_t travel_bits = 0x30, flight_bits = 0x70;
constexpr unsigned cue_screen_shake = 0xe4;
constexpr unsigned ticks_before_release = 0x14;
}

bool FinishingEffects::idle(Address at) const {
    const EffectObject effect(memory_, at);
    return effect.phase() == 0 && !effect.holds(flag::effect_script_running);
}

void FinishingEffects::release(Address at) {
    required(services_.finalize_object, at, "object release service is not attached")(at);
}

void FinishingEffects::release_after_twenty_ticks(Address at) {
    const EffectObject effect(memory_, at);
    if (effect.phase() != 0) return;
    // The tick counter is compared unsigned, as the original does.
    if (std::uint32_t(effect.get(field::tick_count)) < ticks_before_release) return;
    release(at);
}

void FinishingEffects::release_when_script_ends(Address at) {
    if (!idle(at)) return;
    release(at);
}

void FinishingEffects::travel_then_release(Address at) {
    const EffectObject effect(memory_, at);
    if (effect.phase() != 0) return;
    required(services_.apply_homing_motion_clamp, at, "homing motion service is not attached")(at);
    if (effect.get(field::motion_flags) & flight_bits) return;
    release(at);
}

void FinishingEffects::drift_until_script_ends(Address at) {
    const EffectObject effect(memory_, at);
    if (effect.phase() != 0) return;
    required(services_.apply_oscillation, at, "object motion service is not attached")(at);
    if (effect.holds(flag::effect_script_running)) return;
    release(at);
}

void FinishingEffects::report_hit_when_script_ends(Address at) {
    if (!idle(at)) return;
    required(services_.notify_controller, at, "effect controller service is not attached")(
        at, phase::notify_target_hit);
    release(at);
}

void FinishingEffects::report_actor_done_when_script_ends(Address at) {
    if (!idle(at)) return;
    required(services_.notify_controller, at, "effect controller service is not attached")(
        at, phase::notify_actor_done);
    release(at);
}

void FinishingEffects::report_owner_hit_when_script_ends(Address at) {
    if (!idle(at)) return;
    const EffectObject effect(memory_, at);
    required(services_.notify_controller, at, "effect controller service is not attached")(
        Address(effect.get(field::linked_object)), phase::notify_target_hit);
    release(at);
}

void FinishingEffects::travel_then_report_done(Address at) {
    const EffectObject effect(memory_, at);
    if (effect.phase() != 0) return;
    required(services_.apply_homing_motion_clamp, at, "homing motion service is not attached")(at);
    if (effect.get(field::motion_flags) & travel_bits) return;
    required(services_.notify_controller, at, "effect controller service is not attached")(
        at, phase::notify_ring_done);
    release(at);
}

void FinishingEffects::report_ready_when_script_ends(Address at) {
    // The original tests for phase -1 before testing for zero; both leave.
    if (!idle(at)) return;
    required(services_.notify_controller, at, "effect controller service is not attached")(
        at, phase::notify_ready);
    release(at);
}

void FinishingEffects::scatter_drift_when_script_ends(Address at) {
    if (!idle(at)) return;
    const EffectObject effect(memory_, at);
    DriftEffect(memory_, services_).spawn(Address(effect.get(field::linked_object)));
    release(at);
}

void FinishingEffects::drop_then_scatter_debris(Address at) {
    EffectObject effect(memory_, at);
    if (effect.phase() != 0) return;
    required(services_.apply_oscillation, at, "object motion service is not attached")(at);
    if (effect.get(field::z) > 0) return;
    effect.set(field::z, 0);
    DebrisEffect(memory_, services_).spawn(at);
    release(at);
}

void FinishingEffects::fall_then_raise_successor(Address at) {
    EffectObject effect(memory_, at);
    if (effect.phase() != 0) return;
    required(services_.apply_oscillation, at, "object motion service is not attached")(at);
    if (effect.get(field::z) > 0) return;
    // The successor's source is read before the landing is written.
    const auto source = Address(effect.get(field::linked_object));
    effect.set(field::z, 0);
    RisingEffect(memory_, services_).spawn(source);
    release(at);
}

void FinishingEffects::stop_screen_shake() {
    const auto shake = memory_.read(active_screen_shake);
    if (!shake) return;
    required(services_.stop_cue, active_screen_shake, "effect cue service is not attached")(cue_screen_shake);
    // The original leaves the global pointing at the released object.
    release(memory_.read(active_screen_shake));
}

bool FinishingEffects::arrived(Address at) {
    const EffectObject effect(memory_, at);
    if (effect.phase() != 0) return false;
    required(services_.apply_homing_motion_clamp, at, "homing motion service is not attached")(at);
    return (effect.get(field::motion_flags) & travel_bits) == 0;
}

void FinishingEffects::report_and_release(Address at, Address reported, bool also_flight) {
    const auto& notify = required(services_.notify_controller, at,
                                  "effect controller service is not attached");
    notify(reported, phase::notify_target_hit);
    if (also_flight) notify(reported, phase::notify_ring_done);
    release(at);
}

void FinishingEffects::play(Address at, unsigned cue) {
    required(services_.play_cue, at, "effect cue service is not attached")(cue);
}

void FinishingEffects::directional_expand_impact(Address at) {
    if (!arrived(at)) return;
    play(at, 0xd7);
    report_and_release(at, at, false);
}

void FinishingEffects::projectile_impact(Address at) {
    if (!arrived(at)) return;
    play(at, 0xbe);
    report_and_release(at, at, false);
}

void FinishingEffects::particle_impact(Address at) {
    if (!arrived(at)) return;
    play(at, 0xc0);
    report_and_release(at, at, false);
}

void FinishingEffects::dual_notify_impact(Address at) {
    if (!arrived(at)) return;
    play(at, 0xd7);
    report_and_release(at, at, true);
}

void FinishingEffects::homing_impact(Address at) {
    if (!arrived(at)) return;
    report_and_release(at, at, true);
}

void FinishingEffects::homing_impact_with_cue(Address at) {
    if (!arrived(at)) return;
    play(at, 0xb0);
    report_and_release(at, at, true);
}

void FinishingEffects::homing_impact_on_first_target(Address at) {
    if (!arrived(at)) return;
    report_and_release(at, memory_.read(battle::target_objects), true);
}

void FinishingEffects::homing_impact_on_link(Address at) {
    if (!arrived(at)) return;
    const EffectObject effect(memory_, at);
    report_and_release(at, Address(effect.get(field::linked_object)), true);
}

void FinishingEffects::homing_impact_on_link_with_cue(Address at) {
    if (!arrived(at)) return;
    play(at, 0xc0);
    const EffectObject effect(memory_, at);
    report_and_release(at, Address(effect.get(field::linked_object)), true);
}

void FinishingEffects::homing_impact_after_delay(Address at) {
    EffectObject effect(memory_, at);
    // Waits sixty ticks where it is, then starts travelling.
    if (effect.phase() == 0) {
        if (effect.get(field::tick_count) == 0x3c) effect.set_phase(10);
        return;
    }
    if (effect.phase() != 10) return;
    required(services_.apply_homing_motion_clamp, at, "homing motion service is not attached")(at);
    if (effect.get(field::motion_flags) & travel_bits) return;
    report_and_release(at, Address(effect.get(field::linked_object)), true);
}

void FinishingEffects::directional_trail_impact(Address at) {
    const EffectObject effect(memory_, at);
    if (effect.phase() != 0) return;
    required(services_.apply_homing_motion_clamp, at, "homing motion service is not attached")(at);
    // The trail is laid every frame, arrived or not.
    TrailSprite(memory_, services_).spawn(at);
    if (effect.get(field::motion_flags) & travel_bits) return;
    required(services_.stop_cue, at, "effect cue service is not attached")(0xe5);
    play(at, 0xbe);
    report_and_release(at, at, false);
}

void FinishingEffects::arc_projectile_impact(Address at) {
    EffectObject effect(memory_, at);
    if (effect.phase() != 0) return;
    // Two motion helpers in turn, each with the flags it reads.
    effect.set(field::motion_flags, 0x41);
    required(services_.apply_oscillation, at, "object motion service is not attached")(at);
    effect.set(field::motion_flags, 0x31);
    required(services_.apply_homing_motion_clamp, at, "homing motion service is not attached")(at);
    if (effect.get(field::motion_flags) & travel_bits) return;
    report_and_release(at, Address(effect.get(field::linked_object)), true);
}

void FinishingEffects::rising_sprite_notify(Address at) {
    const EffectObject effect(memory_, at);
    if (effect.phase() != 0) return;
    required(services_.apply_oscillation, at, "object motion service is not attached")(at);
    if (effect.get(field::z) <= 0x1f40000) return;
    // Only the object asked to report does; the rest just leave.
    if (effect.holds(flag::notify_owner_on_finish))
        required(services_.notify_controller, at, "effect controller service is not attached")(
            Address(effect.get(field::linked_object)), phase::notify_ready);
    release(at);
}

}
