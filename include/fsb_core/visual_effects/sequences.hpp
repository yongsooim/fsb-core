#pragma once
#include "fsb_core/visual_effects/effect_object.hpp"
#include "fsb_core/visual_effects/effect_services.hpp"

namespace fsb::core::visual_effects {

// A skill sequence that fans one child out over every target and waits for them.
//
// The skill callback table forwards into these: a row names the sequence
// routine and the script table the acting actor's own animation comes from.
// They all walk the same phases —
//
//   0   flash the screen in, then advance
//   10  run the acting actor's script for its facing, then advance
//   30  wait until every child has reported, then go to flash out
//   40  flash the screen out, then release the sequence
//
// — while -100 arrives when the actor's script ends and fans the children out,
// and -200 arrives as each child finishes. What differs between them is which
// child they spawn, which counter they keep it in, and what they do to the
// targets on the way.
class TargetFanoutSequence {
public:
    TargetFanoutSequence(Memory& memory, const EffectServices& services)
        : memory_(memory), services_(services) {}

    void recover_targets(Address sequence, Address actor_scripts);      // 47be07
    void shared_status_buff(Address sequence, Address actor_scripts);   // 485ccf
    void all_target_lift_numbers(Address sequence, Address actor_scripts); // 467be6
    // 46fffd has no screen flash and only two forward phases; it belongs here
    // because it fans the same way and drains the same counters.
    void descending_particle_burst(Address sequence, Address actor_scripts); // 46fffd
    // These two take a weapon tier rather than a script table: the count of
    // falling bursts each target gets.
    void single_target_particle_attack(Address sequence, std::int32_t bursts);   // 483179
    void multitarget_particle_attack(Address sequence, std::int32_t bursts);     // 482f4d
    // Works on the first target only, and raises its number from a child that
    // reports back rather than at the moment of the hit.
    void first_target_directional_burst(Address sequence, Address actor_scripts); // 4693ef
    // Touches one target every eighth tick rather than all at once.
    void target_flash_drift(Address sequence, Address actor_scripts);           // 467ed6

private:
    std::int32_t target_count() const { return std::int32_t(memory_.read(battle_targets_count)); }
    Address target(std::int32_t index) const;
    // Where the notifying child's target sits in the array, or the count when
    // it is not there. The originals do not guard that, and neither does this.
    std::int32_t notifying_target_index(Address sequence) const;
    bool flash(Address sequence, std::int32_t mode);
    void run_actor_script(Address actor_scripts);
    void adjust_pending(Address sequence, Address counter, int delta);
    // Every child reported and every per-target slot clear.
    bool drained(Address sequence) const;
    // Whether every per-target slot is clear, on its own.
    bool target_slots_clear(Address sequence) const;
    // The per-target work the particle attacks do: raise the number, restore
    // the motion block, run the target's hit script, place a burst object and
    // as many falling bursts as the tier asks for.
    void strike_target(Address sequence, std::int32_t index, std::int32_t bursts);
    void clear_target_slots(Address sequence);
    // Restore the target's motion, run its hit script and drop a burst object
    // on it — the opening several sequences share on a per-target notify.
    void open_target_hit(Address sequence, std::int32_t index);
    void mark_notifier_done(Address sequence);

    static constexpr Address battle_targets_count = 0x77a50c;
    static constexpr std::int32_t flash_frames = 30;

    Memory& memory_;
    const EffectServices& services_;
};

// Fields the particle attacks keep beside the shared ones.
namespace attack_field {
inline constexpr Address target_cursor = 0x78;   // 16-bit; which target is next.
inline constexpr Address numbers_pending = 0x7c; // 16-bit.
inline constexpr Address target_slots = 0x3c;    // 16-bit per target; bit 0 means busy.
inline constexpr unsigned target_slot_bytes = 0x40;
}

// The acting actor's own scripts for the two particle attacks.
inline constexpr Address single_attack_actor_scripts = 0x5f6878;
inline constexpr Address single_attack_repeat_scripts = 0x5f6a48;
inline constexpr Address multitarget_actor_scripts = 0x5f6688;
// The descriptor 4693ef gives the child that reports the number back.
inline constexpr Address first_target_number_descriptor = 0x5d3140;
// How often 467ed6 lets one more target start: every eighth tick.
inline constexpr std::uint32_t flash_drift_stagger = 7;

// Facing tables the descending burst reads: the acting actor's facing is
// remapped once, and both the target script and the burst descriptor are
// chosen from tables the battle scope owns.
inline constexpr Address facing_remap = 0x5bf498;
inline constexpr Address target_hit_scripts = 0x5d2b18;
inline constexpr Address burst_descriptors = 0x5d2f40;

// The child 47be07 gives each target: it only marks the target and plays a cue.
class RecoverTargetChild {
public:
    RecoverTargetChild(Memory& memory, const EffectServices& services)
        : memory_(memory), services_(services) {}
    void spawn(Address target);   // 47bde6
    static constexpr Address child_callback = 0x47bd47;

private:
    Memory& memory_;
    const EffectServices& services_;
};

// The child 467be6 gives each target: nothing but a link back to it.
class LiftNumberChild {
public:
    LiftNumberChild(Memory& memory, const EffectServices& services)
        : memory_(memory), services_(services) {}
    void spawn(Address target);   // 467bcf
    static constexpr Address child_callback = 0x4679c6;

private:
    Memory& memory_;
    const EffectServices& services_;
};

// Eight particles thrown from above a target with random horizontal speeds,
// each falling until it reaches the floor.
class DescendingBurst {
public:
    DescendingBurst(Memory& memory, const EffectServices& services)
        : memory_(memory), services_(services) {}
    void spawn(Address target);   // 46ff7a
    void tick(Address particle);  // 46ff40
    static constexpr Address child_callback = 0x46ff40;
    static constexpr Address child_script = 0x5e81c0;

private:
    Memory& memory_;
    const EffectServices& services_;
};

// The child 467ed6 sends to each target: a flash that reports twice, once two
// ticks in and once when its script ends.
class TargetFlashNotify {
public:
    TargetFlashNotify(Memory& memory, const EffectServices& services)
        : memory_(memory), services_(services) {}
    void spawn(Address target);   // 467e8c
    void tick(Address effect);    // 467e47
    static constexpr Address child_callback = 0x467e47;
    static constexpr Address child_script = 0x5d3e48;

private:
    Memory& memory_;
    const EffectServices& services_;
};

// The child 485ccf gives each target: a sprite just above it, running one script.
class StatusBuffChild {
public:
    StatusBuffChild(Memory& memory, const EffectServices& services)
        : memory_(memory), services_(services) {}
    void spawn(Address target);   // 485c80
    static constexpr Address child_callback = 0x485c52;
    static constexpr Address child_script = 0x5f7758;

private:
    Memory& memory_;
    const EffectServices& services_;
};

}
