#include "fsb_core/visual_effects/projectiles.hpp"
#include "fsb_core/visual_effects/effect_objects.hpp"

namespace fsb::core::visual_effects {
namespace {
// The launch scripts, one per dominant axis.
constexpr Address script_along_x = 0x5d3f40, script_along_y = 0x5d3fa0;
constexpr unsigned cue_launch = 0xcf;
// The anchor sits at a fixed height; only the launch elevation varies.
constexpr std::int32_t anchor_elevation = 0x300000;
constexpr std::int32_t speed_along_x = 0x200000, speed_along_y = 0x180000;

template <typename Service>
const Service& required(const Service& service, Address at, const char* what) {
    if (!service) throw Fault(at, what);
    return service;
}
}

void Projectiles::tick(Address at) {
    EffectObject projectile(memory_, at);
    const auto reported = Address(projectile.get(field::linked_object));
    const auto phase_now = projectile.phase();
    if (phase_now == 0) {
        // Hold on the launch pose until the effect script has finished.
        if (!projectile.holds(flag::effect_script_running)) projectile.set_phase(10);
        return;
    }
    if (phase_now != 10) return;
    required(services_.apply_homing_motion_clamp, at, "homing motion service is not attached")(at);
    if (projectile.get(field::motion_flags) & projectile_in_flight_bits) return;
    const auto& notify = required(services_.notify_controller, at,
                                  "effect controller service is not attached");
    notify(reported, phase::notify_ring_done);
    if (projectile.holds(flag::notify_owner_on_finish)) notify(reported, phase::notify_target_hit);
    required(services_.finalize_object, at, "object release service is not attached")(at);
}

std::int32_t Projectiles::jitter(Address origin, std::int32_t spread, std::int32_t bias) {
    const auto draw = required(services_.random, origin, "effect random service is not attached")();
    return signed32(std::uint32_t(draw % spread - bias) << 16);
}

Address Projectiles::begin(Address target, Address origin, std::int32_t offset_x,
                           std::int32_t offset_y, std::int32_t lift) {
    const EffectObject from(memory_, origin), to(memory_, target);
    EffectObject projectile(memory_, required(services_.spawn_object, origin,
                                              "effect object spawn service is not attached")(child_callback));
    projectile.set(field::sprite_id, from.get(field::sprite_id));
    projectile.set(field::x, signed32(std::uint32_t(from.get(field::x)) + std::uint32_t(offset_x)));
    projectile.set(field::y, signed32(std::uint32_t(from.get(field::y)) + std::uint32_t(offset_y)));
    projectile.set(field::motion_flags, projectile_motion_flags);
    projectile.set(field::z, signed32(std::uint32_t(from.get(field::z)) + std::uint32_t(lift)));
    projectile.set(field::motion_anchor_x, to.get(field::x));
    projectile.set(field::motion_anchor_y, to.get(field::y));
    projectile.set(field::motion_anchor_z, anchor_elevation);
    return projectile.address();
}

void Projectiles::aim(Address at, bool along_x, std::int32_t speed, bool clamp_frames) {
    EffectObject projectile(memory_, at);
    const auto lead = along_x ? field::step_x : field::step_y;
    const auto lead_gap = along_x ? signed32(std::uint32_t(projectile.get(field::motion_anchor_x)) - std::uint32_t(projectile.get(field::x)))
                                  : signed32(std::uint32_t(projectile.get(field::motion_anchor_y)) - std::uint32_t(projectile.get(field::y)));
    projectile.set(lead, speed);
    auto frames = lead_gap / speed;
    if (clamp_frames && frames < 1) frames = 1;
    projectile.set(along_x ? field::step_y : field::step_x,
                   (along_x ? signed32(std::uint32_t(projectile.get(field::motion_anchor_y)) - std::uint32_t(projectile.get(field::y)))
                            : signed32(std::uint32_t(projectile.get(field::motion_anchor_x)) - std::uint32_t(projectile.get(field::x)))) / frames);
    projectile.set(field::step_z, signed32(std::uint32_t(anchor_elevation) - std::uint32_t(projectile.get(field::z))) / frames);
}

void Projectiles::finish(Address at, Address target, bool aimed, bool along_x) {
    EffectObject projectile(memory_, at);
    // A facing outside zero to three leaves the flight unaimed and unscripted.
    if (aimed)
        required(services_.run_effect_script, at, "effect script service is not attached")(
            at, along_x ? script_along_x : script_along_y);
    projectile.set(field::linked_object, std::int32_t(target));
    required(services_.play_cue, at, "effect cue service is not attached")(cue_launch);
}

Address Projectiles::launch(Address target, Address origin) {
    // Both draws happen before anything else reads the stream.
    const auto offset_x = jitter(origin, 16, 8);
    const auto offset_y = jitter(origin, 8, 2);
    const auto at = begin(target, origin, offset_x, offset_y, 0x500000);
    const auto facing = std::int32_t(memory_.read(origin + field::facing));
    const bool along_x = facing == 2 || facing == 3;
    if (facing >= 0 && facing <= 3)
        aim(at, along_x, along_x ? (facing == 3 ? speed_along_x : -speed_along_x)
                                 : (facing == 1 ? speed_along_y : -speed_along_y), false);
    finish(at, target, facing >= 0 && facing <= 3, along_x);
    return at;
}

Address Projectiles::launch_arc(Address target, Address origin) {
    const auto at = begin(target, origin, 0, 0, 0x300000);
    EffectObject projectile(memory_, at);
    const auto facing = std::int32_t(memory_.read(origin + field::facing));
    // This launcher nudges the start away from the origin before aiming, and
    // its two ground-plane nudges are not mirror images of each other.
    switch (facing) {
    case 3:
        projectile.add(field::y, 0x10000);
        projectile.add(field::x, 0x140000);
        aim(at, true, speed_along_x, false);
        break;
    case 2:
        projectile.add(field::y, 0x10000);
        projectile.add(field::x, -0x140000);
        aim(at, true, -speed_along_x, false);
        break;
    case 1:
        projectile.add(field::y, 0xc0000);
        aim(at, false, speed_along_y, false);
        break;
    case 0:
        projectile.add(field::y, -0x90000);
        aim(at, false, -speed_along_y, false);
        break;
    default:
        break;
    }
    finish(at, target, facing >= 0 && facing <= 3, facing == 2 || facing == 3);
    return at;
}

Address Projectiles::launch_directional(Address target, Address origin) {
    const auto offset_x = jitter(origin, 8, 4);
    const auto offset_y = jitter(origin, 8, 2);
    const auto at = begin(target, origin, offset_x, offset_y, 0x300000);
    EffectObject projectile(memory_, at);
    const auto facing = std::int32_t(memory_.read(origin + field::facing));
    switch (facing) {
    case 3:
        projectile.add(field::x, 0x240000);
        aim(at, true, speed_along_x, true);
        break;
    case 2:
        projectile.add(field::x, -0x240000);
        aim(at, true, -speed_along_x, true);
        break;
    case 1:
        projectile.add(field::y, speed_along_y);
        aim(at, false, speed_along_y, true);
        break;
    case 0:
        projectile.add(field::y, -speed_along_y);
        aim(at, false, -speed_along_y, true);
        break;
    default:
        break;
    }
    finish(at, target, facing >= 0 && facing <= 3, facing == 2 || facing == 3);
    return at;
}

}
