#pragma once
#include "fsb_core/visual_effects/effect_object.hpp"
#include "fsb_core/visual_effects/effect_services.hpp"

namespace fsb::core::visual_effects {

// Battle-wide storage the sweep controller and its children read. These belong
// to the battle scope; nothing here writes them except where noted.
namespace battle {
inline constexpr Address active_actor = 0x8059f0;   // Pointer to the acting object.
inline constexpr Address target_objects = 0x8059f8; // Array of target object pointers.
inline constexpr Address target_count = 0x77a50c;
inline constexpr Address result_amounts = 0x773f90; // 16-byte records; first word is the amount.
inline constexpr Address target_hit_scripts = 0x5d2dd8; // Indexed by the target's script index.
inline constexpr unsigned result_record_bytes = 16;
}

// Fields an orbiting ring child uses. Above 0x144 the record is per-callback
// work storage, so these names describe this family's use of those bytes, not
// a layout every callback shares.
namespace ring_field {
inline constexpr Address suppress_trail = 0x144; // Low bit; aliases the tick counter slot.
inline constexpr Address motion_radius = 0x190;  // What the motion helper consumes this frame.
inline constexpr Address radius_counter = 0x194; // Drains in the spin phase, grows in the finish phase.
inline constexpr Address radius_step = 0x198;
inline constexpr Address motion_angle = 0x19c;   // Consumed with motion_radius.
inline constexpr Address angle_accumulator = 0x1a0;
inline constexpr Address angle_step = 0x1a4;
}

// Fields the objects a sweep touches use.
namespace sweep_field {
inline constexpr Address target_pending_flags = 0x3c; // One 16-bit entry per target.
inline constexpr Address ring_children_pending = 0x78; // 16-bit.
inline constexpr Address numbers_pending = 0x7a;       // 16-bit.
inline constexpr Address script_index = 0x110;
}

namespace sweep_flag {
inline constexpr std::uint32_t target_visual_busy = 0x10000; // Byte +6, bit 0.
inline constexpr std::uint32_t target_script_ready = 0x100;  // Byte +5, bit 0.
}

// Phases a sweep controller and its ring children step through, and the codes
// children report back through the controller's callback state.
namespace phase {
inline constexpr std::int32_t ring_inactive = -1;
inline constexpr std::int32_t ring_rise = 0;
inline constexpr std::int32_t ring_spin = 10;
inline constexpr std::int32_t ring_finish = 20;
inline constexpr std::int32_t ring_step = 10;

inline constexpr std::int32_t sweep_flash_in = 0;
inline constexpr std::int32_t sweep_actor_script = 10;
inline constexpr std::int32_t sweep_wait_drain = 30;
inline constexpr std::int32_t sweep_flash_out = 40;

inline constexpr std::int32_t notify_numbers_done = -20;
inline constexpr std::int32_t notify_ready = -150;
inline constexpr std::int32_t notify_actor_done = -100;
inline constexpr std::int32_t notify_numbers_ready = -110;
// A child that carried a number up reporting that it has arrived.
inline constexpr std::int32_t notify_number_arrived = -210;
// One target's drift children have finished.
inline constexpr std::int32_t notify_drift_done = -300;
inline constexpr std::int32_t notify_target_hit = -200;
inline constexpr std::int32_t notify_ring_done = -250;
}

// The four children of one ring, and the shared tick that moves them.
//
// Only the last child carries the owner-notify flag and the owner link, so the
// ring reports completion once rather than once per child.
class RingEffect {
public:
    RingEffect(Memory& memory, const EffectServices& services)
        : memory_(memory), services_(services) {}
    // 467417. Returns the angle carry the original leaves in EAX.
    std::int32_t spawn(Address owner);
    // 4672b5. One frame of one child.
    void tick(Address child);

    // The address the spawned children carry as their callback. It stays an
    // original address while stage 3 has not moved callback references.
    static constexpr Address child_callback = 0x4672b5;
    static constexpr Address child_script = 0x5d34e0;

private:
    Memory& memory_;
    const EffectServices& services_;
};

// The all-target sweep: flash in, run the acting actor's script, fan one ring
// out per target, wait for every child to report, then flash out.
class SweepController {
public:
    SweepController(Memory& memory, const EffectServices& services)
        : memory_(memory), services_(services) {}
    // 4674de. `actor_scripts` is the caller's script table for the acting actor.
    void run(Address controller, Address actor_scripts);

private:
    // Index of `object` in the target array, or the target count when absent.
    // The original does not guard that case, and neither does this.
    std::int32_t target_index(Address object) const;
    std::int32_t target_count() const { return std::int32_t(memory_.read(battle::target_count)); }
    Address target(std::int32_t index) const { return memory_.read(battle::target_objects + 4 * std::uint32_t(index)); }
    void launch_rings(Address controller);
    bool targets_drained(Address controller) const;

    Memory& memory_;
    const EffectServices& services_;
};

// Four particles that orbit an anchor and drift apart, at quarter-turn spacing.
// 467db3 leaves the radius at zero and lets its script open it out.
class DriftEffect {
public:
    DriftEffect(Memory& memory, const EffectServices& services)
        : memory_(memory), services_(services) {}
    void spawn(Address source);   // 467db3
    static constexpr Address child_callback = 0x467d53;
    static constexpr Address child_script = 0x5d3e88;

private:
    Memory& memory_;
    const EffectServices& services_;
};

// Four pieces of debris thrown from a point with random velocities and a
// random starting frame.
class DebrisEffect {
public:
    DebrisEffect(Memory& memory, const EffectServices& services)
        : memory_(memory), services_(services) {}
    void spawn(Address source);   // 4681d6
    static constexpr Address child_callback = 0x46818d;

private:
    Memory& memory_;
    const EffectServices& services_;
};

// One object that rises from just behind a source, wearing the acting actor's
// sprite.
class RisingEffect {
public:
    RisingEffect(Memory& memory, const EffectServices& services)
        : memory_(memory), services_(services) {}
    void spawn(Address source);   // 4860ff
    static constexpr Address child_callback = 0x4860c3;

private:
    Memory& memory_;
    const EffectServices& services_;
};

// A particle whose animation stage follows its own elevation. The stage indexes
// a table of effect scripts, and the script is only restarted when the stage
// changes; past a ceiling the particle releases itself.
class AnimatedParticle {
public:
    AnimatedParticle(Memory& memory, const EffectServices& services)
        : memory_(memory), services_(services) {}
    void tick(Address particle);  // 46a1b6
    static constexpr Address stage_scripts = 0x5d4270;

private:
    Memory& memory_;
    const EffectServices& services_;
};

// A sprite dropped at another object's exact placement, left to run one script
// and release itself when the script ends.
class TrailSprite {
public:
    TrailSprite(Memory& memory, const EffectServices& services)
        : memory_(memory), services_(services) {}
    void spawn(Address source);   // 46dfdf
    static constexpr Address child_callback = 0x46dfc3;
    static constexpr Address child_script = 0x5e0300;

private:
    Memory& memory_;
    const EffectServices& services_;
};

}
