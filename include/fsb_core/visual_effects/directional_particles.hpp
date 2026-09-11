#pragma once
#include "fsb_core/visual_effects/effect_object.hpp"
#include "fsb_core/visual_effects/effect_services.hpp"

namespace fsb::core::visual_effects {

// motion_flags value the nine helpers write. 462de2 clears the upper two bits
// once the particle has reached its anchor, which is what ends the travel.
inline constexpr std::int32_t travelling_motion_flags = 0x31;
inline constexpr std::int32_t travel_in_progress_bits = 0x30;

// A particle that travels from an origin object to a target and reports one hit
// when it arrives.
//
// Nine helpers build these. They share an opening — start at the origin's
// placement, aim the motion anchor at the target, pick a drift axis and sign
// from the origin's facing — and differ in how far they lift the flight, how
// fast they drift, and what they give the child to display on the way.
//
// The originals take the target first and the origin second; that order is kept
// so a caller reads the same as the original call.
class DirectionalHitParticles {
public:
    DirectionalHitParticles(Memory& memory, const EffectServices& services)
        : memory_(memory), services_(services) {}

    // 46dca5. The shared per-frame tick of every child below.
    void tick(Address child);

    // The nine helpers, by original entry point.
    Address spawn_hit(Address target, Address origin);                    // 46d904
    Address spawn_double_hit(Address target, Address origin);             // 46dcd7
    Address spawn_high_hit(Address target, Address origin);               // 46f684
    Address spawn_flat_cue(Address target, Address origin);               // 46f92f
    Address spawn_slow_arc_hit(Address target, Address origin);           // 46a8e9
    Address spawn_fast_arc_trigger_hit(Address target, Address origin);   // 46b772
    Address spawn_deferred_trigger(Address target, Address origin);       // 46e344
    Address spawn_facing_deferred_trigger(Address target, Address origin); // 46e8b3
    Address spawn_poison_liquid(Address target, Address origin);          // 470b90
    // Reports to a different tick than the nine above, so it names its own.
    Address spawn_directional_expand(Address target, Address origin);     // 469321

    static constexpr Address child_callback = 0x46dca5;
    static constexpr Address expand_callback = 0x4692e5;

private:
    // The opening every helper shares. `lift` raises the flight; all but one
    // helper raise the elevation, 46f92f raises the ground-plane y instead.
    Address begin(Address callback, Address target, Address origin, std::int32_t lift,
                  bool lift_on_ground_plane, std::int32_t drift_along_x, std::int32_t drift_along_y);
    std::int32_t facing_of(Address origin) const;
    // Tails. Each copies the origin facing onto the child first.
    Address with_script_table(Address child, std::int32_t facing, Address table);
    Address with_script(Address child, std::int32_t facing, Address script);
    Address with_pose(Address child, std::int32_t facing, std::int32_t selector, std::int32_t frame);

    Memory& memory_;
    const EffectServices& services_;
};

}
