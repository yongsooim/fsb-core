#pragma once
#include "fsb_core/visual_effects/effect_object.hpp"
#include "fsb_core/visual_effects/effect_services.hpp"

namespace fsb::core::visual_effects {

// The battle code parks the running screen shake object here.
inline constexpr Address active_screen_shake = 0x806520;

// Effects whose whole tick is: take one motion step if they have one, and once
// they are done, report if they have someone to report to, then release
// themselves. They differ only in what counts as done and what they report.
//
// All of them do nothing unless the effect is in phase zero.
class FinishingEffects {
public:
    FinishingEffects(Memory& memory, const EffectServices& services)
        : memory_(memory), services_(services) {}

    void release_after_twenty_ticks(Address effect);        // 481b11
    void release_when_script_ends(Address effect);          // 46dfc3
    void travel_then_release(Address effect);               // 46c5bb
    void drift_until_script_ends(Address effect);           // 4719e2
    void report_hit_when_script_ends(Address effect);       // 4705c6
    void report_actor_done_when_script_ends(Address effect); // 478c12
    void report_owner_hit_when_script_ends(Address effect); // 485c52
    void travel_then_report_done(Address effect);           // 47eb35
    void report_ready_when_script_ends(Address effect);     // 4727b4
    void scatter_drift_when_script_ends(Address effect);    // 482bd8
    void drop_then_scatter_debris(Address effect);          // 4682cc
    void fall_then_raise_successor(Address effect);         // 48613c

    // Effects that travel to their motion anchor and report once they land.
    // They differ in the cue they play, who they report to, and whether they
    // report the flight as well as the hit.
    void directional_expand_impact(Address effect);      // 4692e5
    void projectile_impact(Address effect);              // 46d24c
    void particle_impact(Address effect);                // 46ed0e
    void dual_notify_impact(Address effect);             // 46f1f6
    void homing_impact(Address effect);                  // 47fcb7
    void homing_impact_with_cue(Address effect);         // 47f526
    void homing_impact_on_first_target(Address effect);  // 47dc33
    void homing_impact_on_link(Address effect);          // 483380
    void homing_impact_on_link_with_cue(Address effect); // 4823e1
    void homing_impact_after_delay(Address effect);      // 484f62
    void directional_trail_impact(Address effect);       // 46e013
    void arc_projectile_impact(Address effect);          // 4837df
    // Rises instead of travelling, and only reports if it is the one asked to.
    void rising_sprite_notify(Address effect);           // 47219f

    // 4692c6. Not a tick: stops the running screen shake, if there is one.
    void stop_screen_shake();

private:
    bool idle(Address effect) const;      // Phase zero and the effect script finished.
    void release(Address effect);
    // Phase zero, one step of the homing clamp, and whether it has arrived.
    bool arrived(Address effect);
    void report_and_release(Address effect, Address reported, bool also_flight);
    void play(Address effect, unsigned cue);
    Memory& memory_;
    const EffectServices& services_;
};

}
