#pragma once
#include "fsb_core/primitives.hpp"
#include <functional>
namespace fsb::core::combat {
namespace anchor_effects {
inline constexpr Address anchor=0x160, motion=0x168, next_mark=0x16c, velocity_z=0x174;
inline constexpr Address step_z=0x180, offset_x=0x184, offset_y=0x188, target_z=0x18c;
inline constexpr Address radius_x=0x190, radius_y=0x194, radius_z=0x198;
inline constexpr Address angle_x=0x19c, angle_y=0x1a0, angle_z=0x1a4, next_rising=0x198;
inline constexpr Address children=0x3c, mark_callback=0x4624ad, rising_callback=0x46265d, orbit_callback=0x4629cb;
inline constexpr Address mark_script=0x5d3d88, explosion_script=0x5d3de0, ring_script=0x5d26f0;
inline constexpr Address scatter_scripts=0x5d3d78, scatter_velocity_x=0x16c, scatter_velocity_y=0x170;
inline constexpr Address scatter_acceleration_x=0x178, scatter_acceleration_y=0x17c;
inline constexpr Address spiral_script_a=0x5d2700, spiral_script_b=0x5d2710;
inline constexpr unsigned scatter_count=4, scatter_mode=0x31, scatter_y_bias=0x80000;
inline constexpr std::int32_t scatter_x_extent=24, scatter_x_variants=8, scatter_y_extent=6, scatter_y_variants=6;
inline constexpr std::int32_t scatter_speed_scale=0x2000, scatter_x_speed_variants=1024, scatter_y_speed_variants=512;
inline constexpr unsigned scatter_x_acceleration=0x8000, scatter_y_acceleration=0x6000, scatter_script_count=3;
inline constexpr std::int32_t scatter_x_speed_cap=0x60000, scatter_y_speed_cap=0x20000;
inline constexpr unsigned spiral_mode=0x72, spiral_z_radius=24, spiral_step=0x666;
inline constexpr unsigned half_turn=0x8000, quarter_turn=0x4000, three_quarters=0xc000;
inline constexpr std::int32_t initialize=-1, cleanup=-2, active=0, looping=10;
inline constexpr unsigned script_active=0x20000, visible=0x40, phase_step=10;
inline constexpr unsigned mark_interval=24, first_rising=10, rising_interval=20;
inline constexpr unsigned mark_motion=0x41, orbit_motion=0x32, rising_motion=0x54;
inline constexpr unsigned mark_velocity=0x20000, ring_height=0x240000, script_ring_height=0x200000;
inline constexpr unsigned ring_target=0x300000, ring_speed_base=0x4000, ring_radius_x=32, ring_radius_y=24, script_ring_radius_y=12;
inline constexpr unsigned rising_x_bias=0x100000, rising_y_bias=0x10000, rising_z_bias=0x400000;
inline constexpr unsigned rising_sprite=0xdb, rising_frame=4, ring_sprite=0xde, ring_frame_base=10;
inline constexpr unsigned ring_count=3, ring_spacing=0x5555, orbit_step=0x20c, turn=0x10000;
inline constexpr std::int32_t mark_x_variants=48, mark_x_center=24, mark_y_variants=32, mark_y_center=16;
inline constexpr std::int32_t mark_z_variants=8, mark_z_base=48, ring_speed_variants=1000, ring_speed_scale=8000;
inline constexpr std::int32_t ring_frame_variants=4, rising_phase_variants=0x1000;
}
struct AnchorEffectServices {
    std::function<Address(Address callback)> spawn;
    std::function<void(Address object)> release, oscillate, spark_cluster, spawn_rising;
    std::function<void(Address object,Address script)> start_script;
    std::function<std::uint32_t()> random;
};
class AnchorEffects {
public:
    AnchorEffects(Memory& memory,const AnchorEffectServices& services):memory_(memory),services_(services){}
    void tick_exploding_mark(Address object); //4624ad
    void emit_floating_marks(Address object); //46256a
    void spawn_rising(Address source); //4626ab
    void emit_rising(Address object); //46274d
    void tick_orbit(Address object); //4629cb
    void scatter_cluster(Address object); //4627eb
    void spiral(Address object,bool second); //462c06/462cf4
    void orbit_ring(Address object,bool scripted); //462a49/462b50
private:
    Memory& memory_;
    const AnchorEffectServices& services_;
    void anchor_position(Address object,Address anchor);
    std::int32_t random_remainder(std::int32_t divisor);
};
}
