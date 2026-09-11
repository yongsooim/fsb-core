#include "fsb_core/visual_effects/directional_particles.hpp"
#include "fsb_core/visual_effects/effect_objects.hpp"

namespace fsb::core::visual_effects {
namespace {
template <typename Service>
const Service& required(const Service& service, Address at, const char* what) {
    if (!service) throw Fault(at, what);
    return service;
}
}

void DirectionalHitParticles::tick(Address at) {
    EffectObject child(memory_, at);
    if (child.phase() != 0) return;
    required(services_.apply_homing_motion_clamp, at, "homing motion service is not attached")(at);
    // 462de2 clears these once the particle has reached its anchor.
    if (child.get(field::motion_flags) & travel_in_progress_bits) return;
    required(services_.notify_controller, at, "effect controller service is not attached")(
        at, phase::notify_target_hit);
    required(services_.finalize_object, at, "object release service is not attached")(at);
}

std::int32_t DirectionalHitParticles::facing_of(Address origin) const {
    return std::int32_t(memory_.read(origin + field::facing));
}

Address DirectionalHitParticles::begin(Address callback, Address target, Address origin,
                                       std::int32_t lift, bool lift_on_ground_plane,
                                       std::int32_t drift_along_x, std::int32_t drift_along_y) {
    const EffectObject from(memory_, origin), to(memory_, target);
    EffectObject child(memory_, required(services_.spawn_object, origin,
                                         "effect object spawn service is not attached")(callback));
    const auto ground_lift = lift_on_ground_plane ? lift : 0;
    const auto elevation_lift = lift_on_ground_plane ? 0 : lift;

    child.set(field::sprite_id, from.get(field::sprite_id));
    child.set(field::x, from.get(field::x));
    child.set(field::y, signed32(std::uint32_t(from.get(field::y)) + std::uint32_t(ground_lift)));
    child.set(field::z, signed32(std::uint32_t(from.get(field::z)) + std::uint32_t(elevation_lift)));
    child.set(field::motion_flags, travelling_motion_flags);
    child.set(field::motion_anchor_x, to.get(field::x));
    child.set(field::motion_anchor_y, signed32(std::uint32_t(to.get(field::y)) + std::uint32_t(ground_lift)));
    child.set(field::motion_anchor_z, signed32(std::uint32_t(to.get(field::z)) + std::uint32_t(elevation_lift)));

    // The origin's facing picks which axis the particle drifts along and which
    // way. Any other facing leaves both drift fields as the spawn left them.
    switch (facing_of(origin)) {
    case 0: child.set(field::step_y, -drift_along_y); break;
    case 1: child.set(field::step_y, drift_along_y); break;
    case 2: child.set(field::step_x, -drift_along_x); break;
    case 3: child.set(field::step_x, drift_along_x); break;
    default: break;
    }
    return child.address();
}

Address DirectionalHitParticles::with_script_table(Address at, std::int32_t facing, Address table) {
    EffectObject child(memory_, at);
    child.set(field::facing, facing);
    required(services_.run_effect_script, at, "effect script service is not attached")(
        at, memory_.read(table + 4 * std::uint32_t(facing)));
    return at;
}

Address DirectionalHitParticles::with_script(Address at, std::int32_t facing, Address script) {
    EffectObject child(memory_, at);
    child.set(field::facing, facing);
    required(services_.run_effect_script, at, "effect script service is not attached")(at, script);
    return at;
}

Address DirectionalHitParticles::with_pose(Address at, std::int32_t facing, std::int32_t selector,
                                           std::int32_t frame) {
    EffectObject child(memory_, at);
    child.raise(flag::visible);
    child.set(actor_offset::sprite_selector, selector);
    child.set(actor_offset::sprite_frame, frame);
    child.set(field::facing, facing);
    return at;
}

Address DirectionalHitParticles::spawn_hit(Address target, Address origin) {
    const auto child = begin(child_callback, target, origin, 0x200000, false, 0x100000, 0xc0000);
    return with_script_table(child, facing_of(origin), 0x5debc0);
}

Address DirectionalHitParticles::spawn_double_hit(Address target, Address origin) {
    const auto child = begin(child_callback, target, origin, 0x200000, false, 0x100000, 0xc0000);
    return with_script_table(child, facing_of(origin), 0x5dff30);
}

Address DirectionalHitParticles::spawn_high_hit(Address target, Address origin) {
    const auto child = begin(child_callback, target, origin, 0x200000, false, 0x200000, 0x180000);
    return with_script_table(child, facing_of(origin), 0x5e6e80);
}

Address DirectionalHitParticles::spawn_flat_cue(Address target, Address origin) {
    // The only helper that lifts the ground plane rather than the elevation.
    const auto child = begin(child_callback, target, origin, 0x10000, true, 0x80000, 0x60000);
    with_script_table(child, facing_of(origin), 0x5e70b0);
    required(services_.play_cue, origin, "effect cue service is not attached")(0xe7);
    return child;
}

Address DirectionalHitParticles::spawn_slow_arc_hit(Address target, Address origin) {
    const auto child = begin(child_callback, target, origin, 0x300000, false, 0xc0000, 0x90000);
    with_script(child, facing_of(origin), 0x5d5a78);
    required(services_.play_cue, origin, "effect cue service is not attached")(0xe3);
    return child;
}

Address DirectionalHitParticles::spawn_fast_arc_trigger_hit(Address target, Address origin) {
    const auto facing = facing_of(origin);
    const auto child = begin(child_callback, target, origin, 0x300000, false, 0x200000, 0x180000);
    return with_pose(child, facing, 0x142, facing);
}

Address DirectionalHitParticles::spawn_deferred_trigger(Address target, Address origin) {
    const auto child = begin(child_callback, target, origin, 0x200000, false, 0xc0000, 0x90000);
    // The one pose tail that starts on frame zero instead of the facing.
    return with_pose(child, facing_of(origin), 0x149, 0);
}

Address DirectionalHitParticles::spawn_facing_deferred_trigger(Address target, Address origin) {
    const auto facing = facing_of(origin);
    const auto child = begin(child_callback, target, origin, 0x200000, false, 0xc0000, 0x90000);
    return with_pose(child, facing, 0xee, facing);
}

Address DirectionalHitParticles::spawn_poison_liquid(Address target, Address origin) {
    const auto facing = facing_of(origin);
    const auto child = begin(child_callback, target, origin, 0x300000, false, 0x200000, 0x180000);
    required(services_.apply_oscillation, child, "object motion service is not attached")(child);
    // Selector and frame come from an eight-byte record per facing.
    const auto record = 0x5ec3e8 + 8 * std::uint32_t(facing);
    with_pose(child, facing, std::int32_t(memory_.read(record)), std::int32_t(memory_.read(record + 4)));
    required(services_.play_cue, origin, "effect cue service is not attached")(0xa9);
    return child;
}

Address DirectionalHitParticles::spawn_directional_expand(Address target, Address origin) {
    const auto facing = facing_of(origin);
    const auto child = begin(expand_callback, target, origin, 0x300000, false, 0x200000, 0x180000);
    // Selector and frame come from an eight-byte record per facing.
    const auto record = 0x5d3468 + 8 * std::uint32_t(facing);
    return with_pose(child, facing, std::int32_t(memory_.read(record)),
                     std::int32_t(memory_.read(record + 4)));
}

}
