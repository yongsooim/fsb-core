#pragma once
#include "fsb_core/primitives.hpp"
#include <functional>

namespace fsb::core::combat {
namespace effects {
// A battle effect object is an actor record driven by an effect script. These
// are the fields the battle's own spawners fill in beyond the actor's.
inline constexpr Address controller_state = 0x190;
// The object this effect follows and reports back to.
inline constexpr Address anchor = 0x160;
// Effect scripts the battle's own spawners start.
inline constexpr Address hit_spark_script = 0x5d3b40, phase_finish_script = 0x5d3ae8;
// State a finished status phase reports to its anchor, and the cue it plays.
inline constexpr std::int32_t phase_finished_state = -200;
inline constexpr unsigned phase_finish_cue = 0xdb;
// Work fields the battle's own effect ticks keep beside the actor record.
inline constexpr Address emit_at = 0x16c, emitted = 0x178, emit_count = 0x170;
inline constexpr Address strike_target = 0x190;
// Scripts and cues the strike, debris and explosion effects use.
inline constexpr Address strike_fall_script = 0x5d3a18, strike_impact_script = 0x5d3a30;
inline constexpr Address debris_scripts = 0x5d3c58;
inline constexpr unsigned debris_script_count = 5, debris_cue = 0xc2, strike_cue = 0xd9;
inline constexpr std::int32_t strike_finished_state = -250;
// A falling strike is gone once it has passed this far up the screen.
inline constexpr std::int32_t strike_offscreen = -0x78;
inline constexpr std::uint32_t strike_flags = 0xe;
inline constexpr unsigned strike_copies = 4, explosion_bursts = 10;
inline constexpr Address strike_origin = 0x7873c0, explosion_running = 0x80650c;
// Explosion bursts scatter around the anchor and rise to a random height.
inline constexpr Address explosion_script = 0x5d3d88;
inline constexpr unsigned explosion_cue = 0xc4;
inline constexpr std::int32_t explosion_spread_x = 0x30, explosion_spread_y = 0x14;
inline constexpr std::int32_t explosion_peak = 8, explosion_peak_base = 0x30;
inline constexpr std::uint32_t explosion_mode = 0x41, explosion_rise = 0x20000;
// Ground bursts sit on the anchor's own row, one sprite per burst.
inline constexpr unsigned burst_cue = 0xeb, burst_sprite = 0xdc, burst_frames = 4;
inline constexpr std::int32_t burst_spread_x = 0x18, burst_height = 0x10, burst_height_base = 0x2c;
inline constexpr std::uint32_t burst_row_bias = 0x190000, big_burst_row_bias = 0x1a0000;
inline constexpr std::uint32_t big_burst_elevation = 0x340000;
inline constexpr unsigned big_burst_frame = 8, big_burst_cues = 3;
// Swirl particles drift outward at a fraction of one speed and fall.
inline constexpr Address swirl_scripts = 0x5d3e38;
inline constexpr unsigned swirl_script_count = 4, swirl_speed_divisors = 4;
inline constexpr std::int32_t swirl_speed = 0x4000;
inline constexpr std::uint32_t swirl_mode = 0x71, swirl_fall = 0xfffe0000;
// A fountain particle arcs from a point along the anchor's row.
inline constexpr Address arc_height = 0x198;
inline constexpr std::int32_t fountain_reach = 0x50, fountain_reach_cap = 0x3f;
inline constexpr std::int32_t fountain_arc_divisor = 3;
inline constexpr std::uint32_t fountain_arc_bias = 0x80000, fountain_elevation = 0x2580000;
inline constexpr std::uint32_t fountain_fall = 0xfff00000, fountain_mode = 0x41;
inline constexpr unsigned fountain_sprite = 0xde, fountain_frames = 0xd;
// A spark cluster is six sparks thrown from one point back toward it.
inline constexpr unsigned spark_copies = 6, spark_sprite = 0xda, spark_frames = 8;
inline constexpr std::int32_t spark_spread = 0xbb8, spark_scale = 0x3e8;
inline constexpr std::uint32_t spark_mode = 0x51, spark_gravity = 0x4000;
inline constexpr std::uint32_t spark_bias_x = 0x18000, spark_bias_z = 0x10000;
inline constexpr unsigned spark_frame_base = 0x10;
// A swirl cluster is thirty-two particles rising from above the anchor.
inline constexpr unsigned swirl_copies = 0x20;
inline constexpr std::int32_t swirl_spread = 0x20, swirl_lift = 0x40, swirl_divisor = 0x10;
inline constexpr std::uint32_t swirl_elevation = 0x800000, swirl_gravity = 0x8000;
// A growing explosion rises, then drags, then finishes the whole run.
inline constexpr Address explosion_grow_script = 0x5d3de0;
inline constexpr std::int32_t explosion_grow_height = 0x300000;
inline constexpr std::uint32_t explosion_drag = 0x1999;
inline constexpr unsigned explosion_finish_cue = 0xc5;
// A fountain emits on every second tick while its anchor holds the cue.
inline constexpr Address cue_owner = 0x8059f8;
inline constexpr unsigned fountain_cue = 0xc3, fountain_life = 0x40;
// A ground burst run tightens its gap until it has placed enough bursts.
inline constexpr std::int32_t burst_gap = 0xc, burst_run_length = 0x10;
inline constexpr std::uint32_t burst_tail = 0x20;
inline constexpr unsigned burst_run_start = 8;
// States the effects report back to their controller.
inline constexpr std::int32_t hit_state = -200, done_state = -250;
inline constexpr std::int32_t swirl_drag_divisor = 4;
// Debris is thrown to either side of its anchor and blinks as it flies.
inline constexpr Address debris_flight_scripts = 0x5d3d78;
inline constexpr unsigned debris_flight_script_count = 3, debris_life = 0x3c, debris_blink = 4;
inline constexpr std::int32_t debris_reach_x = 0x18, debris_spread_x = 8;
inline constexpr std::int32_t debris_reach_y = 6, debris_spread_y = 6;
inline constexpr std::uint32_t debris_mode = 0x31, debris_pull_x = 0x8000, debris_pull_y = 0x6000;
inline constexpr std::uint32_t debris_row_bias = 0x80000;
// A fountain particle rises, hangs, then spins down to the ground.
inline constexpr Address spin = 0x190, spin_tilt = 0x194, sway_pair = 0x19c, sway_pair_output = 0x1a0;
inline constexpr Address fountain_gate = 0x806510;
inline constexpr std::int32_t fountain_drift = 0x3e8, fountain_drift_scale = 0x7d0;
inline constexpr std::uint32_t fountain_drift_bias = 0x4000, fountain_descend = 0x100000;
inline constexpr std::uint32_t fountain_gravity = 0x20000, fountain_hop = 0x10000;
inline constexpr std::uint32_t fountain_spin_mode = 0x32;
inline constexpr std::int32_t fountain_sway = 0x1000;
inline constexpr std::int32_t fountain_spin_step = 0x20, fountain_tilt_step = 0x18, fountain_spin_limit = 0x190;
inline constexpr std::int32_t landed_state = -260;
// Three orbiting sparks circling their anchor, evenly spaced in phase.
inline constexpr Address orbit_owner = 0x178, orbit_angle = 0x1a0, orbit_speed = 0x1a4;
inline constexpr Address orbit_script = 0x5d34e0;
inline constexpr unsigned orbit_copies = 3, orbit_cue = 0xca, orbit_break_cue = 0xcb;
inline constexpr std::int32_t orbit_phase_step = 0x5555, orbit_turn = 0x10000;
inline constexpr std::int32_t orbit_start_phase = 0x400;
inline constexpr std::int32_t orbit_accelerate = 0x20, orbit_top_speed = 0x1000;
inline constexpr std::int32_t orbit_rise_limit = 0x600000, orbit_unwind = 0xa, orbit_unwind_limit = 0x258;
inline constexpr std::int32_t orbit_spin = 0x20, orbit_tilt = 0x18;
inline constexpr std::uint32_t orbit_reports = 0x40000;
// A beam: one core plus three charges spaced along the row, all dropped from
// above and pulled back up once they land.
inline constexpr Address beam_fall_at = 0x18c, beam_start_at = 0x190, beam_hold_until = 0x194;
inline constexpr Address beam_charge_script = 0x5d3b88, beam_fire_script = 0x5d3ba8;
inline constexpr Address beam_core_script = 0x5d3bb8, beam_core_fire_script = 0x5d3bc8;
inline constexpr unsigned beam_open_cue = 0x11f, beam_hum_cue = 0x120;
inline constexpr unsigned beam_fire_cue = 0x121, beam_hold_cue = 0x162;
inline constexpr std::uint32_t beam_row = 0x1980000, beam_top = 0x3780000, beam_target = 0x1b00000;
inline constexpr std::uint32_t beam_fall = 0xfff80000, beam_spacing = 0x100000;
inline constexpr std::int32_t beam_first_delay = 0x78, beam_delay_step = 0x1e, beam_last_delay = 0xd2;
inline constexpr std::int32_t beam_core_delay = 0xd4, beam_hold = 0x12c;
// A floating number: the spawner holds the digits as text and releases one per
// tick pair, each bouncing once before it blinks out.
inline constexpr Address glyphs = 0x3c, glyph_cursor = 0x190, glyph_origin = 0x194;
inline constexpr Address number_owner = 0x198, digit_owner = 0x1a8;
inline constexpr unsigned digit_sprite = 0xd6;
// Two glyph sets share one digit sprite sheet at different offsets.
inline constexpr std::int32_t digit_glyph_bias = 0x23, alt_glyph_bias = 9;
inline constexpr std::int32_t glyph_spacing_shift = 0x11, glyph_spacing = 5;
// A negative amount prints a fixed four-glyph run instead of a number.
inline constexpr std::uint8_t miss_glyphs[] = {0x3a, 0x3b, 0x3c, 0x3c};
inline constexpr std::int32_t number_centre = 0x200000, number_tile_shift = 0x16;
// The glyph-set variants place every digit at once instead of one per tick.
inline constexpr std::int32_t glyph_set_bias = 0x30, alt_glyph_set_bias = 0x16;
inline constexpr std::uint32_t glyph_set_row = 0x1940000, glyph_set_elevation = 0x1e80000;
inline constexpr std::uint32_t glyph_set_rise = 0xffff8000, glyph_set_spacing = 0xa0000;
inline constexpr std::uint32_t digit_row = 0x1900000, digit_start = 0x1ac0000;
inline constexpr std::uint32_t digit_rise = 0x100000, digit_gravity = 0x18000;
inline constexpr std::uint32_t digit_land = 0x1a80000, digit_bounce = 0x80000;
inline constexpr std::uint32_t digit_alt_land = 0x1cc0000, digit_alt_gone = 0x1c00000;
inline constexpr std::int32_t digit_hold = 0x20, digit_shown_state = -20;
inline constexpr std::uint32_t strike_fall_step = 0x80000, strike_lift = 0x400000;
inline constexpr std::uint32_t explosion_interval = 0x10, explosion_tail = 0x1c;
inline constexpr Address sway = 0x19c, sway_output = 0x1a4;
// The debris burst emits four particles, then marks the last one.
inline constexpr unsigned debris_particles = 4, debris_interval = 4;
inline constexpr std::uint32_t final_particle_flag = 4;
// While this is clear, a ground burst keeps living regardless of its age.
inline constexpr Address ground_burst_gate = 0x806514;
inline constexpr std::uint32_t visible_flag = 0x40;
inline constexpr std::int32_t sway_step = 256;
// Bit of the flags word saying an effect script is still running on it. The
// original tests it as bit1 of the third flags byte.
inline constexpr std::uint32_t script_running = 0x20000;
// Effect objects rise by one tile per tick while their script plays.
inline constexpr std::uint32_t rise_per_tick = 0x10000;
} // namespace effects

// Spawning and retiring the battle's own effect objects. The object pool and
// the effect interpreter belong to other owners; both stay explicit calls.
class Effects {
public:
    explicit Effects(Memory& memory) : memory_(memory) {}
    std::function<Address(Address callback)> take_object;          // 45d89c
    std::function<void(Address object)> release_object;            // 45d91d
    std::function<void(Address object, Address script)> start_script;        // 447841
    std::function<void(Address object, std::int32_t state)> notify_controller; // 461d25
    std::function<void(unsigned cue)> play_cue;                    // 435373
    std::function<void(unsigned cue)> stop_cue;                    // 4353cb
    // Formats the amount into the object's own text and reports its length.
    std::function<unsigned(Address text, std::int32_t amount)> write_number;
    // The decimal text on its own, for the variants that keep it in a local.
    std::function<std::string(std::int32_t amount)> number_text;
    std::function<void(Address object)> advance_motion;            // 45d208
    std::function<Address(Address anchor_object)> spawn_debris;    // 465abd
    std::function<Address(Address anchor_object)> spawn_explosion; // 465f7e
    std::function<void(Address object, std::int32_t x, std::int32_t y, std::int32_t z)> place_on_tile; // 45d774

    // 464c27: place an effect object in the world and start its script.
    void spawn_scripted(std::int32_t x, std::int32_t y, std::int32_t elevation,
                        std::uint32_t layer, Address script);
    // 464cf4: the same, but the object tells a controller when it finishes.
    void spawn_notifying(std::int32_t x, std::int32_t y, std::int32_t elevation,
                         std::uint32_t layer, Address script, std::int32_t state);

    // 465b66/4665ca/466a98: take an object whose own tick does the work, and
    // point it at the anchor it belongs to.
    void attach_to(Address tick, Address anchor_object);
    // 4660a3: the same, with a starting horizontal speed.
    void attach_to_drifting(Address tick, Address anchor_object, std::int32_t speed);
    // 465241: a hit spark, one tile behind the anchor and up to four tiles above.
    void spawn_hit_spark(Address anchor_object);
    // 4653d9: the object that closes a status phase, above and in front of it.
    void spawn_phase_finish(Address anchor_object);
    // 4653a5: retire it and tell its anchor the phase is over.
    void notify_anchor_and_retire(Address object);

    // 465abd: one debris particle on the anchor, with one of five scripts.
    Address spawn_debris_particle(Address anchor_object);
    // 46532a: four falling strikes across the anchor's row; the last one carries
    // the flag that reports the hit.
    void spawn_falling_strikes(Address anchor_object);
    // 46528d: fall a tile per tick until the target row, play the impact, then
    // leave once past the top of the screen.
    void fall_and_strike(Address object);
    // 466015: emit explosions on a schedule and mark the run as finished.
    void sequence_explosions(Address object);

    // 465f7e: one explosion burst scattered around the anchor.
    Address spawn_explosion_burst(Address anchor_object);
    // 46688c/46691c: a burst on the anchor's row, small or large.
    void spawn_ground_burst(Address anchor_object);
    void spawn_large_ground_burst(Address anchor_object);
    // 466cfc: a swirl particle drifting outward and falling.
    void spawn_swirl_particle(Address anchor_object);

    // 46648b: one fountain particle, thrown `reach` tiles along the row.
    Address spawn_fountain_particle(Address anchor_object, std::int32_t reach);
    // 465e0b: six sparks thrown outward, each drawn back to the anchor.
    void spawn_spark_cluster(Address anchor_object);
    // 466df3: thirty-two swirl particles falling from above the anchor.
    void spawn_swirl_cluster(Address anchor_object);

    // 465ee1: rise, then drag, then close out the whole explosion run.
    void grow_explosion(Address object);
    // 466da7: drift, losing sideways pull once the fall has stopped.
    void drift_swirl_particle(Address object);
    // 466545: emit a fountain particle every second tick for its lifetime.
    void emit_fountain(Address object);
    // 466992: place ground bursts on a tightening schedule, then one large one.
    void sequence_ground_bursts(Address object);

    // 46596b: throw the debris outward on its first tick, then fly it.
    void fly_debris(Address object);
    // 466309: rise, hang, then spin down to the ground.
    void arc_fountain_particle(Address object);

    // 467040: three sparks set orbiting the anchor a third of a turn apart.
    void spawn_orbit_cluster(Address anchor_object);
    // 466ed1: rise while speeding up, hold, then unwind and leave.
    void orbit_spark(Address object);

    // 4657d7: one beam core and three charges dropped along the anchor's row.
    void spawn_beam(Address anchor_object);
    // 46562e/46570a: a beam part falls, waits its turn, fires, then leaves.
    void tick_beam_charge(Address object);
    void tick_beam_core(Address object);

    // 4646be/464b74: put a floating number over an actor. A negative amount
    // prints the miss glyphs instead of digits.
    void spawn_floating_number(Address actor, std::int32_t amount, bool alternate_glyphs);
    // 4647ad/464925: place every digit of a number at once, spaced along the
    // row. The last digit carries the report.
    void spawn_glyph_number(Address actor, std::int32_t amount, std::int32_t glyph_bias);
    // 4645e7/464a9d: release one digit of the floating number every second tick.
    void step_floating_number(Address object, std::int32_t glyph_bias);
    // 464512/46474d: a digit bounces once, holds, then blinks out.
    void tick_floating_digit(Address object);
    void tick_floating_digit_alt(Address object);

    // 46686d: a ground burst lives until the battle says bursts may end.
    void retire_when_gate_open(Address object);
    // 466cc9: shrink until either the age or the height runs out.
    void shrink_then_retire(Address object);
    // 465dc2: drift, then flicker by toggling visibility, then retire.
    void flicker_then_retire(Address object);
    // 46265d: sway left and right by a whole step each tick, then retire.
    void sway_then_retire(Address object);
    // 465b14: emit one debris particle every few ticks, marking the last.
    void emit_debris(Address object);

    // 464c05: retire the object once its script has stopped. A callback state
    // the script never started (-1) is left alone.
    void retire_when_finished(Address object);
    // 464d2f: retire it and tell its controller first.
    void notify_and_retire(Address object);
    // 46521e: drift upward each tick, then retire when the script stops.
    void rise_then_retire(Address object);

private:
    Memory& memory_;
    bool script_still_running(Address object) const;
    Address place(Address callback, std::int32_t x, std::int32_t y,
                  std::int32_t elevation, std::uint32_t layer);
};
} // namespace fsb::core::combat
