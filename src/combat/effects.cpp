#include "fsb_core/combat/effects.hpp"
#include "fsb_core/combat/object_motion.hpp"
#include "fsb_core/symbols.hpp"
#include <algorithm>
#include <string>

namespace fsb::core::combat {
namespace {
using namespace effects;
} // namespace

bool Effects::script_still_running(Address object) const {
    return (memory_.read(object + actor_offset::flags) & script_running) != 0;
}
Address Effects::place(Address callback, std::int32_t x, std::int32_t y,
                       std::int32_t elevation, std::uint32_t layer) {
    const auto object = take_object ? take_object(callback) : 0;
    memory_.write(object + actor_offset::layer_q16, layer);
    memory_.write(object + actor_offset::world_x, std::uint32_t(x));
    memory_.write(object + actor_offset::world_y, std::uint32_t(y));
    memory_.write(object + actor_offset::elevation, std::uint32_t(elevation));
    return object;
}
void Effects::spawn_scripted(std::int32_t x, std::int32_t y, std::int32_t elevation,
                             std::uint32_t layer, Address script) {
    const auto object = place(0x464c05, x, y, elevation, layer);
    if (start_script) start_script(object, script);
}
void Effects::spawn_notifying(std::int32_t x, std::int32_t y, std::int32_t elevation,
                              std::uint32_t layer, Address script, std::int32_t state) {
    const auto object = place(0x464d2f, x, y, elevation, layer);
    memory_.write(object + controller_state, std::uint32_t(state));
    if (start_script) start_script(object, script);
}
void Effects::attach_to(Address tick, Address anchor_object) {
    const auto object = take_object ? take_object(tick) : 0;
    memory_.write(object + anchor, anchor_object);
}
void Effects::attach_to_drifting(Address tick, Address anchor_object, std::int32_t speed) {
    const auto object = take_object ? take_object(tick) : 0;
    memory_.write(object + object_motion::velocity_x, std::uint32_t(speed));
    memory_.write(object + anchor, anchor_object);
}
void Effects::spawn_hit_spark(Address anchor_object) {
    const auto object = take_object ? take_object(0x46521e) : 0;
    memory_.write(object + actor_offset::layer_q16, memory_.read(anchor_object + actor_offset::layer_q16));
    memory_.write(object + actor_offset::world_x, memory_.read(anchor_object + actor_offset::world_x) - 0x180000);
    // One to four tiles above the anchor, drawn at ground elevation.
    const auto lift = (std::uint32_t(std::int32_t(crt_rand(memory_)) % 4) + 1) << 16;
    memory_.write(object + actor_offset::world_y, memory_.read(anchor_object + actor_offset::world_y) + lift);
    memory_.write(object + actor_offset::elevation, 0);
    if (start_script) start_script(object, hit_spark_script);
}
void Effects::spawn_phase_finish(Address anchor_object) {
    const auto object = take_object ? take_object(0x4653a5) : 0;
    memory_.write(object + actor_offset::layer_q16, memory_.read(anchor_object + actor_offset::layer_q16));
    memory_.write(object + actor_offset::world_x, memory_.read(anchor_object + actor_offset::world_x));
    memory_.write(object + actor_offset::world_y, memory_.read(anchor_object + actor_offset::world_y) + 0x80000);
    memory_.write(object + actor_offset::elevation, memory_.read(anchor_object + actor_offset::elevation) + 0x340000);
    // The original starts the script before it records the anchor.
    if (start_script) start_script(object, phase_finish_script);
    memory_.write(object + anchor, anchor_object);
    if (play_cue) play_cue(phase_finish_cue);
}
void Effects::notify_anchor_and_retire(Address object) {
    const auto state = signed32(memory_.read(object + actor_offset::callback_state));
    if (state == -1 || state != 0) return;
    if (script_still_running(object)) return;
    // The report goes to the anchor, not to this object.
    if (notify_controller) notify_controller(memory_.read(object + anchor), phase_finished_state);
    if (release_object) release_object(object);
}
Address Effects::spawn_debris_particle(Address anchor_object) {
    const auto object = take_object ? take_object(0x46596b) : 0;
    memory_.write(object + actor_offset::layer_q16, memory_.read(anchor_object + actor_offset::layer_q16));
    memory_.write(object + actor_offset::world_x, memory_.read(anchor_object + actor_offset::world_x));
    memory_.write(object + actor_offset::world_y, memory_.read(anchor_object + actor_offset::world_y));
    memory_.write(object + actor_offset::elevation, memory_.read(anchor_object + actor_offset::elevation));
    memory_.write(object + anchor, anchor_object);
    const auto choice = std::uint32_t(std::int32_t(crt_rand(memory_)) % std::int32_t(debris_script_count));
    if (start_script) start_script(object, memory_.read(debris_scripts + choice * 4));
    if (play_cue) play_cue(debris_cue);
    return object;
}
void Effects::spawn_falling_strikes(Address anchor_object) {
    Address object = 0;
    const auto row = signed32(memory_.read(anchor_object + actor_offset::tile_y_q16)) / 0x10000;
    const auto layer = signed32(memory_.read(anchor_object + actor_offset::layer_q16)) / 0x10000;
    const auto first = signed32(memory_.read(strike_origin)) / 0x40;
    for (unsigned i = 0; i < strike_copies; ++i) {
        object = take_object ? take_object(0x46528d) : 0;
        memory_.write(object + actor_offset::flags, memory_.read(object + actor_offset::flags) | strike_flags);
        memory_.write(object + strike_target, memory_.read(anchor_object + actor_offset::world_x) + strike_lift);
        if (place_on_tile) place_on_tile(object, first + std::int32_t(i) + 6, row, layer);
        if (start_script) start_script(object, strike_fall_script);
    }
    // Only the last of the four reports the hit back to the anchor.
    memory_.write(object + 6, memory_.read(object + 6, 1) | final_particle_flag, 1);
    memory_.write(object + anchor, anchor_object);
    if (play_cue) play_cue(strike_cue);
}
void Effects::fall_and_strike(Address object) {
    const auto state = signed32(memory_.read(object + actor_offset::callback_state));
    if (state == -1) return;
    if (state == 10) {
        if (script_still_running(object)) return;
        // The impact is over; drop back to falling.
        if (start_script) start_script(object, strike_fall_script);
        memory_.write(object + actor_offset::callback_state, 0);
        return;
    }
    if (state != 0) return;
    memory_.write(object + actor_offset::world_x,
                  memory_.read(object + actor_offset::world_x) - strike_fall_step);
    spawn_hit_spark(object);
    if (signed32(memory_.read(object + strike_target)) / 0x10000
        == signed32(memory_.read(object + actor_offset::world_x)) / 0x10000) {
        if (start_script) start_script(object, strike_impact_script);
        memory_.write(object + actor_offset::callback_state,
                      memory_.read(object + actor_offset::callback_state) + 10);
    }
    if (signed32(memory_.read(object + actor_offset::screen_anchor_x)) >= strike_offscreen) return;
    if (memory_.read(object + 6, 1) & final_particle_flag)
        if (notify_controller) notify_controller(memory_.read(object + anchor), strike_finished_state);
    if (release_object) release_object(object);
}
void Effects::sequence_explosions(Address object) {
    const auto state = signed32(memory_.read(object + actor_offset::callback_state));
    if (state == -1) { memory_.write(explosion_running, 0); return; }
    if (memory_.read(object + actor_offset::callback_tick_count) != memory_.read(object + emit_at)) return;
    if (state == 10) {
        memory_.write(explosion_running, 1);
        if (release_object) release_object(object);
        return;
    }
    if (state != 0) return;
    const auto burst = spawn_explosion ? spawn_explosion(memory_.read(object + anchor)) : 0;
    memory_.write(object + emit_at, memory_.read(object + emit_at) + explosion_interval);
    const auto count = memory_.read(object + emit_count) + 1;
    memory_.write(object + emit_count, count);
    if (count != explosion_bursts) return;
    // The last burst carries the flag, and the run waits out one longer gap.
    memory_.write(burst + 6, memory_.read(burst + 6, 1) | final_particle_flag, 1);
    memory_.write(object + emit_at, memory_.read(object + emit_at) + explosion_tail);
    memory_.write(object + actor_offset::callback_state,
                  memory_.read(object + actor_offset::callback_state) + count);
}
namespace {
// The original scatters with a remainder taken about a centred range.
std::uint32_t scatter(Memory& memory, std::int32_t range, std::int32_t centre) {
    return std::uint32_t(std::int32_t(crt_rand(memory)) % range - centre) << 16;
}
// A ground burst is drawn on the tile row its anchor stands on.
std::uint32_t burst_row(Memory& memory, Address anchor_object) {
    const auto row = signed32(memory.read(anchor_object + actor_offset::world_y)) / 0x30 / 0x10000;
    return std::uint32_t(row * 3) << 20;
}
} // namespace

namespace {
// The original halves with a round-toward-zero shift.
std::int32_t half_toward_zero(std::int32_t value) { return (value - (value >> 31)) >> 1; }
// A roll scaled into world units: shift first, then divide.
std::int32_t scaled_roll(Memory& memory, std::int32_t range, std::int32_t divisor) {
    return (std::int32_t(crt_rand(memory)) % range << 16) / divisor;
}
} // namespace

Address Effects::spawn_fountain_particle(Address anchor_object, std::int32_t reach) {
    if (reach > fountain_reach_cap) reach = fountain_reach_cap;
    const auto object = take_object ? take_object(0x466309) : 0;
    memory_.write(object + actor_offset::layer_q16, memory_.read(anchor_object + actor_offset::layer_q16));
    // Land somewhere in what is left of the row, centred on the shortfall.
    const auto along = std::uint32_t(std::int32_t(crt_rand(memory_)) % (fountain_reach - reach)) << 16;
    const auto centre = std::uint32_t(half_toward_zero((reach - fountain_reach) << 16));
    memory_.write(object + actor_offset::world_x,
                  memory_.read(anchor_object + actor_offset::world_x) + along + centre);
    memory_.write(object + actor_offset::world_y, memory_.read(anchor_object + actor_offset::world_y) + 0x10000);
    memory_.write(object + object_motion::target_z, 0);
    memory_.write(object + actor_offset::flags, memory_.read(object + actor_offset::flags) | visible_flag);
    memory_.write(object + actor_offset::elevation, fountain_elevation);
    memory_.write(object + object_motion::mode, fountain_mode);
    memory_.write(object + object_motion::velocity_z, fountain_fall);
    memory_.write(object + anchor, anchor_object);
    memory_.write(object + actor_offset::sprite_selector, fountain_sprite);
    // The farther it is thrown, the higher it arcs.
    memory_.write(object + arc_height,
                  std::uint32_t((reach << 0x12) / fountain_arc_divisor) + fountain_arc_bias);
    memory_.write(object + actor_offset::sprite_frame,
                  std::uint32_t(std::int32_t(crt_rand(memory_)) % std::int32_t(fountain_frames)));
    return object;
}
void Effects::spawn_spark_cluster(Address anchor_object) {
    for (unsigned i = 0; i < spark_copies; ++i) {
        const auto object = take_object ? take_object(0x465dc2) : 0;
        for (const auto field : {actor_offset::layer_q16, actor_offset::world_x,
                                 actor_offset::world_y, actor_offset::elevation})
            memory_.write(object + field, memory_.read(anchor_object + field));
        memory_.write(object + object_motion::mode, spark_mode);
        memory_.write(object + object_motion::target_z, 0);
        // Every spark is pulled back toward the point it was thrown from.
        memory_.write(object + object_motion::target_x, memory_.read(anchor_object + actor_offset::world_x));
        memory_.write(object + object_motion::velocity_x,
                      std::uint32_t(scaled_roll(memory_, spark_spread, spark_scale) - std::int32_t(spark_bias_x)));
        memory_.write(object + actor_offset::flags, memory_.read(object + actor_offset::flags) | visible_flag);
        memory_.write(object + object_motion::acceleration_z, spark_gravity);
        memory_.write(object + actor_offset::sprite_selector, spark_sprite);
        memory_.write(object + object_motion::velocity_z,
                      std::uint32_t(scaled_roll(memory_, spark_spread, spark_scale) - std::int32_t(spark_bias_z)));
        memory_.write(object + actor_offset::sprite_frame,
                      std::uint32_t(std::int32_t(crt_rand(memory_)) % std::int32_t(spark_frames)) + spark_frame_base);
    }
}
void Effects::spawn_swirl_cluster(Address anchor_object) {
    for (unsigned i = 0; i < swirl_copies; ++i) {
        const auto object = take_object ? take_object(0x466da7) : 0;
        for (const auto field : {actor_offset::layer_q16, actor_offset::world_x, actor_offset::world_y})
            memory_.write(object + field, memory_.read(anchor_object + field));
        memory_.write(object + actor_offset::elevation, swirl_elevation);
        memory_.write(object + object_motion::mode, swirl_mode);
        const auto drift = [&] {
            return std::uint32_t(((std::int32_t(crt_rand(memory_)) % swirl_spread - swirl_spread / 2) << 16) / swirl_divisor);
        };
        memory_.write(object + object_motion::velocity_x, drift());
        memory_.write(object + object_motion::velocity_y, drift());
        memory_.write(object + object_motion::acceleration_x, 0);
        memory_.write(object + object_motion::acceleration_y, 0);
        memory_.write(object + object_motion::target_z, 0);
        memory_.write(object + object_motion::acceleration_z, swirl_gravity);
        memory_.write(object + object_motion::velocity_z,
                      std::uint32_t(scaled_roll(memory_, swirl_lift, swirl_divisor)));
        const auto choice = std::uint32_t(std::int32_t(crt_rand(memory_)) % std::int32_t(swirl_script_count));
        if (start_script) start_script(object, memory_.read(swirl_scripts + choice * 4));
    }
}
Address Effects::spawn_explosion_burst(Address anchor_object) {
    const auto object = take_object ? take_object(0x465ee1) : 0;
    memory_.write(object + actor_offset::layer_q16, memory_.read(anchor_object + actor_offset::layer_q16));
    memory_.write(object + actor_offset::world_x, memory_.read(anchor_object + actor_offset::world_x)
                  + scatter(memory_, explosion_spread_x, explosion_spread_x / 2));
    memory_.write(object + actor_offset::world_y, memory_.read(anchor_object + actor_offset::world_y)
                  + scatter(memory_, explosion_spread_y, explosion_spread_y / 5));
    memory_.write(object + actor_offset::elevation, 0);
    memory_.write(object + object_motion::mode, explosion_mode);
    memory_.write(object + object_motion::velocity_z, explosion_rise);
    memory_.write(object + anchor, anchor_object);
    memory_.write(object + object_motion::target_z,
                  std::uint32_t(std::int32_t(crt_rand(memory_)) % explosion_peak + explosion_peak_base) << 16);
    if (start_script) start_script(object, explosion_script);
    if (play_cue) play_cue(explosion_cue);
    return object;
}
void Effects::spawn_ground_burst(Address anchor_object) {
    const auto object = take_object ? take_object(0x46686d) : 0;
    memory_.write(object + actor_offset::layer_q16, memory_.read(anchor_object + actor_offset::layer_q16));
    memory_.write(object + actor_offset::world_x, memory_.read(anchor_object + actor_offset::world_x)
                  + scatter(memory_, burst_spread_x, burst_spread_x / 2));
    memory_.write(object + actor_offset::world_y, burst_row(memory_, anchor_object) + burst_row_bias);
    memory_.write(object + actor_offset::flags, memory_.read(object + actor_offset::flags) | visible_flag);
    memory_.write(object + actor_offset::sprite_selector, burst_sprite);
    memory_.write(object + actor_offset::elevation,
                  std::uint32_t(std::int32_t(crt_rand(memory_)) % burst_height + burst_height_base) << 16);
    memory_.write(object + actor_offset::sprite_frame,
                  std::uint32_t(std::int32_t(crt_rand(memory_)) % std::int32_t(burst_frames)));
    if (play_cue) play_cue(burst_cue);
}
void Effects::spawn_large_ground_burst(Address anchor_object) {
    const auto object = take_object ? take_object(0x46686d) : 0;
    memory_.write(object + actor_offset::layer_q16, memory_.read(anchor_object + actor_offset::layer_q16));
    memory_.write(object + actor_offset::world_x, memory_.read(anchor_object + actor_offset::world_x));
    memory_.write(object + actor_offset::flags, memory_.read(object + actor_offset::flags) | visible_flag);
    memory_.write(object + actor_offset::elevation, big_burst_elevation);
    memory_.write(object + actor_offset::sprite_selector, burst_sprite);
    memory_.write(object + actor_offset::sprite_frame, big_burst_frame);
    memory_.write(object + actor_offset::world_y, burst_row(memory_, anchor_object) + big_burst_row_bias);
    // The large burst is three of the same cue at once.
    for (unsigned i = 0; i < big_burst_cues; ++i) if (play_cue) play_cue(burst_cue);
}
void Effects::spawn_swirl_particle(Address anchor_object) {
    const auto object = take_object ? take_object(0x466cc9) : 0;
    for (const auto field : {actor_offset::layer_q16, actor_offset::world_x,
                             actor_offset::world_y, actor_offset::elevation})
        memory_.write(object + field, memory_.read(anchor_object + field));
    memory_.write(object + object_motion::mode, swirl_mode);
    // Each axis gets the same speed divided by one to four.
    const auto share = [&] {
        return std::uint32_t(swirl_speed / (std::int32_t(crt_rand(memory_)) % std::int32_t(swirl_speed_divisors) + 1));
    };
    memory_.write(object + object_motion::velocity_x, share());
    memory_.write(object + object_motion::velocity_z, swirl_fall);
    memory_.write(object + object_motion::velocity_y, share());
    for (const auto field : {object_motion::acceleration_x, object_motion::acceleration_y,
                             object_motion::acceleration_z, object_motion::target_z})
        memory_.write(object + field, 0);
    const auto choice = std::uint32_t(std::int32_t(crt_rand(memory_)) % std::int32_t(swirl_script_count));
    if (start_script) start_script(object, memory_.read(swirl_scripts + choice * 4));
}
void Effects::grow_explosion(Address object) {
    const auto state = memory_.read(object + actor_offset::callback_state);
    const auto advance = [&](std::uint32_t by) {
        memory_.write(object + actor_offset::callback_state,
                      memory_.read(object + actor_offset::callback_state) + by);
    };
    if (state == 0) {
        if (script_still_running(object)) return;
        if (start_script) start_script(object, explosion_grow_script);
        advance(10);
        return;
    }
    if (state == 10) {
        if (advance_motion) advance_motion(object);
        if (signed32(memory_.read(object + actor_offset::elevation)) < explosion_grow_height) return;
        // High enough: start dragging and wait for the run to finish.
        memory_.write(object + object_motion::acceleration_z, explosion_drag);
        advance(10);
        return;
    }
    if (state != 20) return;
    if (advance_motion) advance_motion(object);
    if (!memory_.read(explosion_running)) return;
    if (memory_.read(object + 6, 1) & final_particle_flag) {
        if (play_cue) play_cue(explosion_finish_cue);
        const auto owner = memory_.read(object + anchor);
        if (notify_controller) { notify_controller(owner, hit_state); notify_controller(owner, done_state); }
    }
    spawn_spark_cluster(object);
    if (release_object) release_object(object);
}
void Effects::drift_swirl_particle(Address object) {
    if (memory_.read(object + actor_offset::callback_state) != 0) return;
    if (advance_motion) advance_motion(object);
    // Once it has stopped falling, the sideways pull decays instead.
    if (memory_.read(object + object_motion::velocity_z) == 0)
        memory_.write(object + object_motion::acceleration_z,
                      std::uint32_t(signed32(memory_.read(object + object_motion::acceleration_z)) / swirl_drag_divisor));
    if (memory_.read(object + actor_offset::callback_tick_count) > 0x30
        || signed32(memory_.read(object + actor_offset::elevation)) <= 0)
        if (release_object) release_object(object);
}
void Effects::emit_fountain(Address object) {
    const auto state = memory_.read(object + actor_offset::callback_state);
    const auto owner = memory_.read(object + anchor);
    const auto owns_cue = owner == memory_.read(cue_owner);
    if (state == 0) {
        if (owns_cue && play_cue) play_cue(fountain_cue);
        memory_.write(object + actor_offset::callback_state, state + 10);
    } else if (state != 10) return;
    const auto age = memory_.read(object + actor_offset::callback_tick_count);
    // Every second tick throws one particle; the last one carries the flag.
    const auto marked = age & 1 ? object : spawn_fountain_particle(owner, std::int32_t(age));
    if (age < fountain_life) return;
    if (owns_cue && stop_cue) stop_cue(fountain_cue);
    memory_.write(marked + 6, memory_.read(marked + 6, 1) | final_particle_flag, 1);
    if (release_object) release_object(object);
}
void Effects::sequence_ground_bursts(Address object) {
    const auto state = signed32(memory_.read(object + actor_offset::callback_state));
    if (state == -1) {
        memory_.write(object + emit_at, 0);
        memory_.write(object + emit_count, burst_run_start);
        return;
    }
    if (memory_.read(object + actor_offset::callback_tick_count) != memory_.read(object + emit_count)) return;
    const auto owner = memory_.read(object + anchor);
    if (state == 20) {
        if (notify_controller) notify_controller(owner, hit_state);
        memory_.write(ground_burst_gate, 1);
        if (release_object) release_object(object);
        return;
    }
    if (state == 0) {
        if (notify_controller) notify_controller(owner, done_state);
        spawn_ground_burst(owner);
        const auto placed = signed32(memory_.read(object + emit_at) + 1u);
        memory_.write(object + emit_at, std::uint32_t(placed));
        // The gap shortens with each burst but never below one tick.
        memory_.write(object + emit_count,
                      memory_.read(object + emit_count) + std::uint32_t(std::max(signed32(std::uint32_t(burst_gap) - std::uint32_t(placed)), 1)));
        if (placed < burst_run_length) return;
    } else if (state == 10) {
        if (notify_controller) notify_controller(owner, done_state);
        spawn_large_ground_burst(owner);
    } else return;
    memory_.write(object + emit_count, memory_.read(object + actor_offset::callback_tick_count) + burst_tail);
    memory_.write(object + actor_offset::callback_state,
                  memory_.read(object + actor_offset::callback_state) + 10);
}
void Effects::fly_debris(Address object) {
    const auto state = memory_.read(object + actor_offset::callback_state);
    if (state == 10) {
        if (advance_motion) advance_motion(object);
        // Blink: visible on three ticks out of four.
        const auto lit = std::int32_t(crt_rand(memory_)) % std::int32_t(debris_blink) != 0;
        const auto flags = memory_.read(object + actor_offset::flags);
        memory_.write(object + actor_offset::flags, lit ? flags | visible_flag : flags & ~visible_flag);
        if (memory_.read(object + actor_offset::callback_tick_count) < debris_life) return;
        // The marked piece reports the hit, and reports it about itself.
        if (memory_.read(object + 6, 1) & final_particle_flag)
            if (notify_controller) notify_controller(object, hit_state);
        if (release_object) release_object(object);
        return;
    }
    if (state != 0) return;
    if (script_still_running(object)) return;
    const auto owner = memory_.read(object + anchor);
    if (notify_controller) notify_controller(owner, done_state);
    memory_.write(object + actor_offset::layer_q16, memory_.read(owner + actor_offset::layer_q16));
    // One roll picks the side, the next how far; the original draws them in
    // that order for each axis.
    const auto thrown = [&](std::int32_t reach, std::int32_t spread) {
        const auto rightward = crt_rand(memory_) & 1;
        const auto distance = std::int32_t(crt_rand(memory_)) % spread;
        return std::uint32_t(rightward ? distance + reach : -reach - distance) << 16;
    };
    memory_.write(object + actor_offset::world_x,
                  thrown(debris_reach_x, debris_spread_x) + memory_.read(owner + actor_offset::world_x));
    const auto sideways = thrown(debris_reach_y, debris_spread_y);
    memory_.write(object + actor_offset::elevation, 0);
    memory_.write(object + object_motion::mode, debris_mode);
    memory_.write(object + actor_offset::world_y,
                  sideways + memory_.read(owner + actor_offset::world_y) + debris_row_bias);
    // It is pulled back to the point it came from.
    memory_.write(object + object_motion::target_x, memory_.read(owner + actor_offset::world_x));
    memory_.write(object + object_motion::acceleration_x, debris_pull_x);
    memory_.write(object + object_motion::target_y, memory_.read(owner + actor_offset::world_y) + debris_row_bias);
    memory_.write(object + object_motion::acceleration_y, debris_pull_y);
    const auto choice = std::uint32_t(std::int32_t(crt_rand(memory_)) % std::int32_t(debris_flight_script_count));
    if (start_script) start_script(object, memory_.read(debris_flight_scripts + choice * 4));
    memory_.write(object + actor_offset::callback_state, state + 10);
}
void Effects::arc_fountain_particle(Address object) {
    const auto state = memory_.read(object + actor_offset::callback_state);
    const auto advance = [&] {
        memory_.write(object + actor_offset::callback_state,
                      memory_.read(object + actor_offset::callback_state) + 10);
    };
    const auto marked = (memory_.read(object + 6, 1) & final_particle_flag) != 0;
    if (state == 0) {
        if (advance_motion) advance_motion(object);
        if (signed32(memory_.read(object + actor_offset::elevation))
            > signed32(memory_.read(object + arc_height))) return;
        // At the top of the arc it starts falling with a sideways drift.
        memory_.write(object + object_motion::mode, memory_.read(object + object_motion::mode) | 0x10u);
        memory_.write(object + object_motion::velocity_z, fountain_descend);
        memory_.write(object + object_motion::acceleration_z, fountain_gravity);
        memory_.write(object + object_motion::velocity_x,
                      std::uint32_t(scaled_roll(memory_, fountain_drift, fountain_drift_scale)) - fountain_drift_bias);
        memory_.write(object + object_motion::target_z, memory_.read(object + arc_height));
        memory_.write(object + actor_offset::elevation, memory_.read(object + arc_height) + fountain_hop);
        advance();
        return;
    }
    if (state == 10) {
        if (advance_motion) advance_motion(object);
        if (signed32(memory_.read(object + actor_offset::elevation))
            > signed32(memory_.read(object + arc_height))) return;
        if (marked) if (notify_controller) notify_controller(object, hit_state);
        advance();
        return;
    }
    if (state == 20) {
        if (!memory_.read(fountain_gate)) return;
        memory_.write(object + object_motion::mode, fountain_spin_mode);
        memory_.write(object + object_motion::target_x, memory_.read(object + actor_offset::world_x));
        memory_.write(object + object_motion::target_y, memory_.read(object + actor_offset::world_y));
        memory_.write(object + spin, 0);
        memory_.write(object + spin_tilt, 0);
        const auto start = std::uint32_t(scaled_roll(memory_, fountain_sway, fountain_sway));
        memory_.write(object + sway_pair, start);
        memory_.write(object + sway_pair_output, start);
        if (marked) if (notify_controller) notify_controller(memory_.read(object + anchor), done_state);
        advance();
        return;
    }
    if (state != 30) return;
    if (advance_motion) advance_motion(object);
    memory_.write(object + spin, memory_.read(object + spin) + std::uint32_t(fountain_spin_step));
    memory_.write(object + spin_tilt, memory_.read(object + spin_tilt) + std::uint32_t(fountain_tilt_step));
    if (signed32(memory_.read(object + spin)) <= fountain_spin_limit) return;
    if (marked) if (notify_controller) notify_controller(memory_.read(object + anchor), landed_state);
    if (release_object) release_object(object);
}
void Effects::spawn_orbit_cluster(Address anchor_object) {
    auto phase = scaled_roll(memory_, orbit_start_phase, orbit_start_phase);
    Address object = 0;
    for (unsigned i = 0; i < orbit_copies; ++i) {
        object = take_object ? take_object(0x466ed1) : 0;
        memory_.write(object + actor_offset::elevation, 0);
        memory_.write(object + actor_offset::layer_q16, memory_.read(anchor_object + actor_offset::layer_q16));
        memory_.write(object + object_motion::mode, fountain_spin_mode);
        memory_.write(object + object_motion::target_x, memory_.read(anchor_object + actor_offset::world_x));
        memory_.write(object + object_motion::target_z, 0);
        memory_.write(object + object_motion::target_y, memory_.read(anchor_object + actor_offset::world_y));
        memory_.write(object + spin, std::uint32_t(orbit_spin));
        memory_.write(object + spin_tilt, std::uint32_t(orbit_tilt));
        memory_.write(object + sway_pair, std::uint32_t(phase));
        memory_.write(object + orbit_angle, std::uint32_t(phase));
        memory_.write(object + orbit_speed, std::uint32_t(orbit_start_phase));
        if (advance_motion) advance_motion(object);
        if (start_script) start_script(object, orbit_script);
        // A third of a turn between one spark and the next.
        phase = (phase + orbit_phase_step) % orbit_turn;
    }
    memory_.write(object + 6, memory_.read(object + 6, 1) | final_particle_flag, 1);
    memory_.write(object + orbit_owner, anchor_object);
    if (play_cue) play_cue(orbit_cue);
}
void Effects::orbit_spark(Address object) {
    const auto state = signed32(memory_.read(object + actor_offset::callback_state));
    if (state == -1) return;
    // Advance the orbit, store it, and wrap it back into one turn.
    const auto turn = [&](std::int32_t by) {
        auto angle = signed32(memory_.read(object + orbit_angle) + std::uint32_t(by));
        memory_.write(object + orbit_angle, std::uint32_t(angle));
        if (angle >= orbit_turn) {
            angle -= orbit_turn;
            memory_.write(object + orbit_angle, std::uint32_t(angle));
        }
        memory_.write(object + sway_pair, std::uint32_t(angle));
    };
    if (state == 0) {
        memory_.write(object + actor_offset::elevation,
                      memory_.read(object + actor_offset::elevation) + std::uint32_t(orbit_turn));
        const auto speed = std::min(signed32(memory_.read(object + orbit_speed) + std::uint32_t(orbit_accelerate)), orbit_top_speed);
        memory_.write(object + orbit_speed, std::uint32_t(speed));
        turn(speed);
        if (advance_motion) advance_motion(object);
        // A swirl particle trails it on every second tick.
        if (!(memory_.read(object + actor_offset::callback_tick_count, 1) & 1)) spawn_swirl_particle(object);
        if (signed32(memory_.read(object + actor_offset::elevation)) <= orbit_rise_limit) return;
        memory_.write(object + actor_offset::callback_state, std::uint32_t(state) + 10);
        return;
    }
    if (state == 10) {
        turn(signed32(memory_.read(object + orbit_speed)));
        const auto held = memory_.read(object + spin_tilt);
        memory_.write(object + spin, held);
        memory_.write(object + spin_tilt, held - 1);
        if (advance_motion) advance_motion(object);
        if (signed32(memory_.read(object + spin)) > 0) return;
        if (memory_.read(object + actor_offset::flags) & orbit_reports) {
            if (notify_controller) notify_controller(memory_.read(object + orbit_owner), hit_state);
            if (memory_.read(object + actor_offset::flags) & orbit_reports)
                if (play_cue) play_cue(orbit_break_cue);
        }
        memory_.write(object + arc_height, 0);
        memory_.write(object + actor_offset::callback_state, std::uint32_t(state) + 10);
        return;
    }
    if (state != 20) return;
    // Unwinding: the tilt grows faster every tick until it flies apart.
    memory_.write(object + arc_height, memory_.read(object + arc_height) + std::uint32_t(orbit_unwind));
    const auto tilt = memory_.read(object + spin_tilt) + memory_.read(object + arc_height);
    memory_.write(object + spin_tilt, tilt);
    memory_.write(object + spin, tilt);
    if (advance_motion) advance_motion(object);
    if (signed32(memory_.read(object + spin)) < orbit_unwind_limit) return;
    if (memory_.read(object + 6, 1) & final_particle_flag)
        if (notify_controller) notify_controller(memory_.read(object + orbit_owner), done_state);
    if (release_object) release_object(object);
}
void Effects::spawn_beam(Address anchor_object) {
    const auto drop = [&](Address tick, std::uint32_t across) {
        const auto object = take_object ? take_object(tick) : 0;
        memory_.write(object + actor_offset::layer_q16, memory_.read(anchor_object + actor_offset::layer_q16));
        memory_.write(object + actor_offset::world_x, memory_.read(anchor_object + actor_offset::world_x) + across);
        memory_.write(object + actor_offset::world_y, memory_.read(anchor_object + actor_offset::world_y) + beam_row);
        memory_.write(object + actor_offset::elevation, memory_.read(anchor_object + actor_offset::elevation) + beam_top);
        memory_.write(object + object_motion::mode, explosion_mode);
        memory_.write(object + object_motion::velocity_z, beam_fall);
        memory_.write(object + beam_fall_at, memory_.read(anchor_object + actor_offset::elevation) + beam_target);
        memory_.write(object + beam_hold_until, std::uint32_t(beam_hold));
        return object;
    };
    memory_.write(drop(0x46570a, 0) + beam_start_at, std::uint32_t(beam_core_delay));
    // The charges start one tile left of the anchor and fire in turn.
    Address object = 0;
    std::uint32_t across = 0u - beam_spacing;
    for (auto delay = beam_first_delay; delay < beam_last_delay; delay += beam_delay_step) {
        object = drop(0x46562e, across);
        memory_.write(object + beam_start_at, std::uint32_t(delay));
        across += beam_spacing;
    }
    memory_.write(object + 6, memory_.read(object + 6, 1) | final_particle_flag, 1);
    if (play_cue) { play_cue(beam_open_cue); play_cue(beam_hum_cue); }
}
void Effects::tick_beam_charge(Address object) {
    const auto state = signed32(memory_.read(object + actor_offset::callback_state));
    const auto advance = [&] {
        memory_.write(object + actor_offset::callback_state,
                      memory_.read(object + actor_offset::callback_state) + 10);
    };
    const auto landed = [&] {
        return signed32(memory_.read(object + beam_fall_at)) >= signed32(memory_.read(object + actor_offset::elevation));
    };
    const auto reached = [&](Address at) {
        return signed32(memory_.read(object + actor_offset::callback_tick_count)) >= signed32(memory_.read(object + at));
    };
    if (state == -1) { if (start_script) start_script(object, beam_charge_script); return; }
    if (state == 0) {
        if (advance_motion) advance_motion(object);
        if (!landed()) return;
        if (memory_.read(object + 6, 1) & final_particle_flag) if (play_cue) play_cue(beam_hold_cue);
        advance();
        return;
    }
    if (state == 10) {
        if (!reached(beam_start_at)) return;
        if (start_script) start_script(object, beam_fire_script);
        advance();
        if (play_cue) play_cue(beam_fire_cue);
        return;
    }
    if (state == 20) {
        if (!reached(beam_hold_until)) return;
        // Reverse the fall so it climbs back out of the field.
        memory_.write(object + object_motion::velocity_z,
                      (0u - memory_.read(object + object_motion::velocity_z)));
        memory_.write(object + actor_offset::callback_state, 30);
        return;
    }
    if (state != 30) return;
    if (advance_motion) advance_motion(object);
    if (memory_.read(object + actor_offset::elevation) < beam_top) return;
    if (release_object) release_object(object);
}
void Effects::tick_beam_core(Address object) {
    const auto state = signed32(memory_.read(object + actor_offset::callback_state));
    const auto advance = [&] {
        memory_.write(object + actor_offset::callback_state,
                      memory_.read(object + actor_offset::callback_state) + 10);
    };
    const auto reached = [&](Address at) {
        return signed32(memory_.read(object + actor_offset::callback_tick_count)) >= signed32(memory_.read(object + at));
    };
    if (state == -1) { if (start_script) start_script(object, beam_core_script); return; }
    if (state == 0) {
        if (advance_motion) advance_motion(object);
        if (signed32(memory_.read(object + beam_fall_at)) < signed32(memory_.read(object + actor_offset::elevation))) return;
        advance();
        return;
    }
    if (state == 10) {
        if (!reached(beam_start_at)) return;
        if (stop_cue) stop_cue(beam_hum_cue);
        if (start_script) start_script(object, beam_core_fire_script);
        advance();
        return;
    }
    if (state == 20) {
        if (!reached(beam_hold_until)) return;
        if (stop_cue) stop_cue(beam_hold_cue);
        memory_.write(object + object_motion::velocity_z,
                      (0u - memory_.read(object + object_motion::velocity_z)));
        advance();
        return;
    }
    if (state != 30) return;
    if (advance_motion) advance_motion(object);
    if (memory_.read(object + actor_offset::elevation) < beam_top) return;
    if (notify_controller) notify_controller(object, hit_state);
    if (release_object) release_object(object);
}
void Effects::spawn_floating_number(Address actor, std::int32_t amount, bool alternate_glyphs) {
    const auto object = take_object ? take_object(alternate_glyphs ? 0x464a9d : 0x4645e7) : 0;
    unsigned length = 0;
    if (amount >= 0) length = write_number ? write_number(object + glyphs, amount) : 0;
    else {
        // A miss prints its own glyph run rather than a number.
        memory_.write(object + glyphs + std::size(miss_glyphs), 0, 1);
        for (unsigned i = 0; i < std::size(miss_glyphs); ++i)
            memory_.write(object + glyphs + i, miss_glyphs[i], 1);
        length = unsigned(std::size(miss_glyphs));
    }
    // Centre the run over the actor's tile.
    memory_.write(object + glyph_origin,
                  (memory_.read(actor + actor_offset::tile_x) << number_tile_shift)
                  - ((length * glyph_spacing) << 16) + std::uint32_t(number_centre));
    for (const auto field : {actor_offset::layer_q16, actor_offset::world_x,
                             actor_offset::world_y, actor_offset::elevation})
        memory_.write(object + field, memory_.read(actor + field));
    memory_.write(object + number_owner, actor);
}
void Effects::spawn_glyph_number(Address actor, std::int32_t amount, std::int32_t glyph_bias) {
    // A miss prints its own glyph run rather than a number.
    std::string text;
    if (amount >= 0) text = number_text ? number_text(amount) : std::string();
    else text.assign(std::begin(miss_glyphs), std::end(miss_glyphs));
    auto across = (memory_.read(actor + actor_offset::tile_x) << number_tile_shift)
                - ((unsigned(text.size()) * glyph_spacing) << 16) + std::uint32_t(number_centre);
    Address object = 0;
    std::uint32_t flags = 0;
    for (const auto glyph : text) {
        object = take_object ? take_object(0x46474d) : 0;
        memory_.write(object + actor_offset::flags, memory_.read(object + actor_offset::flags) | visible_flag);
        memory_.write(object + actor_offset::sprite_selector, digit_sprite);
        flags = memory_.read(object + actor_offset::flags);
        memory_.write(object + actor_offset::sprite_frame, std::uint32_t(std::int32_t(std::int8_t(glyph)) - glyph_bias));
        memory_.write(object + actor_offset::layer_q16, memory_.read(actor + actor_offset::layer_q16));
        memory_.write(object + actor_offset::world_x, across);
        memory_.write(object + object_motion::target_z, 0);
        memory_.write(object + actor_offset::world_y, memory_.read(actor + actor_offset::world_y) + glyph_set_row);
        memory_.write(object + actor_offset::elevation, glyph_set_elevation);
        memory_.write(object + object_motion::mode, explosion_mode);
        memory_.write(object + object_motion::velocity_z, glyph_set_rise);
        across += glyph_set_spacing;
    }
    // Only the last digit reports back, and it names the actor it belongs to.
    memory_.write(object + digit_owner, actor);
    memory_.write(object + actor_offset::flags, flags | orbit_reports);
}
void Effects::step_floating_number(Address object, std::int32_t glyph_bias) {
    if (memory_.read(object + actor_offset::callback_state) != 0) return;
    // One digit every second tick.
    if (!(memory_.read(object + actor_offset::callback_tick_count, 1) & 1)) return;
    const auto digit = take_object ? take_object(0x464512) : 0;
    memory_.write(digit + actor_offset::flags, memory_.read(digit + actor_offset::flags) | visible_flag);
    memory_.write(digit + actor_offset::sprite_selector, digit_sprite);
    const auto at = memory_.read(object + glyph_cursor);
    const auto glyph = signed32(memory_.read(object + glyphs + at, 1) << 24) >> 24;
    memory_.write(digit + object_motion::mode, explosion_mode);
    memory_.write(digit + actor_offset::sprite_frame, std::uint32_t(glyph - glyph_bias));
    memory_.write(digit + actor_offset::layer_q16, memory_.read(object + actor_offset::layer_q16));
    memory_.write(digit + actor_offset::world_x, memory_.read(object + glyph_origin)
                  + ((at * glyph_spacing) << glyph_spacing_shift));
    memory_.write(digit + object_motion::target_z, 0);
    memory_.write(digit + actor_offset::world_y, memory_.read(object + actor_offset::world_y) + digit_row);
    memory_.write(digit + actor_offset::elevation, digit_start);
    memory_.write(digit + object_motion::velocity_z, digit_rise);
    memory_.write(digit + object_motion::acceleration_z, digit_gravity);
    memory_.write(object + glyph_cursor, at + 1);
    // The digit that empties the text carries the report and ends the spawner.
    if (memory_.read(object + glyphs + memory_.read(object + glyph_cursor), 1)) return;
    memory_.write(digit + 6, memory_.read(digit + 6, 1) | final_particle_flag, 1);
    memory_.write(digit + digit_owner, memory_.read(object + number_owner));
    if (release_object) release_object(object);
}
void Effects::tick_floating_digit(Address object) {
    const auto state = signed32(memory_.read(object + actor_offset::callback_state));
    const auto step = [&] {
        memory_.write(object + actor_offset::callback_state,
                      memory_.read(object + actor_offset::callback_state) + 1);
    };
    if (state == 0) {
        if (advance_motion) advance_motion(object);
        if (memory_.read(object + actor_offset::elevation) >= digit_land) return;
        // Landed: bounce once.
        step();
        memory_.write(object + actor_offset::elevation, digit_land);
        memory_.write(object + object_motion::velocity_z, digit_bounce);
        return;
    }
    if (state == 1) {
        if (advance_motion) advance_motion(object);
        if (memory_.read(object + actor_offset::elevation) >= digit_land) return;
        step();
        memory_.write(object + actor_offset::elevation, digit_land);
        memory_.write(object + glyph_cursor,
                      memory_.read(object + actor_offset::callback_tick_count) + std::uint32_t(digit_hold));
        if (memory_.read(object + 6, 1) & final_particle_flag)
            if (notify_controller) notify_controller(memory_.read(object + digit_owner), digit_shown_state);
        return;
    }
    if (state == 2) {
        if (memory_.read(object + actor_offset::callback_tick_count) != memory_.read(object + glyph_cursor)) return;
        memory_.write(object + glyph_cursor,
                      memory_.read(object + actor_offset::callback_tick_count) + std::uint32_t(digit_hold));
        memory_.write(object + actor_offset::callback_state, 3);
        return;
    }
    if (state != 3) return;
    // Blinking out: the flag flips every tick whether or not it is the last.
    const auto age = memory_.read(object + actor_offset::callback_tick_count);
    memory_.write(object + actor_offset::flags, memory_.read(object + actor_offset::flags) ^ visible_flag);
    if (age != memory_.read(object + glyph_cursor)) return;
    if (release_object) release_object(object);
}
void Effects::tick_floating_digit_alt(Address object) {
    const auto state = signed32(memory_.read(object + actor_offset::callback_state));
    if (state == 0) {
        if (advance_motion) advance_motion(object);
        if (memory_.read(object + actor_offset::elevation) >= digit_alt_land) return;
        memory_.write(object + actor_offset::callback_state,
                      memory_.read(object + actor_offset::callback_state) + 1);
        if (memory_.read(object + 6, 1) & final_particle_flag)
            if (notify_controller) notify_controller(memory_.read(object + digit_owner), digit_shown_state);
        return;
    }
    if (state != 1) return;
    if (advance_motion) advance_motion(object);
    memory_.write(object + actor_offset::flags, memory_.read(object + actor_offset::flags) ^ visible_flag);
    if (memory_.read(object + actor_offset::elevation) >= digit_alt_gone) return;
    if (release_object) release_object(object);
}
void Effects::retire_when_gate_open(Address object) {
    if (memory_.read(object + actor_offset::callback_state) != 0) return;
    if (!memory_.read(ground_burst_gate)) return;
    if (release_object) release_object(object);
}
void Effects::shrink_then_retire(Address object) {
    const auto state = signed32(memory_.read(object + actor_offset::callback_state));
    if (state == -1 || state != 0) return;
    if (advance_motion) advance_motion(object);
    const auto aged = memory_.read(object + actor_offset::callback_tick_count) > 0x30;
    if (aged || signed32(memory_.read(object + actor_offset::elevation)) <= 0)
        if (release_object) release_object(object);
}
void Effects::flicker_then_retire(Address object) {
    const auto state = memory_.read(object + actor_offset::callback_state);
    if (state == 0) {
        if (advance_motion) advance_motion(object);
        // After the drift, move on to the flicker phase.
        if (memory_.read(object + actor_offset::callback_tick_count) > 8)
            memory_.write(object + actor_offset::callback_state, state + 10);
        return;
    }
    if (state != 10) return;
    if (advance_motion) advance_motion(object);
    memory_.write(object + actor_offset::flags, memory_.read(object + actor_offset::flags) ^ visible_flag);
    if (memory_.read(object + actor_offset::callback_tick_count) > 0x18)
        if (release_object) release_object(object);
}
void Effects::sway_then_retire(Address object) {
    if (memory_.read(object + actor_offset::callback_state) != 0) return;
    // One whole step either way, chosen by a single bit of the roll.
    const auto step = crt_rand(memory_) & 1 ? sway_step : -sway_step;
    const auto swayed = memory_.read(object + sway) + std::uint32_t(step);
    memory_.write(object + sway, swayed);
    memory_.write(object + sway_output, swayed);
    if (advance_motion) advance_motion(object);
    if (memory_.read(object + actor_offset::callback_tick_count) > 0x50)
        if (release_object) release_object(object);
}
void Effects::emit_debris(Address object) {
    if (memory_.read(object + actor_offset::callback_state) != 0) return;
    if (memory_.read(object + actor_offset::callback_tick_count) != memory_.read(object + emit_at)) return;
    const auto particle = spawn_debris ? spawn_debris(memory_.read(object + anchor)) : 0;
    const auto count = memory_.read(object + emitted) + 1;
    memory_.write(object + emitted, count);
    if (count == debris_particles) {
        // The last particle carries the flag that ends the burst.
        memory_.write(particle + 6, memory_.read(particle + 6, 1) | final_particle_flag, 1);
        if (release_object) release_object(object);
    }
    memory_.write(object + emit_at, memory_.read(object + emit_at) + debris_interval);
}
void Effects::retire_when_finished(Address object) {
    const auto state = signed32(memory_.read(object + actor_offset::callback_state));
    if (state == -1 || state != 0) return;
    if (script_still_running(object)) return;
    if (release_object) release_object(object);
}
void Effects::notify_and_retire(Address object) {
    if (memory_.read(object + actor_offset::callback_state) != 0) return;
    if (script_still_running(object)) return;
    if (notify_controller) notify_controller(object, signed32(memory_.read(object + controller_state)));
    if (release_object) release_object(object);
}
void Effects::rise_then_retire(Address object) {
    if (memory_.read(object + actor_offset::callback_state) != 0) return;
    memory_.write(object + actor_offset::elevation,
                  memory_.read(object + actor_offset::elevation) + rise_per_tick);
    if (script_still_running(object)) return;
    if (release_object) release_object(object);
}
} // namespace fsb::core::combat
